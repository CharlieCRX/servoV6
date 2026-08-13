// ============================================================================
// AsioModbusTcpClient.cpp —— Step 5 transport: 真实 Modbus TCP 客户端实现
// ============================================================================
// 迁移自旧 infrastructure/plc/protocol/AsioModbusTcpClient（已修复悬空引用：
// 用 shared_ptr 承载超时标志，不再捕获栈上局部引用），并适配 vnext 接口：
//   - 返回 contracts::CommunicationResult，去掉对 ISystemDriver 依赖；
//   - 0 基址原样透传，业务偏移一律由 layout 层负责；
//   - 断线/超时/重连期间绝不自动重放命令（每笔事务独立，不缓存请求）。
//
// 线程模型：
//   - io_context::run() 在独立工作线程执行，socket 操作在该线程内串行；
//   - 业务线程经 promise/future 同步等待；单 io 线程天然串行，事务不交叉；
//   - 超时机制：平台级 socket 收发超时（SO_SNDTIMEO/SO_RCVTIMEO）让阻塞
//     read/write 在断网时快速返回；future.wait_for(timeoutMs) 在调用方线程
//     兜底（超时则 native shutdown 解阻塞 io 线程）。
// ============================================================================
#include "infrastructure/plc_vnext/transport/AsioModbusTcpClient.h"

// Windows 宏污染修复：<windows.h>（由 Asio / AsioModbusTcpClient.h 传递引入）
// 定义 ERROR/DEBUG/TRACE 宏，会破坏 Logger 的 LogLevel::ERROR 等枚举值。
// 必须在 #include Logger.h 之前取消这些宏，否则枚举定义即被替换。
#ifdef ERROR
#undef ERROR
#endif
#ifdef DEBUG
#undef DEBUG
#endif
#ifdef TRACE
#undef TRACE
#endif

#include "infrastructure/logger/Logger.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace plc_vnext::transport {

namespace {
// 平台 socket 收发超时：作为断网/无响应的快速解阻塞兜底（毫秒）。
// 覆盖 Windows(WSAETIMEDOUT=10060) 与 POSIX(ETIMEDOUT=110)。
constexpr unsigned kPlatformIOTimeoutMs = 200u;

bool isTimeoutError(const std::error_code& ec) {
    if (ec == asio::error::operation_aborted) return true;
    if (ec == asio::error::timed_out) return true;
    return ec.value() == 10060 || ec.value() == 110;
}

void appendBE16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

uint16_t readBE16(const std::vector<uint8_t>& b, size_t off) {
    return static_cast<uint16_t>((static_cast<uint16_t>(b[off]) << 8) |
                                 static_cast<uint16_t>(b[off + 1]));
}

std::string toHex2(uint8_t v) {
    std::ostringstream o;
    o << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(v);
    return o.str();
}

std::string toHex4(uint16_t v) {
    std::ostringstream o;
    o << std::hex << std::setw(4) << std::setfill('0') << v;
    return o.str();
}

// 事务结果载体：通讯结果 + 读回响应帧。经 promise/future 在 io 线程与调用方
// 线程之间安全传递，杜绝 lambda 捕获调用方局部引用（超时后悬空引用风险）。
struct TransactionOutcome {
    contracts::CommunicationResult result = contracts::CommunicationResult::sent();
    std::vector<uint8_t> response;  // 仅 result.ok() 时有意义
};
}  // namespace

// ========================================================================
//  构造 / 析构
// ========================================================================

AsioModbusTcpClient::AsioModbusTcpClient(const Config& config)
    : m_config(config)
    , m_ioctx()
    , m_socket(m_ioctx)
    , m_timer(m_ioctx)
    , m_moduleName("ModbusTCP|" + config.host + ":" + std::to_string(config.port)) {
    std::ostringstream oss;
    oss << "AsioModbusTcpClient constructed (unitId=" << static_cast<int>(config.unitId)
        << ", timeout=" << config.timeoutMs << "ms, reconnect="
        << config.reconnectIntervalMs << "ms)";
    LOG_INFO(LogLayer::HAL, m_moduleName, oss.str());
}

AsioModbusTcpClient::~AsioModbusTcpClient() {
    LOG_DEBUG(LogLayer::HAL, m_moduleName, "destructor -- calling stop()");
    stop();
    LOG_DEBUG(LogLayer::HAL, m_moduleName, "destructor -- done");
}

