// infrastructure/plc/protocol/AsioModbusTcpClient.cpp
// P4 Phase 2 -- AsioModbusTcpClient 工业级实现
//
// 架构演进:
//   Phase 2.0 -- 基础同步 I/O + promise/future 桥接
//   Phase 2.1 -- executor_work_guard 防止 io_context 提前退出
//   Phase 2.2 -- 工业级 stop 序列 + 线程生命周期日志
//   Phase 2.3 -- socket 级超时 (expires_after) + 全链路可观测性
//   Phase 2.4 -- 日志统一到 Logger 框架
//
// 线程模型:
//   - io_context::run() 在独立工作线程中执行
//   - 所有 socket 操作在 io_context 线程中序列化
//   - 业务线程通过 promise/future 同步等待结果
//
// 日志约定 (Logger 框架):
//   格式: [HH:MM:SS.mmm][LEVEL][HAL][ModbusTCP|host:port][N/A][N/A][N/A] msg
//   通过 m_moduleName 区分多实例 (e.g., "ModbusTCP|192.168.1.88:502")

#include "infrastructure/plc/protocol/AsioModbusTcpClient.h"
#include "infrastructure/plc/protocol/ModbusTcpFrame.h"
#include "infrastructure/logger/Logger.h"

#include <future>
#include <iomanip>
#include <sstream>
#include <system_error>

// ═══════════════════════════════════════════════════════
//  Windows 宏污染修复
//  <windows.h> (被 Asio 传递引入) 定义了 ERROR/DEBUG 等宏，
//  会破坏 Logger 框架的 LogLevel::ERROR 等枚举值。
//  在所有 #include 后取消这些宏定义。
// ═══════════════════════════════════════════════════════
#ifdef ERROR
#undef ERROR
#endif
#ifdef DEBUG
#undef DEBUG
#endif
#ifdef TRACE
#undef TRACE
#endif

