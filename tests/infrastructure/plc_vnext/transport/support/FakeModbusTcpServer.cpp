// ============================================================================
// FakeModbusTcpServer.cpp —— 本地 Modbus TCP 测试服务器实现（测试替身）
// ============================================================================
#include "tests/infrastructure/plc_vnext/transport/support/FakeModbusTcpServer.h"

#include <array>
#include <chrono>
#include <thread>

namespace plc_vnext::transport::test_support {

using asio::ip::tcp;

namespace {

uint16_t be16(const std::vector<uint8_t>& b, size_t off) {
    return static_cast<uint16_t>((static_cast<uint16_t>(b[off]) << 8) |
                                 static_cast<uint16_t>(b[off + 1]));
}

void appendBE16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

}  // namespace

FakeModbusTcpServer::FakeModbusTcpServer(uint8_t unitId)
    : m_unitId(unitId), m_io(), m_acceptor(m_io) {}

FakeModbusTcpServer::~FakeModbusTcpServer() { stop(); }

uint16_t FakeModbusTcpServer::start() {
    if (m_running.load()) return m_port;

    m_acceptor.open(tcp::v4());
    m_acceptor.set_option(tcp::acceptor::reuse_address(true));
    m_acceptor.bind(tcp::endpoint(tcp::v4(), 0));  // port 0 -> 系统分配空闲端口
    m_acceptor.listen();
    m_port = m_acceptor.local_endpoint().port();

    m_running.store(true);
    m_thread = std::thread([this]() { run(); });
    return m_port;
}

void FakeModbusTcpServer::stop() {
    m_running.store(false);
    std::error_code ec;

    // 关闭正在服务的连接 socket，中断 serve() 中的阻塞读，保证 join() 返回
    std::shared_ptr<tcp::socket> active;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        active = m_activeSocket;
    }
    if (active) {
        active->cancel(ec);
        active->close(ec);
    }

    m_acceptor.cancel(ec);
    m_acceptor.close(ec);
    if (m_thread.joinable()) m_thread.join();
}

bool FakeModbusTcpServer::running() const { return m_running.load(); }