// ========================================================================
//  连接生命周期
// ========================================================================

void AsioModbusTcpClient::start() {
    if (m_running.load(std::memory_order_acquire)) {
        LOG_DEBUG(LogLayer::HAL, m_moduleName, "start() called but already running -- no-op");
        return;
    }

    m_running.store(true, std::memory_order_release);
    m_ioctx.restart();
    if (!m_workGuard) {
        m_workGuard = std::make_unique<asio::io_context::work>(m_ioctx);
    }

    m_worker = std::thread([this]() {
        LOG_DEBUG(LogLayer::HAL, m_moduleName, "worker thread started");
        startReconnect();  // 发起首次连接
        m_ioctx.run();
    });
}

void AsioModbusTcpClient::stop() {
    if (!m_running.load(std::memory_order_acquire)) {
        // 可能从未 start 过；仍关闭 socket 以防半初始化状态
        std::error_code ec;
        m_socket.close(ec);
        return;
    }

    // 先置停止标志：阻断 scheduleReconnect / startReconnect 再次调度重连，
    // 否则重连定时器链会让 io_context 永不 drain，worker.join() 挂死。
    m_running.store(false, std::memory_order_release);

    // 在 io 线程关闭 socket 并取消定时器，避免与正在执行的 I/O 竞争
    std::error_code ec;
    asio::post(m_ioctx, [this]() {
        std::error_code e;
        m_socket.cancel(e);
        m_socket.close(e);
        m_timer.cancel(e);
    });
    m_workGuard.reset();  // 让 io_context 自然 drain
    if (m_worker.joinable()) m_worker.join();

    m_connected.store(false, std::memory_order_release);
    LOG_INFO(LogLayer::HAL, m_moduleName, "stop() -- shutdown complete");
}

bool AsioModbusTcpClient::isConnected() const {
    return m_connected.load(std::memory_order_acquire);
}

void AsioModbusTcpClient::requestReconnect() {
    if (!m_running.load(std::memory_order_acquire)) {
        start();
        return;
    }
    asio::post(m_ioctx, [this]() {
        std::error_code ec;
        if (m_socket.is_open()) m_socket.close(ec);
        m_connected.store(false, std::memory_order_release);
        m_timer.cancel(ec);
        LOG_INFO(LogLayer::HAL, m_moduleName, "requestReconnect() -- immediate reconnect");
        startReconnect();
    });
}

// ========================================================================
//  IModbusClient —— 读通道（0 基址原样透传）
// ========================================================================

contracts::CommunicationResult AsioModbusTcpClient::readCoils(
    uint16_t startAddress, uint16_t count, std::vector<uint8_t>& payload) {
    if (!m_connected.load(std::memory_order_acquire)) {
        return contracts::CommunicationResult::disconnected(
            "Not connected to " + m_moduleName);
    }
    uint16_t tid = nextTransactionId();
    std::vector<uint8_t> pdu;
    pdu.push_back(0x01);
    appendBE16(pdu, startAddress);
    appendBE16(pdu, count);
    auto frame = buildFrame(tid, 0x01, pdu);

    std::vector<uint8_t> response;
    auto result = executeTransaction(frame, response);
    if (!result.ok()) return result;

    // 解析：MBAP(7) | FC(1) | byteCount(1) | bits...
    if (response.size() < 9) {
        return contracts::CommunicationResult{
            contracts::CommunicationResult::Status::InvalidResponse, 0,
            "FC01 response too short"};
    }
    uint8_t byteCount = response[8];
    if (response.size() < 9u + byteCount) {
        return contracts::CommunicationResult{
            contracts::CommunicationResult::Status::InvalidResponse, 0,
            "FC01 response byteCount mismatch"};
    }
    payload.assign(response.begin() + 9, response.begin() + 9 + byteCount);
    return result;
}

