// infrastructure/plc/protocol/AsioModbusTcpClient.cpp
// P4 Phase 2 — AsioModbusTcpClient 工业级实现
//
// 架构演进:
//   Phase 2.0 — 基础同步 I/O + promise/future 桥接
//   Phase 2.1 — executor_work_guard 防止 io_context 提前退出
//   Phase 2.2 — 工业级 stop 序列 + 线程生命周期日志
//   Phase 2.3 — socket 级超时 (expires_after) + 全链路可观测性
//
// 线程模型:
//   - io_context::run() 在独立工作线程中执行
//   - 所有 socket 操作在 io_context 线程中序列化
//   - 业务线程通过 promise/future 同步等待结果
//
// 日志约定:
//   [IO]       — 线程生命周期
//   [CONNECT]  — 连接/重连
//   [TX]       — 事务执行
//   [MODBUS]   — 帧级协议交互

#include "infrastructure/plc/protocol/AsioModbusTcpClient.h"
#include "infrastructure/plc/protocol/ModbusTcpFrame.h"

#include <future>
#include <iostream>
#include <system_error>

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
{
    std::cout << "[IO] AsioModbusTcpClient constructed ("
              << config.host << ":" << config.port
              << ", unitId=" << static_cast<int>(config.unitId)
              << ", timeout=" << config.timeoutMs << "ms"
              << ", reconnect=" << config.reconnectIntervalMs << "ms)"
              << std::endl;
}

AsioModbusTcpClient::~AsioModbusTcpClient() {
    std::cout << "[IO] AsioModbusTcpClient destructor — calling stop()" << std::endl;
    stop();
    std::cout << "[IO] AsioModbusTcpClient destructor — done" << std::endl;
}

// ========================================================================
//  连接管理
// ========================================================================

void AsioModbusTcpClient::start() {
    if (m_running.load(std::memory_order_acquire)) {
        std::cout << "[IO] start() called but already running — no-op" << std::endl;
        return;
    }

    std::cout << "[IO] start() — restarting io_context and creating worker thread" << std::endl;

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
        std::cout << "[IO] thread started (tid="
                  << std::this_thread::get_id() << ")" << std::endl;

        // 发起首次连接
        startReconnect();

        // 进入事件循环 — work_guard 保证不会提前退出
        m_ioctx.run();

        std::cout << "[IO] run() exited (tid="
                  << std::this_thread::get_id() << ")" << std::endl;
    });
}

void AsioModbusTcpClient::stop() {
    if (!m_running.load(std::memory_order_acquire)) {
        std::cout << "[IO] stop() called but already stopped — no-op" << std::endl;
        return;
    }

    std::cout << "[IO] stop() — initiating shutdown sequence" << std::endl;

    // 标记停止，阻止新事务
    m_running.store(false, std::memory_order_release);
    m_connected.store(false, std::memory_order_release);

    // 在 io_context 线程中安全关闭 socket 和 timer
    // 使用 post 而非 dispatch，即使当前在 io 线程也排队到下一帧
    asio::post(m_ioctx, [this]() {
        std::error_code ec;
        std::cout << "[IO] stop() — cancelling timer and closing socket on io thread" << std::endl;

        // 先取消 timer（重连/超时回调）
        m_timer.cancel(ec);
        if (ec) {
            std::cout << "[IO] stop() — timer.cancel() warning: " << ec.message() << std::endl;
        }

        // 再取消并关闭 socket
        m_socket.cancel(ec);
        if (ec) {
            std::cout << "[IO] stop() — socket.cancel() warning: " << ec.message() << std::endl;
        }
        m_socket.close(ec);
        if (ec) {
            std::cout << "[IO] stop() — socket.close() warning: " << ec.message() << std::endl;
        }
    });

    // 销毁 work guard，让 io_context 自然 drain
    // ⚠️ 千万不要调用 m_ioctx.stop()！
    //    那会暴力终止事件循环，导致未完成的 async_connect/async_resolve 回调被吞噬，
    //    既不触发 success 也不触发 failure，日志链断裂。
    //    正确做法：work_guard reset → socket.cancel（通过 post 排队）→ run() 自然返回。
    m_workGuard.reset();
    std::cout << "[IO] stop() — work_guard reset, io_context will drain naturally" << std::endl;

    // 等待工作线程退出
    if (m_worker.joinable()) {
        std::cout << "[IO] stop() — joining worker thread..." << std::endl;
        m_worker.join();
        std::cout << "[IO] stop() — worker thread joined" << std::endl;
    }

    std::cout << "[IO] stop() — shutdown sequence complete" << std::endl;
}