void FakeModbusTcpServer::run() {
    while (m_running.load()) {
        std::error_code ec;
        auto socket = std::make_shared<tcp::socket>(m_io);
        m_acceptor.accept(*socket, ec);
        if (ec) continue;  // stop() 取消导致 accept 失败，退出循环
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            ++m_connections;
        }
        serve(std::move(*socket));
    }
}
void FakeModbusTcpServer::serve(tcp::socket socket) {
    auto sock = std::make_shared<tcp::socket>(std::move(socket));
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_activeSocket = sock;
    }
    std::error_code ec;
    for (;;) {
        // 1 -- 读 7 字节 MBAP
        std::array<uint8_t, 7> mbap{};
        std::size_t n = asio::read(*sock, asio::buffer(mbap), ec);
        if (ec || n < mbap.size()) break;  // 对端关闭 / 出错 -> 结束本连接

        uint16_t tid = static_cast<uint16_t>(
            (static_cast<uint16_t>(mbap[0]) << 8) | static_cast<uint16_t>(mbap[1]));
        uint16_t length = static_cast<uint16_t>(
            (static_cast<uint16_t>(mbap[4]) << 8) | static_cast<uint16_t>(mbap[5]));

        // 2 -- 读 (length-1) 字节 PDU = FC + data（unit 已在 MBAP 第 7 字节中读走）
        std::vector<uint8_t> body(length > 0 ? length - 1u : 0u);
        if (body.empty()) break;
        n = asio::read(*sock, asio::buffer(body), ec);
        if (ec || n < body.size()) break;

        // 3 -- 记录请求（0 基址原样）
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            m_requests.push_back(
                ReceivedRequest{tid, body[0],
                                std::vector<uint8_t>(body.begin() + 1, body.end())});
        }

        // 4 -- 消费一次性脚本（在锁外消费，避免长操作持锁）
        bool delaySet = false, noResponse = false, badMbap = false;
        bool badTid = false, invalidLength = false, disconnect = false;
        unsigned delayMs = 0;
        std::optional<uint8_t> exCode;
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            noResponse = m_scriptNoResponse;
            m_scriptNoResponse = false;
            badMbap = m_scriptBadMbap;
            m_scriptBadMbap = false;
            badTid = m_scriptBadTid;
            m_scriptBadTid = false;
            invalidLength = m_scriptInvalidLength;
            m_scriptInvalidLength = false;
            disconnect = m_scriptDisconnect;
            m_scriptDisconnect = false;
            if (m_scriptDelayMs.has_value()) {
                delaySet = true;
                delayMs = *m_scriptDelayMs;
                m_scriptDelayMs.reset();
            }
            if (m_scriptException.has_value()) {
                exCode = *m_scriptException;
                m_scriptException.reset();
            }
        }

        if (noResponse) {
            // 不回包：让客户端读超时。连接保持，等待客户端断开或继续。
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }
        if (delaySet) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }

        std::vector<uint8_t> response;
        if (badMbap) {
            // 非法 MBAP：Protocol ID != 0x0000，且 length 明显过短
            response = {0x00, 0x01, 0x12, 0x34, 0x00, 0x02, m_unitId};
        } else if (exCode.has_value()) {
            response = buildException(tid, body[0], *exCode);
        } else {
            auto data = handleRequest(body);
            if (data.empty()) continue;  // 无法处理 -> 不回包
            response = buildResponse(tid, body[0], data);
            if (badTid) {
                // 篡改 Transaction ID
                response[0] = static_cast<uint8_t>((tid + 1) >> 8);
                response[1] = static_cast<uint8_t>(tid + 1);
            }
            if (invalidLength) {
                // length 字段改得比实际小（客户端按此少读，随后读不到完整帧）
                response[4] = 0x00;
                response[5] = 0x01;
            }
        }

        asio::write(*sock, asio::buffer(response), ec);
        if (ec) break;
        if (disconnect) break;
    }
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_activeSocket.reset();
    }
}
std::vector<uint8_t> FakeModbusTcpServer::handleRequest(const std::vector<uint8_t>& body) {
    // body = [FC(1)][data...]（unit 已在 MBAP 中读取）
    uint8_t fc = body[0];
    const std::vector<uint8_t> data(body.begin() + 1, body.end());
    std::vector<uint8_t> out;

    switch (fc) {
        case 0x01: {  // FC01 read coils
            if (data.size() < 4) return {};
            uint16_t start = be16(data, 0);
            uint16_t count = be16(data, 2);
            std::lock_guard<std::mutex> lock(m_mtx);
            uint8_t byteCount = static_cast<uint8_t>((count + 7) / 8);
            std::vector<uint8_t> bits(byteCount, 0);
            for (uint16_t i = 0; i < count; ++i) {
                auto it = m_coils.find(start + i);
                if (it != m_coils.end() && it->second) {
                    bits[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
                }
            }
            out.push_back(static_cast<uint8_t>(byteCount));
            out.insert(out.end(), bits.begin(), bits.end());
            break;
        }
        case 0x03: {  // FC03 read holding registers
            if (data.size() < 4) return {};
            uint16_t start = be16(data, 0);
            uint16_t count = be16(data, 2);
            std::lock_guard<std::mutex> lock(m_mtx);
            uint8_t byteCount = static_cast<uint8_t>(count * 2);
            out.push_back(byteCount);
            for (uint16_t i = 0; i < count; ++i) {
                auto it = m_regs.find(start + i);
                appendBE16(out, it == m_regs.end() ? 0u : it->second);
            }
            break;
        }
        case 0x05: {  // FC05 write single coil
            if (data.size() < 4) return {};
            uint16_t addr = be16(data, 0);
            uint16_t value = be16(data, 2);
            {
                std::lock_guard<std::mutex> lock(m_mtx);
                m_coils[addr] = (value == 0xFF00);
            }
            out.insert(out.end(), data.begin(), data.begin() + 4);  // echo addr+value
            break;
        }
        case 0x06: {  // FC06 write single register
            if (data.size() < 4) return {};
            uint16_t addr = be16(data, 0);
            uint16_t value = be16(data, 2);
            {
                std::lock_guard<std::mutex> lock(m_mtx);
                m_regs[addr] = value;
            }
            out.insert(out.end(), data.begin(), data.begin() + 4);  // echo addr+value
            break;
        }
        case 0x10: {  // FC10 write multiple registers
            if (data.size() < 6) return {};
            uint16_t addr = be16(data, 0);
            uint16_t count = be16(data, 2);
            std::lock_guard<std::mutex> lock(m_mtx);
            for (uint16_t i = 0; i < count && (5u + i * 2u + 1u) < data.size(); ++i) {
                m_regs[addr + i] = be16(data, 5u + i * 2u);
            }
            appendBE16(out, addr);   // startAddr
            appendBE16(out, count);  // quantity
            break;
        }
        default:
            return {};  // 未知功能码 -> 不回包
    }

    return out;
}
std::vector<uint8_t> FakeModbusTcpServer::buildResponse(
    uint16_t tid, uint8_t fc, const std::vector<uint8_t>& data) const {
    std::vector<uint8_t> frame;
    appendBE16(frame, tid);
    appendBE16(frame, 0x0000);  // Protocol ID
    appendBE16(frame, static_cast<uint16_t>(1 + 1 + data.size()));  // unit + FC + data
    frame.push_back(m_unitId);
    frame.push_back(fc);
    frame.insert(frame.end(), data.begin(), data.end());
    return frame;
}

std::vector<uint8_t> FakeModbusTcpServer::buildException(uint16_t tid, uint8_t fc,
                                                         uint8_t exCode) const {
    std::vector<uint8_t> frame;
    appendBE16(frame, tid);
    appendBE16(frame, 0x0000);
    appendBE16(frame, 1 + 1 + 1);  // unit + FC + exceptionCode
    frame.push_back(m_unitId);
    frame.push_back(static_cast<uint8_t>(fc | 0x80));
    frame.push_back(exCode);
    return frame;
}

// ============================================================================
//  PLC RAM
// ============================================================================
void FakeModbusTcpServer::setCoil(uint16_t address, bool value) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_coils[address] = value;
}