contracts::CommunicationResult AsioModbusTcpClient::readHoldingRegisters(
    uint16_t startAddress, uint16_t count, std::vector<uint16_t>& payload) {
    if (!m_connected.load(std::memory_order_acquire)) {
        return contracts::CommunicationResult::disconnected(
            "Not connected to " + m_moduleName);
    }
    uint16_t tid = nextTransactionId();
    std::vector<uint8_t> pdu;
    pdu.push_back(0x03);
    appendBE16(pdu, startAddress);
    appendBE16(pdu, count);
    auto frame = buildFrame(tid, 0x03, pdu);

    std::vector<uint8_t> response;
    auto result = executeTransaction(frame, response);
    if (!result.ok()) return result;

    // 解析：MBAP(7) | FC(1) | byteCount(1) | regs(2*count)
    if (response.size() < 9) {
        return contracts::CommunicationResult{
            contracts::CommunicationResult::Status::InvalidResponse, 0,
            "FC03 response too short"};
    }
    uint8_t byteCount = response[8];
    if (response.size() < 9u + byteCount) {
        return contracts::CommunicationResult{
            contracts::CommunicationResult::Status::InvalidResponse, 0,
            "FC03 response byteCount mismatch"};
    }
    payload.clear();
    payload.reserve(byteCount / 2);
    for (size_t i = 0; i + 1 < byteCount; i += 2) {
        payload.push_back(readBE16(response, 9u + i));
    }
    return result;
}

// ========================================================================
//  IModbusClient —— 写通道（0 基址原样透传）
// ========================================================================

contracts::CommunicationResult AsioModbusTcpClient::writeSingleCoil(
    uint16_t address, bool value) {
    if (!m_connected.load(std::memory_order_acquire)) {
        return contracts::CommunicationResult::disconnected(
            "Not connected to " + m_moduleName);
    }
    uint16_t tid = nextTransactionId();
    std::vector<uint8_t> pdu;
    pdu.push_back(0x05);
    appendBE16(pdu, address);
    appendBE16(pdu, value ? 0xFF00 : 0x0000);
    auto frame = buildFrame(tid, 0x05, pdu);

    std::vector<uint8_t> response;
    return executeTransaction(frame, response);
}

contracts::CommunicationResult AsioModbusTcpClient::writeSingleRegister(
    uint16_t address, uint16_t value) {
    if (!m_connected.load(std::memory_order_acquire)) {
        return contracts::CommunicationResult::disconnected(
            "Not connected to " + m_moduleName);
    }
    uint16_t tid = nextTransactionId();
    std::vector<uint8_t> pdu;
    pdu.push_back(0x06);
    appendBE16(pdu, address);
    appendBE16(pdu, value);
    auto frame = buildFrame(tid, 0x06, pdu);

    std::vector<uint8_t> response;
    return executeTransaction(frame, response);
}

contracts::CommunicationResult AsioModbusTcpClient::writeMultipleRegisters(
    uint16_t startAddress, const std::vector<uint16_t>& values) {
    if (!m_connected.load(std::memory_order_acquire)) {
        return contracts::CommunicationResult::disconnected(
            "Not connected to " + m_moduleName);
    }
    uint16_t tid = nextTransactionId();
    std::vector<uint8_t> pdu;
    pdu.push_back(0x10);
    appendBE16(pdu, startAddress);
    appendBE16(pdu, static_cast<uint16_t>(values.size()));
    pdu.push_back(static_cast<uint8_t>(values.size() * 2));
    for (uint16_t v : values) appendBE16(pdu, v);
    auto frame = buildFrame(tid, 0x10, pdu);

    std::vector<uint8_t> response;
    return executeTransaction(frame, response);
}

uint16_t AsioModbusTcpClient::nextTransactionId() {
    return m_transactionId.fetch_add(1, std::memory_order_relaxed);
}

std::vector<uint8_t> AsioModbusTcpClient::buildFrame(
    uint16_t tid, uint8_t fc, const std::vector<uint8_t>& pdu) {
    std::vector<uint8_t> frame;
    appendBE16(frame, tid);
    appendBE16(frame, 0x0000);                                 // Protocol ID
    appendBE16(frame, static_cast<uint16_t>(1 + pdu.size()));  // unit + pdu
    frame.push_back(m_config.unitId);
    frame.push_back(fc);
    frame.insert(frame.end(), pdu.begin() + 1, pdu.end());  // pdu[0]=fc，跳过
    return frame;
}

// ========================================================================
//  事务执行（异步-同步桥接，单 io 线程串行）
// ========================================================================