namespace plc::protocol {

namespace tcp_frame = plc::protocol::tcp;

// ========================================================================
//  构造 / 析构
// ========================================================================

AsioModbusTcpClient::AsioModbusTcpClient(const Config& config)
    : m_config(config)
    , m_ioctx()
    , m_socket(m_ioctx)
    , m_timer(m_ioctx)
    , m_moduleName("ModbusTCP|" + config.host + ":" + std::to_string(config.port))
{
    std::ostringstream oss;
    oss << "AsioModbusTcpClient constructed"
        << " (unitId=" << static_cast<int>(config.unitId)
        << ", timeout=" << config.timeoutMs << "ms"
        << ", reconnect=" << config.reconnectIntervalMs << "ms)";
    LOG_INFO(LogLayer::HAL, m_moduleName, oss.str());
}

AsioModbusTcpClient::~AsioModbusTcpClient() {
    LOG_DEBUG(LogLayer::HAL, m_moduleName, "destructor -- calling stop()");
    stop();
    LOG_DEBUG(LogLayer::HAL, m_moduleName, "destructor -- done");
}

// ========================================================================
//  连接管理
// ========================================================================

void AsioModbusTcpClient::start() {
    if (m_running.load(std::memory_order_acquire)) {
        LOG_DEBUG(LogLayer::HAL, m_moduleName, "start() called but already running -- no-op");
        return;
    }

    LOG_INFO(LogLayer::HAL, m_moduleName,
        "start() -- restarting io_context and creating worker thread");

    m_running.store(true, std::memory_order_release);

    // 如果 io_context 之前被 stop() 过，需要 restart 才能再次 run()
    m_ioctx.restart();

    // 创建 work guard，防止 io_context 在空闲时退出
    // 必须在线程启动前创建，否则 run() 可能立即返回
    if (!m_workGuard) {
        m_workGuard = std::make_unique<asio::io_context::work>(m_ioctx);
    }

    // 创建工作线程并启动 io_context
    m_worker = std::thread([this]() {
        {
            std::ostringstream oss;
            oss << "thread started (tid=" << std::this_thread::get_id() << ")";
            LOG_DEBUG(LogLayer::HAL, m_moduleName, oss.str());
        }

        // 发起首次连接
        startReconnect();

        // 进入事件循环 -- work_guard 保证不会提前退出
        m_ioctx.run();

        {
            std::ostringstream oss;
            oss << "run() exited (tid=" << std::this_thread::get_id() << ")";
            LOG_DEBUG(LogLayer::HAL, m_moduleName, oss.str());
        }
    });
}

void AsioModbusTcpClient::stop() {
    if (!m_running.load(std::memory_order_acquire)) {
        LOG_DEBUG(LogLayer::HAL, m_moduleName, "stop() called but already stopped -- no-op");
        return;
    }

    LOG_INFO(LogLayer::HAL, m_moduleName, "stop() -- initiating shutdown sequence");

    // 标记停止，阻止新事务
    m_running.store(false, std::memory_order_release);
    m_connected.store(false, std::memory_order_release);

    // 在 io_context 线程中安全关闭 socket 和 timer
    // 使用 post 而非 dispatch，即使当前在 io 线程也排队到下一帧
    asio::post(m_ioctx, [this]() {
        std::error_code ec;
        LOG_DEBUG(LogLayer::HAL, m_moduleName,
            "stop() -- cancelling timer and closing socket on io thread");

        // 先取消 timer（重连/超时回调）
        m_timer.cancel(ec);
        if (ec) {
            std::ostringstream oss;
            oss << "stop() -- timer.cancel() warning: " << ec.message();
            LOG_WARN(LogLayer::HAL, m_moduleName, oss.str());
        }

        // 再取消并关闭 socket
        m_socket.cancel(ec);
        if (ec) {
            std::ostringstream oss;
            oss << "stop() -- socket.cancel() warning: " << ec.message();
            LOG_WARN(LogLayer::HAL, m_moduleName, oss.str());
        }
        m_socket.close(ec);
        if (ec) {
            std::ostringstream oss;
            oss << "stop() -- socket.close() warning: " << ec.message();
            LOG_WARN(LogLayer::HAL, m_moduleName, oss.str());
        }
    });

    // 销毁 work guard，让 io_context 自然 drain
    m_workGuard.reset();
    LOG_DEBUG(LogLayer::HAL, m_moduleName,
        "stop() -- work_guard reset, io_context will drain naturally");

    // 等待工作线程退出
    if (m_worker.joinable()) {
        LOG_DEBUG(LogLayer::HAL, m_moduleName, "stop() -- joining worker thread...");
        m_worker.join();
        LOG_DEBUG(LogLayer::HAL, m_moduleName, "stop() -- worker thread joined");
    }

    LOG_INFO(LogLayer::HAL, m_moduleName, "stop() -- shutdown sequence complete");
}

bool AsioModbusTcpClient::isConnected() const {
    return m_connected.load(std::memory_order_acquire);
}

// ========================================================================
//  IModbusClient -- 读通道
// ========================================================================

CommunicationResult AsioModbusTcpClient::readCoils(
    uint16_t startAddress, uint16_t count,
    std::vector<uint8_t>& payload)
{
    if (!m_running.load(std::memory_order_acquire)) {
        return CommunicationResult{
            CommunicationResult::Status::Disconnected,
            0,
            "Client not started"
        };
    }

    uint16_t tid = nextTransactionId();
    auto frame = tcp_frame::ModbusTcpFrame::buildReadCoils(tid, m_config.unitId, startAddress, count);

    std::vector<uint8_t> response;
    CommunicationResult result = executeTransaction(frame, response);
    if (!result.ok()) {
        return result;
    }

    // 提取线圈数据
    payload = tcp_frame::ModbusTcpFrame::parseCoilResponse(response);
    if (payload.empty() && count > 0) {
        return CommunicationResult{
            CommunicationResult::Status::InvalidResponse,
            0,
            "parseCoilResponse returned empty data"
        };
    }
    return result;
}

CommunicationResult AsioModbusTcpClient::readHoldingRegisters(
    uint16_t startAddress, uint16_t count,
    std::vector<uint16_t>& payload)
{
    if (!m_running.load(std::memory_order_acquire)) {
        return CommunicationResult{
            CommunicationResult::Status::Disconnected,
            0,
            "Client not started"
        };
    }

    uint16_t tid = nextTransactionId();
    auto frame = tcp_frame::ModbusTcpFrame::buildReadHoldingRegisters(tid, m_config.unitId, startAddress, count);

    std::vector<uint8_t> response;
    CommunicationResult result = executeTransaction(frame, response);
    if (!result.ok()) {
        return result;
    }

    // 提取寄存器数据
    payload = tcp_frame::ModbusTcpFrame::parseRegisterResponse(response);
    if (payload.empty() && count > 0) {
        return CommunicationResult{
            CommunicationResult::Status::InvalidResponse,
            0,
            "parseRegisterResponse returned empty data"
        };
    }
    return result;
}

// ========================================================================
//  IModbusClient -- 写通道
// ========================================================================

CommunicationResult AsioModbusTcpClient::writeSingleCoil(
    uint16_t address, bool value)
{
    if (!m_running.load(std::memory_order_acquire)) {
        return CommunicationResult{
            CommunicationResult::Status::Disconnected,
            0,
            "Client not started"
        };
    }

    uint16_t tid = nextTransactionId();
    auto frame = tcp_frame::ModbusTcpFrame::buildWriteSingleCoil(tid, m_config.unitId, address, value);

    std::vector<uint8_t> response;
    return executeTransaction(frame, response);
}

CommunicationResult AsioModbusTcpClient::writeSingleRegister(
    uint16_t address, uint16_t value)
{
    if (!m_running.load(std::memory_order_acquire)) {
        return CommunicationResult{
            CommunicationResult::Status::Disconnected,
            0,
            "Client not started"
        };
    }

    uint16_t tid = nextTransactionId();
    auto frame = tcp_frame::ModbusTcpFrame::buildWriteSingleRegister(tid, m_config.unitId, address, value);

    std::vector<uint8_t> response;
    return executeTransaction(frame, response);
}

CommunicationResult AsioModbusTcpClient::writeMultipleRegisters(
    uint16_t startAddress,
    const std::vector<uint16_t>& values)
{
    if (!m_running.load(std::memory_order_acquire)) {
        return CommunicationResult{
            CommunicationResult::Status::Disconnected,
            0,
            "Client not started"
        };
    }

    uint16_t tid = nextTransactionId();
    auto frame = tcp_frame::ModbusTcpFrame::buildWriteMultipleRegisters(tid, m_config.unitId, startAddress, values);

    std::vector<uint8_t> response;
    return executeTransaction(frame, response);
}

// ========================================================================
//  内部实现 -- 事务执行
// ========================================================================

CommunicationResult AsioModbusTcpClient::executeTransaction(
    const std::vector<uint8_t>& frame,
    std::vector<uint8_t>& response)
{
    // 快速失败路径：未连接 -- 使用时间节流防止日志风暴
    if (!m_connected.load(std::memory_order_acquire)) {
        LOG_WARN_EVERY_MS(1000, LogLayer::HAL, m_moduleName,
            "executeTransaction rejected -- not connected");
        return CommunicationResult{
            CommunicationResult::Status::Disconnected,
            0,
            "Not connected to " + m_config.host + ":" + std::to_string(m_config.port)
        };
    }

    uint16_t tid = (static_cast<uint16_t>(frame[0]) << 8) | frame[1];
    uint8_t fc = frame[7];
    {
        std::ostringstream oss;
        oss << "executeTransaction -- posting to io_context (TID=" << tid
            << ", FC=" << static_cast<int>(fc) << ")";
        LOG_TRACE(LogLayer::HAL, m_moduleName, oss.str());
    }

    // 使用 promise/future 将异步 I/O 桥接到同步调用
    std::promise<CommunicationResult> promise;
    auto future = promise.get_future();

    // post 到 io_context 线程执行
    // 需要捕获 m_moduleName 的副本，因为 lambda 跨线程执行时 this 可能已被析构
    std::string moduleName = m_moduleName;
    asio::post(m_ioctx, [this, moduleName, &frame, &response, p = std::move(promise), timeout = m_config.timeoutMs]() mutable {
        uint16_t tid = (static_cast<uint16_t>(frame[0]) << 8) | frame[1];
        uint8_t fcode = frame[7];
        {
            std::ostringstream oss;
            oss << "lambda executing on io thread (TID=" << tid
                << ", TID_raw=" << std::this_thread::get_id() << ")";
            LOG_TRACE(LogLayer::HAL, moduleName, oss.str());
        }

        // ═══════════════════════════════════════════════════
        //  Socket 级超时机制 (Phase 2.3)
        // ═══════════════════════════════════════════════════
        std::error_code ec;
        size_t readBytes = 0;
        bool timedOut = false;

        // 启动超时定时器 -- 在 io_context 线程内设置
        m_timer.expires_after(std::chrono::milliseconds(timeout));
        m_timer.async_wait([this, moduleName, tid, &timedOut](std::error_code timerEc) {
            if (timerEc) {
                // timer 被取消（I/O 在超时前完成）
                return;
            }
            // 超时触发 -- 取消所有 socket 操作
            timedOut = true;
            {
                std::ostringstream oss;
                oss << "socket timer fired (TID=" << tid << ") -- calling socket.cancel()";
                LOG_WARN(LogLayer::HAL, moduleName, oss.str());
            }
            std::error_code cancelEc;
            m_socket.cancel(cancelEc);
            if (cancelEc) {
                std::ostringstream oss2;
                oss2 << "socket.cancel() warning: " << cancelEc.message();
                LOG_WARN(LogLayer::HAL, moduleName, oss2.str());
            }
        });

        // 0 -- 打印请求帧十六进制
        {
            std::ostringstream oss;
            oss << "Request hex  (TID=" << tid << ", FC=" << static_cast<int>(fcode) << "): ";
            for (size_t i = 0; i < frame.size(); ++i) {
                oss << std::hex << std::uppercase << std::setfill('0') << std::setw(2)
                    << static_cast<int>(frame[i]) << " ";
            }
            LOG_TRACE(LogLayer::HAL, moduleName, oss.str());
        }

        // 1 -- 写入请求帧
        readBytes = asio::write(m_socket, asio::buffer(frame), ec);
        if (ec) {
            m_timer.cancel();
            m_timer.expires_at(std::chrono::steady_clock::time_point::max());

            if (timedOut || ec == asio::error::operation_aborted) {
                m_connected.store(false, std::memory_order_release);
                {
                    std::ostringstream oss;
                    oss << "write aborted by timeout (TID=" << tid << ")";
                    LOG_WARN(LogLayer::HAL, moduleName, oss.str());
                }
                p.set_value(CommunicationResult{
                    CommunicationResult::Status::Timeout,
                    0,
                    "Socket write timed out after " + std::to_string(timeout) + "ms"
                });
            } else {
                m_connected.store(false, std::memory_order_release);
                {
                    std::ostringstream oss;
                    oss << "write failed (TID=" << tid << "): " << ec.message();
                    LOG_ERROR(LogLayer::HAL, moduleName, oss.str());
                }
                p.set_value(CommunicationResult{
                    CommunicationResult::Status::NetworkError,
                    0,
                    "Write failed: " + ec.message()
                });
            }
            return;
        }
        {
            std::ostringstream oss;
            oss << "write OK (TID=" << tid << ", " << readBytes << " bytes)";
            LOG_TRACE(LogLayer::HAL, moduleName, oss.str());
        }

        // 2 -- 两步分帧读取：先读 7 字节 MBAP 头
        std::vector<uint8_t> mbap(7);
        {
            std::ostringstream oss;
            oss << "reading MBAP header (TID=" << tid << ")...";
            LOG_TRACE(LogLayer::HAL, moduleName, oss.str());
        }
        readBytes = asio::read(m_socket, asio::buffer(mbap), ec);
        if (ec) {
            m_timer.cancel();
            m_timer.expires_at(std::chrono::steady_clock::time_point::max());

            if (timedOut || ec == asio::error::operation_aborted) {
                m_connected.store(false, std::memory_order_release);
                {
                    std::ostringstream oss;
                    oss << "read MBAP aborted by timeout (TID=" << tid << ")";
                    LOG_WARN(LogLayer::HAL, moduleName, oss.str());
                }
                p.set_value(CommunicationResult{
                    CommunicationResult::Status::Timeout,
                    0,
                    "Socket read timed out after " + std::to_string(timeout) + "ms"
                });
            } else {
                m_connected.store(false, std::memory_order_release);
                {
                    std::ostringstream oss;
                    oss << "read MBAP failed (TID=" << tid << "): " << ec.message();
                    LOG_ERROR(LogLayer::HAL, moduleName, oss.str());
                }
                p.set_value(CommunicationResult{
                    CommunicationResult::Status::NetworkError,
                    0,
                    "Read MBAP failed: " + ec.message()
                });
            }
            return;
        }
        {
            std::ostringstream oss;
            oss << "MBAP header received (TID=" << tid << ", " << readBytes << " bytes)";
            LOG_TRACE(LogLayer::HAL, moduleName, oss.str());
        }

        // 3 -- 解析 MBAP 头获取 PDU 长度
        auto mbapHdr = tcp_frame::ModbusTcpFrame::parseMbap(mbap);
        if (!mbapHdr.has_value()) {
            m_timer.cancel();
            m_timer.expires_at(std::chrono::steady_clock::time_point::max());
            {
                std::ostringstream oss;
                oss << "invalid MBAP header (TID=" << tid << ")";
                LOG_ERROR(LogLayer::HAL, moduleName, oss.str());
            }
            p.set_value(CommunicationResult{
                CommunicationResult::Status::InvalidResponse,
                0,
                "Invalid MBAP header"
            });
            return;
        }
        {
            std::ostringstream oss;
            oss << "MBAP parsed -- respTID=" << mbapHdr->transactionId
                << ", length=" << mbapHdr->length
                << " (PDU=" << (mbapHdr->length - 1) << " bytes)";
            LOG_TRACE(LogLayer::HAL, moduleName, oss.str());
        }

        // 4 -- 读取 PDU 数据
        size_t pduLen = mbapHdr->length - 1;

        if (pduLen == 0) {
            m_timer.cancel();
            m_timer.expires_at(std::chrono::steady_clock::time_point::max());
            {
                std::ostringstream oss;
                oss << "PDU length is zero (TID=" << tid << ")";
                LOG_WARN(LogLayer::HAL, moduleName, oss.str());
            }
            p.set_value(CommunicationResult{
                CommunicationResult::Status::InvalidResponse,
                0,
                "PDU length is zero"
            });
            return;
        }

        std::vector<uint8_t> pdu(pduLen);
        {
            std::ostringstream oss;
            oss << "reading PDU (TID=" << tid << ", " << pduLen << " bytes)...";
            LOG_TRACE(LogLayer::HAL, moduleName, oss.str());
        }
        readBytes = asio::read(m_socket, asio::buffer(pdu), ec);
        if (ec) {
            m_timer.cancel();
            m_timer.expires_at(std::chrono::steady_clock::time_point::max());

            if (timedOut || ec == asio::error::operation_aborted) {
                m_connected.store(false, std::memory_order_release);
                {
                    std::ostringstream oss;
                    oss << "read PDU aborted by timeout (TID=" << tid << ")";
                    LOG_WARN(LogLayer::HAL, moduleName, oss.str());
                }
                p.set_value(CommunicationResult{
                    CommunicationResult::Status::Timeout,
                    0,
                    "Socket read timed out after " + std::to_string(timeout) + "ms"
                });
            } else {
                m_connected.store(false, std::memory_order_release);
                {
                    std::ostringstream oss;
                    oss << "read PDU failed (TID=" << tid << "): " << ec.message();
                    LOG_ERROR(LogLayer::HAL, moduleName, oss.str());
                }
                p.set_value(CommunicationResult{
                    CommunicationResult::Status::NetworkError,
                    0,
                    "Read PDU failed: " + ec.message()
                });
            }
            return;
        }
        {
            std::ostringstream oss;
            oss << "PDU received (TID=" << tid << ", " << readBytes << " bytes)";
            LOG_TRACE(LogLayer::HAL, moduleName, oss.str());
        }

        // I/O 全部完成，取消超时定时器
        m_timer.cancel();
        m_timer.expires_at(std::chrono::steady_clock::time_point::max());

        // 5 -- 组装完整响应帧
        response.clear();
        response.reserve(7 + pduLen);
        response.insert(response.end(), mbap.begin(), mbap.end());
        response.insert(response.end(), pdu.begin(), pdu.end());

        // 打印响应帧十六进制
        {
            std::ostringstream oss;
            oss << "Response hex (TID=" << tid << "): ";
            for (size_t i = 0; i < response.size(); ++i) {
                oss << std::hex << std::uppercase << std::setfill('0') << std::setw(2)
                    << static_cast<int>(response[i]) << " ";
            }
            LOG_TRACE(LogLayer::HAL, moduleName, oss.str());
        }

        // 6 -- 检查事务 ID 匹配
        uint16_t reqTid = (static_cast<uint16_t>(frame[0]) << 8) | frame[1];
        uint16_t respTid = mbapHdr->transactionId;

        if (reqTid != respTid) {
            {
                std::ostringstream oss;
                oss << "TID mismatch (TID=" << tid
                    << "): req=" << reqTid << " resp=" << respTid;
                LOG_WARN(LogLayer::HAL, moduleName, oss.str());
            }
            p.set_value(CommunicationResult{
                CommunicationResult::Status::InvalidResponse,
                0,
                "Transaction ID mismatch: req=" + std::to_string(reqTid) +
                " resp=" + std::to_string(respTid)
            });
            return;
        }

        // 7 -- 检查 Modbus 异常
        uint8_t responseFc = pdu[0];
        uint8_t requestFc = frame[7];
        std::vector<uint8_t> pduData(pdu.begin() + 1, pdu.end());

        int exCode = tcp_frame::ModbusTcpFrame::checkException(requestFc, responseFc, pduData);
        if (exCode != 0) {
            if (exCode == 0x06) {
                {
                    std::ostringstream oss;
                    oss << "Modbus Exception: Device Busy (TID=" << tid << ")";
                    LOG_WARN(LogLayer::HAL, moduleName, oss.str());
                }
                p.set_value(CommunicationResult{
                    CommunicationResult::Status::Busy,
                    exCode,
                    "PLC device busy"
                });
            } else {
                {
                    std::ostringstream oss;
                    oss << "Modbus Exception code: " << exCode << " (TID=" << tid << ")";
                    LOG_WARN(LogLayer::HAL, moduleName, oss.str());
                }
                p.set_value(CommunicationResult{
                    CommunicationResult::Status::ProtocolError,
                    exCode,
                    "Modbus exception code: " + std::to_string(exCode)
                });
            }
            return;
        }

        // 成功
        {
            std::ostringstream oss;
            oss << "transaction succeeded (TID=" << tid << ")";
            LOG_TRACE(LogLayer::HAL, moduleName, oss.str());
        }
        p.set_value(CommunicationResult{
            CommunicationResult::Status::Sent,
            0,
            ""
        });
    });

    // 同步等待结果（带超时）
    auto status = future.wait_for(std::chrono::milliseconds(m_config.timeoutMs + 500));
    if (status == std::future_status::timeout) {
        {
            std::ostringstream oss;
            oss << "future timeout -- posting socket close (TID=" << tid << ")";
            LOG_WARN(LogLayer::HAL, m_moduleName, oss.str());
        }
        asio::post(m_ioctx, [this]() {
            std::error_code ec;
            m_socket.cancel(ec);
            m_socket.close(ec);
            m_connected.store(false, std::memory_order_release);
        });
        return CommunicationResult{
            CommunicationResult::Status::Timeout,
            0,
            "Transaction timed out after " + std::to_string(m_config.timeoutMs + 500) + "ms (future-level)"
        };
    }

    return future.get();
}

uint16_t AsioModbusTcpClient::nextTransactionId() {
    return m_transactionId.fetch_add(1, std::memory_order_relaxed);
}

void AsioModbusTcpClient::configureSocket() {
    if (!m_socket.is_open()) {
        LOG_DEBUG(LogLayer::HAL, m_moduleName, "configureSocket() skipped -- socket not open");
        return;
    }

    std::error_code ec;

    // TCP_NODELAY
    m_socket.set_option(asio::ip::tcp::no_delay(true), ec);
    if (ec) {
        std::ostringstream oss;
        oss << "no_delay set failed: " << ec.message();
        LOG_WARN(LogLayer::HAL, m_moduleName, oss.str());
    }

    // SO_KEEPALIVE
    m_socket.set_option(asio::socket_base::keep_alive(true), ec);
    if (ec) {
        std::ostringstream oss;
        oss << "keep_alive set failed: " << ec.message();
        LOG_WARN(LogLayer::HAL, m_moduleName, oss.str());
    }

    LOG_DEBUG(LogLayer::HAL, m_moduleName,
        "socket configured (no_delay=true, keep_alive=true)");
}

// ========================================================================
//  连接与重连
// ========================================================================

void AsioModbusTcpClient::startReconnect() {
    if (!m_running.load(std::memory_order_acquire)) {
        LOG_DEBUG(LogLayer::HAL, m_moduleName,
            "startReconnect() skipped -- not running");
        return;
    }

    {
        std::ostringstream oss;
        oss << "startReconnect() -- resolving " << m_config.host << ":" << m_config.port;
        LOG_DEBUG(LogLayer::HAL, m_moduleName, oss.str());
    }

    auto resolver = std::make_shared<asio::ip::tcp::resolver>(m_ioctx);
    std::string portStr = std::to_string(m_config.port);

    // 捕获 moduleName 副本供异步回调使用
    std::string moduleName = m_moduleName;
    resolver->async_resolve(
        m_config.host, portStr,
        [this, moduleName, resolver](std::error_code ec, asio::ip::tcp::resolver::results_type endpoints) {
            if (ec) {
                std::ostringstream oss;
                oss << "DNS resolve failed: " << ec.message();
                LOG_ERROR(LogLayer::HAL, moduleName, oss.str());
                scheduleReconnect();
                return;
            }

            LOG_DEBUG(LogLayer::HAL, moduleName,
                "DNS resolved -- starting async_connect");

            asio::async_connect(
                m_socket, endpoints,
                [this, moduleName](std::error_code ec, asio::ip::tcp::endpoint ep) {
                    {
                        std::ostringstream oss;
                        oss << "async_connect callback fired (ec=" << ec.value() << ")";
                        LOG_DEBUG(LogLayer::HAL, moduleName, oss.str());
                    }

                    if (!ec) {
                        configureSocket();
                        m_connected.store(true, std::memory_order_release);
                        {
                            std::ostringstream oss;
                            oss << "Successfully connected to "
                                << ep.address().to_string() << ":" << ep.port();
                            LOG_INFO(LogLayer::HAL, moduleName, oss.str());
                        }
                        return;
                    }

                    {
                        std::ostringstream oss;
                        oss << "Connect failed: " << ec.message();
                        LOG_ERROR(LogLayer::HAL, moduleName, oss.str());
                    }
                    std::error_code ignored;
                    m_socket.close(ignored);
                    scheduleReconnect();
                });
        });
}

void AsioModbusTcpClient::scheduleReconnect() {
    if (!m_running.load(std::memory_order_acquire)) {
        LOG_DEBUG(LogLayer::HAL, m_moduleName,
            "scheduleReconnect() skipped -- not running");
        return;
    }

    {
        std::ostringstream oss;
        oss << "scheduling reconnect in " << m_config.reconnectIntervalMs << "ms";
        LOG_DEBUG(LogLayer::HAL, m_moduleName, oss.str());
    }

    std::string moduleName = m_moduleName;
    m_timer.expires_from_now(
        std::chrono::milliseconds(m_config.reconnectIntervalMs));
    m_timer.async_wait([this, moduleName](std::error_code ec) {
        if (ec) {
            std::ostringstream oss;
            oss << "reconnect timer cancelled: " << ec.message();
            LOG_DEBUG(LogLayer::HAL, moduleName, oss.str());
            return;
        }
        if (!m_running.load(std::memory_order_acquire)) {
            LOG_DEBUG(LogLayer::HAL, moduleName,
                "reconnect timer fired but not running -- ignored");
            return;
        }
        LOG_DEBUG(LogLayer::HAL, moduleName,
            "reconnect timer fired -- calling startReconnect()");
        startReconnect();
    });
}

void AsioModbusTcpClient::cleanup() {
    LOG_DEBUG(LogLayer::HAL, m_moduleName, "cleanup() -- delegating to stop()");
    stop();
}

} // namespace plc::protocol
