// infrastructure/plc/protocol/AsioModbusTcpClient.cpp
// P4 Phase 2 — AsioModbusTcpClient 基础实现（连接管理）

#include "infrastructure/plc/protocol/AsioModbusTcpClient.h"
#include "infrastructure/plc/protocol/ModbusTcpFrame.h"

#include <future>
#include <system_error>

namespace plc::protocol {

namespace tcp_frame = plc::protocol::tcp;

// ═══════════════════════════════════════════════════════════
//  构造 / 析构
// ═══════════════════════════════════════════════════════════

AsioModbusTcpClient::AsioModbusTcpClient(const Config& config)
    : m_config(config)
    , m_ioctx()
    , m_socket(m_ioctx)
    , m_timer(m_ioctx)
{
}

AsioModbusTcpClient::~AsioModbusTcpClient() {
    stop();
}

// ═══════════════════════════════════════════════════════════
//  连接管理
// ═══════════════════════════════════════════════════════════

void AsioModbusTcpClient::start() {
    if (m_running.load(std::memory_order_acquire)) {
        return; // 已启动，幂等
    }

    m_running.store(true, std::memory_order_release);

    // 创建工作线程并启动 io_context
    m_worker = std::thread([this]() {
        // 发起首次连接
        startReconnect();
        m_ioctx.run();
    });
}

void AsioModbusTcpClient::stop() {
    if (!m_running.load(std::memory_order_acquire)) {
        return; // 已停止，幂等
    }

    // 标记停止，避免新操作
    m_running.store(false, std::memory_order_release);
    m_connected.store(false, std::memory_order_release);

    // 取消所有待处理的异步操作并停止 io_context
    std::error_code ec;
    m_timer.cancel(ec);
    m_socket.close(ec);
    m_ioctx.stop();

    // 等待工作线程退出
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

bool AsioModbusTcpClient::isConnected() const {
    return m_connected.load(std::memory_order_acquire);
}

// ═══════════════════════════════════════════════════════════
//  IModbusClient — 读通道
// ═══════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════
//  IModbusClient — 写通道
// ═══════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════
//  内部实现
// ═══════════════════════════════════════════════════════════

CommunicationResult AsioModbusTcpClient::executeTransaction(
    const std::vector<uint8_t>& frame,
    std::vector<uint8_t>& response)
{
    // 确保当前已连接
    if (!m_connected.load(std::memory_order_acquire)) {
        // 如果 io_context 不再运行，标记为 Disconnected
        if (m_ioctx.stopped()) {
            return CommunicationResult{
                CommunicationResult::Status::Disconnected,
                0,
                "io_context stopped"
            };
        }
        return CommunicationResult{
            CommunicationResult::Status::Disconnected,
            0,
            "Not connected to " + m_config.host + ":" + std::to_string(m_config.port)
        };
    }

    // 使用 promise/future 将异步 I/O 桥接到同步调用
    std::promise<CommunicationResult> promise;
    auto future = promise.get_future();

    // post 到 io_context 线程执行
    asio::post(m_ioctx, [this, &frame, &response, p = std::move(promise)]() mutable {
        // 使用 strand 确保操作串行化（避免帧交织）
        std::error_code ec;
        size_t written = 0;

        // 1️⃣ 写入请求帧
        asio::write(m_socket, asio::buffer(frame), ec);
        if (ec) {
            m_connected.store(false, std::memory_order_release);
            p.set_value(CommunicationResult{
                CommunicationResult::Status::NetworkError,
                0,
                "Write failed: " + ec.message()
            });
            return;
        }

        // 2️⃣ 两步分帧读取：先读 7 字节 MBAP 头
        std::vector<uint8_t> mbap(7);
        size_t readBytes = asio::read(m_socket, asio::buffer(mbap), ec);
        if (ec) {
            m_connected.store(false, std::memory_order_release);
            p.set_value(CommunicationResult{
                CommunicationResult::Status::NetworkError,
                0,
                "Read MBAP failed: " + ec.message()
            });
            return;
        }

        // 3️⃣ 解析 MBAP 头获取 PDU 长度
        auto mbapHdr = tcp_frame::ModbusTcpFrame::parseMbap(mbap);
        if (!mbapHdr.has_value()) {
            p.set_value(CommunicationResult{
                CommunicationResult::Status::InvalidResponse,
                0,
                "Invalid MBAP header"
            });
            return;
        }

        // 4️⃣ 读取 PDU 数据
        // MBAP Length = UnitID(1) + FC + Data
        // 但 UnitID 已经在前面的 7 字节 MBAP 中读取
        size_t pduLen = mbapHdr->length - 1;
            
        if (pduLen == 0) {
            p.set_value(CommunicationResult{
                CommunicationResult::Status::InvalidResponse,
                0,
                "PDU length is zero"
            });
            return;
        }
        
        std::vector<uint8_t> pdu(pduLen);
        
        readBytes = asio::read(
            m_socket,
            asio::buffer(pdu),
            ec
        );
        if (ec) {
            m_connected.store(false, std::memory_order_release);
            p.set_value(CommunicationResult{
                CommunicationResult::Status::NetworkError,
                0,
                "Read PDU failed: " + ec.message()
            });
            return;
        }

        // 5️⃣ 组装完整响应帧
        response.clear();
        response.reserve(7 + pduLen);
        response.insert(response.end(), mbap.begin(), mbap.end());
        response.insert(response.end(), pdu.begin(), pdu.end());

        std::cout << "Response hex: ";
        for(auto b : response) printf("%02X ", b);
        std::cout << std::endl;

        // 6️⃣ 检查异常响应
        uint8_t responseFc = pdu[0]; // Unit ID 在 mbap[6]，FC 是 PDU 第一字节
        uint16_t reqTid = (static_cast<uint16_t>(frame[0]) << 8) | frame[1];
        uint16_t respTid = mbapHdr->transactionId;

        if (reqTid != respTid) {
            p.set_value(CommunicationResult{
                CommunicationResult::Status::InvalidResponse,
                0,
                "Transaction ID mismatch: req=" + std::to_string(reqTid) +
                " resp=" + std::to_string(respTid)
            });
            return;
        }

        uint8_t requestFc = frame[7];                      // PDU FC = frame byte 7
        std::vector<uint8_t> pduData(pdu.begin() + 1, pdu.end()); // FC 之后的数据

        int exCode = tcp_frame::ModbusTcpFrame::checkException(requestFc, responseFc, pduData);
        if (exCode != 0) {
            if (exCode == 0x06) {
                p.set_value(CommunicationResult{
                    CommunicationResult::Status::Busy,
                    exCode,
                    "PLC device busy"
                });
            } else {
                p.set_value(CommunicationResult{
                    CommunicationResult::Status::ProtocolError,
                    exCode,
                    "Modbus exception code: " + std::to_string(exCode)
                });
            }
            return;
        }

        // 成功
        p.set_value(CommunicationResult{
            CommunicationResult::Status::Sent,
            0,
            ""
        });
    });

    // 同步等待结果（带超时）
    // 注意: 超时处理已内建在 socket 超时设置中（configureSocket）
    // 此处 future::wait_for 作为双重保险
    auto status = future.wait_for(std::chrono::milliseconds(m_config.timeoutMs + 500));
    if (status == std::future_status::timeout) {
        // 超时：关闭 socket 中断 io 操作
        std::error_code ec;
        m_socket.close(ec);
        m_connected.store(false, std::memory_order_release);
        return CommunicationResult{
            CommunicationResult::Status::Timeout,
            0,
            "Transaction timed out after " + std::to_string(m_config.timeoutMs + 500) + "ms"
        };
    }

    return future.get();
}

uint16_t AsioModbusTcpClient::nextTransactionId() {
    // 自增回绕: [0, 65535] → 65535 之后回到 0
    return m_transactionId.fetch_add(1, std::memory_order_relaxed);
}

void AsioModbusTcpClient::configureSocket() {
    if (!m_socket.is_open()) return; // 安全检查

    // 设置 socket 选项（非阻塞模式由 Asio 异步操作处理）
    std::error_code ec;

    // TCP_NODELAY — 禁用 Nagle 算法，满足低延迟要求
    m_socket.set_option(asio::ip::tcp::no_delay(true), ec);

    // SO_KEEPALIVE — 长连接心跳保活
    m_socket.set_option(asio::socket_base::keep_alive(true), ec);
}

#include <iostream> // 记得在文件开头包含 iostream

void AsioModbusTcpClient::startReconnect() {
    // 1. 使用 shared_ptr，确保 resolver 活到回调执行完毕
    auto resolver = std::make_shared<asio::ip::tcp::resolver>(m_ioctx);
    std::string portStr = std::to_string(m_config.port);

    // 2. 将 resolver 捕获进 lambda 中 [this, resolver]
    resolver->async_resolve(
        m_config.host, portStr,
        [this, resolver](std::error_code ec, asio::ip::tcp::resolver::results_type endpoints) {
            if (ec) {
                std::cout << "[Modbus] DNS Resolve failed: " << ec.message() << std::endl;
                scheduleReconnect();
                return;
            }
            
            asio::async_connect(
                m_socket, endpoints,
                [this](std::error_code ec, asio::ip::tcp::endpoint ep) {
                    if (!ec) {
                        // 3. 连接成功后，socket 才真正打开，此时再配置参数！
                        configureSocket(); 
                        m_connected.store(true, std::memory_order_release);
                        std::cout << "[Modbus] 🟢 Successfully connected to " << ep << std::endl;
                        return;
                    }
                    std::cout << "[Modbus] 🔴 Connect failed: " << ec.message() << std::endl;
                    std::error_code ignored;
                    m_socket.close(ignored);
                    scheduleReconnect();
                });
        });
}

void AsioModbusTcpClient::scheduleReconnect() {
    if (!m_running.load(std::memory_order_acquire)) {
        return;
    }
    m_timer.expires_from_now(
        std::chrono::milliseconds(m_config.reconnectIntervalMs));
    m_timer.async_wait([this](std::error_code ec) {
        if (ec) return; // 被取消
        startReconnect();
    });
}

void AsioModbusTcpClient::cleanup() {
    stop();
}

} // namespace plc::protocol