contracts::CommunicationResult AsioModbusTcpClient::executeTransaction(
    const std::vector<uint8_t>& frame, std::vector<uint8_t>& response) {
    if (!m_connected.load(std::memory_order_acquire)) {
        return contracts::CommunicationResult::disconnected(
            "Not connected to " + m_moduleName);
    }

    uint16_t tid = readBE16(frame, 0);
    uint8_t fc = frame[7];
    uint16_t reqAddr = readBE16(frame, 8);
    std::string moduleName = m_moduleName;

    // 响应数据不通过引用传给 io 线程，而是由 lambda 在内部构造并放入
    // TransactionOutcome，经 promise 传回。这样即使调用方超时提前返回、
    // io 线程随后才执行完 lambda，也不存在悬空引用（late 结果被共享状态丢弃）。
    std::promise<TransactionOutcome> promise;
    auto future = promise.get_future();

    asio::post(m_ioctx, [this, moduleName, frame, fc, tid, reqAddr,
                         p = std::move(promise), timeout = m_config.timeoutMs]() mutable {
        std::error_code ec;
        {
            std::ostringstream oss;
            oss << "req TID=" << tid << " FC=0x" << std::hex << std::uppercase
                << static_cast<int>(fc) << " addr=0x" << std::setw(4) << std::setfill('0')
                << reqAddr;
            LOG_TRACE(LogLayer::HAL, moduleName, oss.str());
        }

        // 1 -- 写请求帧
        asio::write(m_socket, asio::buffer(frame), ec);
        if (ec) {
            bool isTimeout = isTimeoutError(ec);
            m_connected.store(false, std::memory_order_release);
            std::ostringstream oss;
            oss << "write failed (TID=" << tid << ", FC=0x" << std::hex
                << static_cast<int>(fc) << "): " << ec.message();
            LOG_WARN(LogLayer::HAL, moduleName, oss.str());
            TransactionOutcome o;
            o.result = isTimeout
                           ? contracts::CommunicationResult{
                                 contracts::CommunicationResult::Status::Timeout, 0, oss.str()}
                           : contracts::CommunicationResult{
                                 contracts::CommunicationResult::Status::NetworkError, 0,
                                 oss.str()};
            p.set_value(std::move(o));
            scheduleReconnect();
            return;
        }

        // 2 -- 读响应（两步分帧 + MBAP/PDU/TID 校验 + 异常转换）
        TransactionOutcome o;
        auto result = readFullResponse(tid, fc, reqAddr, timeout, o.response);
        o.result = result;
        if (!result.ok()) {
            // 协议类错误（TID 不符 / 长度错）不视为断线，保持连接
            if (result.status == contracts::CommunicationResult::Status::InvalidResponse) {
                p.set_value(std::move(o));
                return;
            }
            m_connected.store(false, std::memory_order_release);
            p.set_value(std::move(o));
            scheduleReconnect();
            return;
        }
        p.set_value(std::move(o));
    });

    // 调用方线程兜底超时：若 io 线程被阻塞，native shutdown 解阻塞
    auto status = future.wait_for(std::chrono::milliseconds(m_config.timeoutMs + 500));
    if (status == std::future_status::timeout) {
        m_connected.store(false, std::memory_order_release);
        {
            auto sockfd = m_socket.native_handle();
            if (sockfd != asio::ip::tcp::socket::native_handle_type(-1)) {
#ifdef _WIN32
                ::shutdown(sockfd, SD_BOTH);
#else
                ::shutdown(sockfd, SHUT_RDWR);
#endif
            }
        }
        std::ostringstream oss;
        oss << "transaction timed out (TID=" << tid << ", FC=0x" << std::hex
            << static_cast<int>(fc) << ")";
        // 注意：lambda 可能仍在 io 线程执行，晚到的 TransactionOutcome 会写进
        // 已被放弃的 future（promise 仍由 lambda 持有），自然被丢弃，不访问任何
        // 已失效的内存。
        return contracts::CommunicationResult{
            contracts::CommunicationResult::Status::Timeout, 0, oss.str()};
    }
    auto outcome = future.get();
    if (outcome.result.ok()) response = std::move(outcome.response);
    return outcome.result;
}

// ========================================================================
//  两步分帧读取 + 协议校验 + 异常转换
// ========================================================================