void FakeModbusTcpServer::setRegister(uint16_t address, uint16_t value) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_regs[address] = value;
}

std::optional<bool> FakeModbusTcpServer::coil(uint16_t address) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto it = m_coils.find(address);
    if (it == m_coils.end()) return std::nullopt;
    return it->second;
}

std::optional<uint16_t> FakeModbusTcpServer::reg(uint16_t address) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto it = m_regs.find(address);
    if (it == m_regs.end()) return std::nullopt;
    return it->second;
}

std::vector<FakeModbusTcpServer::ReceivedRequest>
FakeModbusTcpServer::receivedRequests() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_requests;
}

unsigned FakeModbusTcpServer::connectionCount() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_connections;
}

// ============================================================================
//  一次性脚本化故障设置
// ============================================================================

void FakeModbusTcpServer::scriptException(uint8_t exceptionCode) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_scriptException = exceptionCode;
}

void FakeModbusTcpServer::scriptNoResponse() {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_scriptNoResponse = true;
}

void FakeModbusTcpServer::scriptBadMbap() {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_scriptBadMbap = true;
}

void FakeModbusTcpServer::scriptBadTid() {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_scriptBadTid = true;
}

void FakeModbusTcpServer::scriptInvalidLength() {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_scriptInvalidLength = true;
}

void FakeModbusTcpServer::scriptDelayMs(unsigned delayMs) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_scriptDelayMs = delayMs;
}

void FakeModbusTcpServer::scriptDisconnectAfterResponse() {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_scriptDisconnect = true;
}

}  // namespace plc_vnext::transport::test_support