bool AsioModbusTcpClient::isConnected() const {
    return m_connected.load(std::memory_order_acquire);
}

// ========================================================================
//  IModbusClient — 读通道
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
//  IModbusClient — 写通道
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
//  内部实现 — 事务执行
// ========================================================================

CommunicationResult AsioModbusTcpClient::executeTransaction(
    const std::vector<uint8_t>& frame,
    std::vector<uint8_t>& response)
{
    // 快速失败路径：未连接
    if (!m_connected.load(std::memory_order_acquire)) {
        std::cout << "[TX] executeTransaction rejected — not connected" << std::endl;
        return CommunicationResult{
            CommunicationResult::Status::Disconnected,
            0,
            "Not connected to " + m_config.host + ":" + std::to_string(m_config.port)
        };
    }

    uint16_t tid = (static_cast<uint16_t>(frame[0]) << 8) | frame[1];
    uint8_t fc = frame[7];
    std::cout << "[TX] executeTransaction — posting to io_context (TID=" << tid
              << ", FC=" << static_cast<int>(fc) << ")" << std::endl;

    // 使用 promise/future 将异步 I/O 桥接到同步调用
    std::promise<CommunicationResult> promise;
    auto future = promise.get_future();

    // post 到 io_context 线程执行
    asio::post(m_ioctx, [this, &frame, &response, p = std::move(promise)]() mutable {
        uint16_t tid = (static_cast<uint16_t>(frame[0]) << 8) | frame[1];
        uint8_t fcode = frame[7];
        std::cout << "[TX] lambda executing on io thread (TID=" << tid << ")" << std::endl;

        std::error_code ec;
        size_t readBytes = 0;

        // 0️⃣ 打印请求帧十六进制（诊断 diagslave 无响应问题）
        std::cout << "[MODBUS] Request hex  (TID=" << tid << ", FC=" << static_cast<int>(fcode) << "): ";
        for (size_t i = 0; i < frame.size(); ++i) {
            printf("%02X ", frame[i]);
        }
        std::cout << std::endl;

        // 1️⃣ 写入请求帧
        readBytes = asio::write(m_socket, asio::buffer(frame), ec);
        if (ec) {
            m_connected.store(false, std::memory_order_release);
            std::cout << "[TX] write failed (TID=" << tid << "): " << ec.message() << std::endl;
            p.set_value(CommunicationResult{
                CommunicationResult::Status::NetworkError,
                0,
                "Write failed: " + ec.message()
            });
            return;
        }
        std::cout << "[TX] write OK (TID=" << tid << ", " << readBytes << " bytes)" << std::endl;

        // 2️⃣ 两步分帧读取：先读 7 字节 MBAP 头
        std::vector<uint8_t> mbap(7);
        std::cout << "[TX] reading MBAP header (TID=" << tid << ")..." << std::endl;
        readBytes = asio::read(m_socket, asio::buffer(mbap), ec);
        if (ec) {
            m_connected.store(false, std::memory_order_release);
            std::cout << "[TX] read MBAP failed (TID=" << tid << "): " << ec.message() << std::endl;
            p.set_value(CommunicationResult{
                CommunicationResult::Status::NetworkError,
                0,
                "Read MBAP failed: " + ec.message()
            });
            return;
        }
        std::cout << "[TX] MBAP header received (TID=" << tid << ", " << readBytes << " bytes)" << std::endl;

        // 3️⃣ 解析 MBAP 头获取 PDU 长度
        auto mbapHdr = tcp_frame::ModbusTcpFrame::parseMbap(mbap);
        if (!mbapHdr.has_value()) {
            std::cout << "[TX] invalid MBAP header (TID=" << tid << ")" << std::endl;
            p.set_value(CommunicationResult{
                CommunicationResult::Status::InvalidResponse,
                0,
                "Invalid MBAP header"
            });
            return;
        }
        std::cout << "[TX] MBAP parsed — respTID=" << mbapHdr->transactionId
                  << ", length=" << mbapHdr->length
                  << " (PDU=" << (mbapHdr->length - 1) << " bytes)" << std::endl;

        // 4️⃣ 读取 PDU 数据
        // MBAP Length = UnitID(1) + FC + Data
        // UnitID 已在前 7 字节 MBAP 中，PDU 包含 FC + Data
        size_t pduLen = mbapHdr->length - 1;

        if (pduLen == 0) {
            std::cout << "[TX] PDU length is zero (TID=" << tid << ")" << std::endl;
            p.set_value(CommunicationResult{
                CommunicationResult::Status::InvalidResponse,
                0,
                "PDU length is zero"
            });
            return;
        }

        std::vector<uint8_t> pdu(pduLen);
        std::cout << "[TX] reading PDU (TID=" << tid << ", " << pduLen << " bytes)..." << std::endl;
        readBytes = asio::read(m_socket, asio::buffer(pdu), ec);
        if (ec) {
            m_connected.store(false, std::memory_order_release);
            std::cout << "[TX] read PDU failed (TID=" << tid << "): " << ec.message() << std::endl;
            p.set_value(CommunicationResult{
                CommunicationResult::Status::NetworkError,
                0,
                "Read PDU failed: " + ec.message()
            });
            return;
        }
        std::cout << "[TX] PDU received (TID=" << tid << ", " << readBytes << " bytes)" << std::endl;

        // 5️⃣ 组装完整响应帧
        response.clear();
        response.reserve(7 + pduLen);
        response.insert(response.end(), mbap.begin(), mbap.end());
        response.insert(response.end(), pdu.begin(), pdu.end());

        // 打印响应帧十六进制（调试用）
        std::cout << "[MODBUS] Response hex (TID=" << tid << "): ";
        for (size_t i = 0; i < response.size(); ++i) {
            printf("%02X ", response[i]);
        }
        std::cout << std::endl;

        // 6️⃣ 检查事务 ID 匹配
        uint16_t reqTid = (static_cast<uint16_t>(frame[0]) << 8) | frame[1];
        uint16_t respTid = mbapHdr->transactionId;

        if (reqTid != respTid) {
            std::cout << "[TX] TID mismatch (TID=" << tid
                      << "): req=" << reqTid << " resp=" << respTid << std::endl;
            p.set_value(CommunicationResult{
                CommunicationResult::Status::InvalidResponse,
                0,
                "Transaction ID mismatch: req=" + std::to_string(reqTid) +
                " resp=" + std::to_string(respTid)
            });
            return;
        }

        // 7️⃣ 检查 Modbus 异常
        uint8_t responseFc = pdu[0];                      // PDU 第一字节 = FC
        uint8_t requestFc = frame[7];                     // 请求帧 PDU FC = frame byte 7
        std::vector<uint8_t> pduData(pdu.begin() + 1, pdu.end()); // FC 之后的数据

        int exCode = tcp_frame::ModbusTcpFrame::checkException(requestFc, responseFc, pduData);
        if (exCode != 0) {
            if (exCode == 0x06) {
                std::cout << "[MODBUS] Exception: Device Busy (TID=" << tid << ")" << std::endl;
                p.set_value(CommunicationResult{
                    CommunicationResult::Status::Busy,
                    exCode,
                    "PLC device busy"
                });
            } else {
                std::cout << "[MODBUS] Exception code: " << exCode << " (TID=" << tid << ")" << std::endl;
                p.set_value(CommunicationResult{
                    CommunicationResult::Status::ProtocolError,
                    exCode,
                    "Modbus exception code: " + std::to_string(exCode)
                });
            }
            return;
        }

        // 成功
        std::cout << "[TX] transaction succeeded (TID=" << tid << ")" << std::endl;
        p.set_value(CommunicationResult{
            CommunicationResult::Status::Sent,
            0,
            ""
        });
    });

    // 同步等待结果（带超时）
    // 超时处理：future::wait_for 是业务线程的保底防护
    // socket 级超时由 configureSocket() 中的 expires_after() 提供
    auto status = future.wait_for(std::chrono::milliseconds(m_config.timeoutMs + 500));
    if (status == std::future_status::timeout) {
        std::cout << "[TX] transaction timed out after "
                  << (m_config.timeoutMs + 500) << "ms (TID=" << tid << ")"
                  << " — closing socket to abort io" << std::endl;
        // 超时：关闭 socket 中断阻塞的 I/O 操作
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
    // 使用 relaxed ordering，因为 TID 递增仅要求原子性不要求 happens-before
    return m_transactionId.fetch_add(1, std::memory_order_relaxed);
}

void AsioModbusTcpClient::configureSocket() {
    if (!m_socket.is_open()) {
        std::cout << "[CONNECT] configureSocket() skipped — socket not open" << std::endl;
        return;
    }

    std::error_code ec;

    // TCP_NODELAY — 禁用 Nagle 算法，满足低延迟要求
    m_socket.set_option(asio::ip::tcp::no_delay(true), ec);
    if (ec) {
        std::cout << "[CONNECT] no_delay set failed: " << ec.message() << std::endl;
    }

    // SO_KEEPALIVE — 长连接心跳保活
    m_socket.set_option(asio::socket_base::keep_alive(true), ec);
    if (ec) {
        std::cout << "[CONNECT] keep_alive set failed: " << ec.message() << std::endl;
    }

    std::cout << "[CONNECT] socket configured (no_delay=true, keep_alive=true)" << std::endl;
}

// ========================================================================
//  连接与重连
// ========================================================================

void AsioModbusTcpClient::startReconnect() {
    if (!m_running.load(std::memory_order_acquire)) {
        std::cout << "[CONNECT] startReconnect() skipped — not running" << std::endl;
        return;
    }

    std::cout << "[CONNECT] startReconnect() — resolving "
              << m_config.host << ":" << m_config.port << std::endl;

    // 使用 shared_ptr 确保 resolver 活到回调执行完毕
    auto resolver = std::make_shared<asio::ip::tcp::resolver>(m_ioctx);
    std::string portStr = std::to_string(m_config.port);

    resolver->async_resolve(
        m_config.host, portStr,
        [this, resolver](std::error_code ec, asio::ip::tcp::resolver::results_type endpoints) {
            if (ec) {
                std::cout << "[CONNECT] DNS resolve failed: " << ec.message() << std::endl;
                scheduleReconnect();
                return;
            }

            std::cout << "[CONNECT] DNS resolved — starting async_connect" << std::endl;

            asio::async_connect(
                m_socket, endpoints,
                [this](std::error_code ec, asio::ip::tcp::endpoint ep) {
                    // 入口日志：无论成功/失败/取消，必须确认回调被触发
                    std::cout << "[CONNECT] async_connect callback fired (ec="
                              << ec.value() << ")" << std::endl;

                    if (!ec) {
                        // 连接成功 — socket 已打开，配置参数
                        configureSocket();
                        m_connected.store(true, std::memory_order_release);
                        std::cout << "[CONNECT] 🟢 Successfully connected to "
                                  << ep << std::endl;
                        return;
                    }

                    std::cout << "[CONNECT] 🔴 Connect failed: "
                              << ec.message() << std::endl;
                    std::error_code ignored;
                    m_socket.close(ignored);
                    scheduleReconnect();
                });
        });
}

void AsioModbusTcpClient::scheduleReconnect() {
    if (!m_running.load(std::memory_order_acquire)) {
        std::cout << "[CONNECT] scheduleReconnect() skipped — not running" << std::endl;
        return;
    }

    std::cout << "[CONNECT] scheduling reconnect in "
              << m_config.reconnectIntervalMs << "ms" << std::endl;

    m_timer.expires_from_now(
        std::chrono::milliseconds(m_config.reconnectIntervalMs));
    m_timer.async_wait([this](std::error_code ec) {
        if (ec) {
            std::cout << "[CONNECT] reconnect timer cancelled: "
                      << ec.message() << std::endl;
            return;
        }
        // 防止 stop() 后 cancel 触发重连：
        // timer.cancel() 的行为因操作系统而异，某些平台上 cancel 虽然成功但 ec 为 success。
        // 因此必须显式检查 m_running。
        if (!m_running.load(std::memory_order_acquire)) {
            std::cout << "[CONNECT] reconnect timer fired but not running — ignored" << std::endl;
            return;
        }
        std::cout << "[CONNECT] reconnect timer fired — calling startReconnect()" << std::endl;
        startReconnect();
    });
}

void AsioModbusTcpClient::cleanup() {
    std::cout << "[IO] cleanup() — delegating to stop()" << std::endl;
    stop();
}

} // namespace plc::protocol