contracts::CommunicationResult AsioModbusTcpClient::readFullResponse(
    uint16_t requestTid, uint8_t requestFc, uint16_t requestAddr, uint16_t timeout,
    std::vector<uint8_t>& response) {
    std::error_code ec;
    (void)timeout;  // 平台级 socket 收发超时已兜底，这里同步读取即可
    const std::string prefix = m_moduleName + " FC=0x" + toHex2(requestFc)
        + " addr=0x" + toHex4(requestAddr);

    // 2a -- 严格读 7 字节 MBAP
    std::array<uint8_t, 7> mbap{};
    std::size_t n = asio::read(m_socket, asio::buffer(mbap), ec);
    if (ec) {
        std::ostringstream oss;
        oss << "read MBAP failed (FC=0x" << std::hex << static_cast<int>(requestFc) << "): "
            << ec.message();
        LOG_WARN(LogLayer::HAL, m_moduleName, oss.str());
        return isTimeoutError(ec)
                   ? contracts::CommunicationResult{
                         contracts::CommunicationResult::Status::Timeout, 0, oss.str()}
                   : contracts::CommunicationResult{
                         contracts::CommunicationResult::Status::NetworkError, 0, oss.str()};
    }
    if (n < mbap.size()) {
        return contracts::CommunicationResult{
            contracts::CommunicationResult::Status::NetworkError, 0,
            "read MBAP: incomplete frame"};
    }

    // 2b -- 校验 Protocol ID 与 Length
    uint16_t protocolId = static_cast<uint16_t>(
        (static_cast<uint16_t>(mbap[2]) << 8) | static_cast<uint16_t>(mbap[3]));
    if (protocolId != 0x0000) {
        return contracts::CommunicationResult{
            contracts::CommunicationResult::Status::InvalidResponse, 0,
            "Invalid MBAP protocol ID"};
    }
    uint16_t length = static_cast<uint16_t>(
        (static_cast<uint16_t>(mbap[4]) << 8) | static_cast<uint16_t>(mbap[5]));
    if (length == 0) {
        return contracts::CommunicationResult{
            contracts::CommunicationResult::Status::InvalidResponse, 0,
            "Invalid MBAP length (zero)"};
    }

    // 2c -- 读 length-1 字节 PDU（unit 已在 MBAP 中读走）
    std::vector<uint8_t> pdu(static_cast<size_t>(length) - 1u);
    n = asio::read(m_socket, asio::buffer(pdu), ec);
    if (ec) {
        std::ostringstream oss;
        oss << "read PDU failed (FC=0x" << std::hex << static_cast<int>(requestFc) << "): "
            << ec.message();
        LOG_WARN(LogLayer::HAL, m_moduleName, oss.str());
        return isTimeoutError(ec)
                   ? contracts::CommunicationResult{
                         contracts::CommunicationResult::Status::Timeout, 0, oss.str()}
                   : contracts::CommunicationResult{
                         contracts::CommunicationResult::Status::NetworkError, 0, oss.str()};
    }
    if (n < pdu.size()) {
        return contracts::CommunicationResult{
            contracts::CommunicationResult::Status::InvalidResponse, 0,
            "read PDU: incomplete frame (length mismatch)"};
    }
    if (pdu.empty()) {
        return contracts::CommunicationResult{
            contracts::CommunicationResult::Status::InvalidResponse, 0,
            prefix + " empty PDU"};
    }

    // 2d -- 校验 Transaction ID
    uint16_t respTid = static_cast<uint16_t>(
        (static_cast<uint16_t>(mbap[0]) << 8) | static_cast<uint16_t>(mbap[1]));
    if (respTid != requestTid) {
        std::ostringstream oss;
        oss << prefix << " Transaction ID mismatch (req=" << requestTid
            << " resp=" << respTid << ")";
        LOG_WARN(LogLayer::HAL, m_moduleName, oss.str());
        return contracts::CommunicationResult{
            contracts::CommunicationResult::Status::InvalidResponse, 0, oss.str()};
    }

    // 2e -- 检查 Modbus 异常（responseFc == requestFc | 0x80）
    uint8_t responseFc = pdu[0];
    if (responseFc == static_cast<uint8_t>(requestFc | 0x80)) {
        uint8_t exCode = (pdu.size() >= 2) ? pdu[1] : 0;
        std::ostringstream oss;
        oss << prefix << " Modbus exception code=0x" << toHex2(exCode);
        LOG_WARN(LogLayer::HAL, m_moduleName, oss.str());
        if (exCode == 0x06) {
            return contracts::CommunicationResult{
                contracts::CommunicationResult::Status::Busy, exCode, oss.str()};
        }
        return contracts::CommunicationResult{
            contracts::CommunicationResult::Status::ProtocolError, exCode, oss.str()};
    }

    // 2f -- 成功：组装完整响应帧返回
    response.reserve(7u + pdu.size());
    response.insert(response.end(), mbap.begin(), mbap.end());
    response.insert(response.end(), pdu.begin(), pdu.end());
    return contracts::CommunicationResult::sent();
}

// ========================================================================
//  socket 配置
// ========================================================================

void AsioModbusTcpClient::configureSocket() {
    if (!m_socket.is_open()) return;

    std::error_code ec;
    m_socket.set_option(asio::ip::tcp::no_delay(true), ec);
    m_socket.set_option(asio::socket_base::keep_alive(true), ec);

    // 平台级收发超时：断网 / 无响应时让阻塞 read/write 快速返回，避免 io 线程
    // 长时间挂起（asio 自身不提供阻塞 socket 超时）。
    auto native = m_socket.native_handle();
#ifdef _WIN32
    DWORD socketTimeoutMs = kPlatformIOTimeoutMs;
    ::setsockopt(native, SOL_SOCKET, SO_SNDTIMEO,
                 reinterpret_cast<const char*>(&socketTimeoutMs), sizeof(socketTimeoutMs));
    ::setsockopt(native, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&socketTimeoutMs), sizeof(socketTimeoutMs));
#else
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = static_cast<suseconds_t>(kPlatformIOTimeoutMs * 1000);
    ::setsockopt(native, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    ::setsockopt(native, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    LOG_DEBUG(LogLayer::HAL, m_moduleName,
              "socket configured (no_delay, keep_alive, io_timeout="
                  + std::to_string(kPlatformIOTimeoutMs) + "ms)");
}

// ========================================================================
//  连接与重连
// ========================================================================

void AsioModbusTcpClient::startReconnect() {
    if (!m_running.load(std::memory_order_acquire)) {
        LOG_DEBUG(LogLayer::HAL, m_moduleName, "startReconnect() skipped -- not running");
        return;
    }

    auto resolver = std::make_shared<asio::ip::tcp::resolver>(m_ioctx);
    std::string moduleName = m_moduleName;
    std::string portStr = std::to_string(m_config.port);

    resolver->async_resolve(
        m_config.host, portStr,
        [this, moduleName, resolver](std::error_code ec,
                                     asio::ip::tcp::resolver::results_type endpoints) {
            if (ec) {
                LOG_WARN(LogLayer::HAL, moduleName,
                         "DNS resolve failed: " + ec.message());
                scheduleReconnect();
                return;
            }
            asio::async_connect(
                m_socket, endpoints,
                [this, moduleName](std::error_code cec,
                                   asio::ip::tcp::endpoint) {
                    if (!cec) {
                        configureSocket();
                        m_connected.store(true, std::memory_order_release);
                        LOG_INFO(LogLayer::HAL, moduleName, "connected");
                        return;
                    }
                    std::error_code ignored;
                    m_socket.close(ignored);
                    LOG_WARN(LogLayer::HAL, moduleName,
                             "connect failed: " + cec.message());
                    scheduleReconnect();
                });
        });
}

void AsioModbusTcpClient::scheduleReconnect() {
    if (!m_running.load(std::memory_order_acquire)) return;

    std::string moduleName = m_moduleName;
    m_timer.expires_after(std::chrono::milliseconds(m_config.reconnectIntervalMs));
    m_timer.async_wait([this, moduleName](std::error_code ec) {
        if (ec) return;  // 定时器被取消（stop / requestReconnect）
        if (!m_running.load(std::memory_order_acquire)) return;
        startReconnect();
    });
}

// ========================================================================
//  诊断日志
// ========================================================================

void AsioModbusTcpClient::diag(const std::string& level, const std::string& msg) const {
    // 统一诊断入口：所有日志均带 moduleName（含 endpoint），msg 内含功能码 /
    // 地址 / 错误类型，满足「日志可定位 endpoint + FC + 地址 + 原因」。
    (void)level;
    LOG_TRACE(LogLayer::HAL, m_moduleName, msg);
}

}  // namespace plc_vnext::transport
