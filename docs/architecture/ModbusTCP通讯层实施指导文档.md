# Modbus TCP 通讯层实施指导文档

> 基于当前 `IModbusClient` 接口契约 + P4 ProtocolRuntime 架构  的 Asio 网络层实现指导  
> 生成日期：2026-05-27  
> 项目：servoV6 工控上位机  
> 状态：**指导文档，非最终代码**

---

## 目录

1. [定位与上下文](#1-定位与上下文)
2. [Modbus TCP 协议速览](#2-modbus-tcp-协议速览)
3. [实施内容清单](#3-实施内容清单)
4. [文件清单与依赖关系](#4-文件清单与依赖关系)
5. [TDD 开发策略](#5-tdd-开发策略)
6. [关键设计决策](#6-关键设计决策)
7. [实施防坑指南（必读）](#7-实施防坑指南必读)
8. [实施顺序](#8-实施顺序)

---

## 1. 定位与上下文

### 1.1 本阶段的架构位置

在五层架构模型中，本阶段聚焦于 **L3 传输层 + L4 连接管理层** 的真实实现：

```
L1: DRIVER 集成层        ModbusTcpDriver (后续阶段)
L2: PROTOCOL RUNTIME     RegisterCodec / PlcPoller / PlcDevice / RegisterRegistry  ✅ 已实现
L3: TRANSPORT 传输层     AsioModbusTcpClient : IModbusClient  ← 本阶段核心
L4: CONNECTION 连接管理   ModbusConnection (内聚在 AsioModbusTcpClient 中)
L5: FEEDBACK 反馈层      PlcSnapshot / MemorySnapshot  ✅ 已实现
```

### 1.2 上游依赖（已就绪）

| 组件 | 文件 | 对本阶段的约束 |
|------|------|---------------|
| `IModbusClient` | `infrastructure/plc/protocol/IModbusClient.h` | 本阶段必须实现的5个纯虚方法 |
| `CommunicationResult` | `infrastructure/ISystemDriver.h` | 所有返回值类型，含 `ok()` / `retryable()` / `isNetworkIssue()` |
| `PlcPoller` | `infrastructure/plc/protocol/PlcPoller.h` | 消费 `readCoils` / `readHoldingRegisters` |
| `PlcDevice` | `infrastructure/plc/protocol/PlcDevice.h` | 消费 `writeSingleCoil` / `writeSingleRegister` / `writeMultipleRegisters` |

### 1.3 下游消费者（验证目标）

- **PlcPoller::poll()**：每 20ms 高频调用 `readCoils` + `readHoldingRegisters`
- **PlcDevice::dispatchWrite()**：按需调用 `writeSingleCoil` / `writeSingleRegister` / `writeMultipleRegisters`
- **FakeModbusClient**（测试）：TDD 替身，需与本实现保持接口一致

---

## 2. Modbus TCP 协议速览

### 2.1 帧结构对比

```
Modbus RTU 帧（不用于 TCP）:
┌──────────┬──────┬──────────┬───────┬───────┐
│ Slave ID │  FC  │  Data    │  CRC  │ CRC   │
│  1 byte  │1 byte│ N bytes  │ Low   │ High  │
└──────────┴──────┴──────────┴───────┴───────┘

Modbus TCP 帧（本阶段实现）:
┌──────────────────────────────┬──────┬──────────┐
│     MBAP Header (7 bytes)    │  FC  │  Data    │
├──────┬──────┬──────┬─────────┼──────┼──────────┤
│Trans │Proto │Length│ Unit ID │1 byte│ N bytes  │
│  ID  │  ID  │      │         │      │          │
│2 bytes│2 bytes│2 bytes│1 byte │      │          │
└──────┴──────┴──────┴─────────┴──────┴──────────┘
```

### 2.2 MBAP 报文头字段说明

| 字段 | 大小 | 说明 | 本系统取值 |
|------|------|------|-----------|
| Transaction ID | 2 bytes | 请求-响应配对标识 | 递增计数器 `m_transactionId++` |
| Protocol ID | 2 bytes | Modbus = 0x0000 | 固定 `0x0000` |
| Length | 2 bytes | 后续字节数（Unit ID + FC + Data） | 动态计算 |
| Unit ID | 1 byte | 设备地址（TCP 中通常为 0xFF 或 0x01） | 可配置参数 |

### 2.3 各功能码的请求/响应格式

#### FC01 — 读线圈

```
请求:  MBAP | 01 | StartAddr(2B) | Quantity(2B)
响应:  MBAP | 01 | ByteCount(1B) | CoilData(N bytes, MSB打包)
```

#### FC03 — 读保持寄存器

```
请求:  MBAP | 03 | StartAddr(2B) | Quantity(2B)
响应:  MBAP | 03 | ByteCount(1B) | RegData(N×2 bytes, BigEndian每字)
```

#### FC05 — 写单线圈

```
请求:  MBAP | 05 | Addr(2B) | Value(2B: 0xFF00=ON / 0x0000=OFF)
响应:  MBAP | 05 | Addr(2B) | Value(2B)  ← 回显
```

#### FC06 — 写单寄存器

```
请求:  MBAP | 06 | Addr(2B) | Value(2B)
响应:  MBAP | 06 | Addr(2B) | Value(2B)  ← 回显
```

#### FC16 (0x10) — 写多寄存器

```
请求:  MBAP | 10 | StartAddr(2B) | Quantity(2B) | ByteCount(1B) | RegData(N×2B)
响应:  MBAP | 10 | StartAddr(2B) | Quantity(2B)
```

#### 异常响应

```
响应:  MBAP | FC+0x80 | ExceptionCode(1B)
       FC=0x01 → 0x81
       FC=0x03 → 0x83
       FC=0x05 → 0x85
       FC=0x06 → 0x86
       FC=0x10 → 0x90
```

---

## 3. 实施内容清单

### 3.1 核心组件

| # | 组件 | 文件 | 职责 |
|---|------|------|------|
| 1 | **AsioModbusTcpClient** | `infrastructure/plc/protocol/AsioModbusTcpClient.h` | 实现 `IModbusClient`，基于 Asio 异步 I/O |
| 2 | **ModbusTcpFrame** | `infrastructure/plc/protocol/ModbusTcpFrame.h` | MBAP 封包/解包工具（纯静态函数） |
| 3 | **ModbusConnection** | 内聚在 AsioModbusTcpClient 中 | TCP 连接生命周期、异步读写、超时控制 |
| 4 | **AsioContext** | 内聚在 AsioModbusTcpClient 中 | `asio::io_context` 生命周期 + 工作线程 |

### 3.2 各组件详细职责

#### 3.2.1 ModbusTcpFrame — MBAP 封包/解包（纯函数，零依赖）

```cpp
namespace plc::protocol::tcp {

struct ModbusTcpFrame {
    // ── 封包（构建完整的 Modbus TCP 请求帧） ──
    
    /// @brief 构建 FC01 请求帧（读线圈）
    /// @param tid 事务 ID
    /// @param unitId 单元 ID
    /// @param startAddr 起始地址
    /// @param count 线圈数量
    /// @return 完整帧字节序列
    static std::vector<uint8_t> buildReadCoils(
        uint16_t tid, uint8_t unitId,
        uint16_t startAddr, uint16_t count);

    /// @brief 构建 FC03 请求帧（读保持寄存器）
    static std::vector<uint8_t> buildReadHoldingRegisters(
        uint16_t tid, uint8_t unitId,
        uint16_t startAddr, uint16_t count);

    /// @brief 构建 FC05 请求帧（写单线圈）
    static std::vector<uint8_t> buildWriteSingleCoil(
        uint16_t tid, uint8_t unitId,
        uint16_t addr, bool value);

    /// @brief 构建 FC06 请求帧（写单寄存器）
    static std::vector<uint8_t> buildWriteSingleRegister(
        uint16_t tid, uint8_t unitId,
        uint16_t addr, uint16_t value);

    /// @brief 构建 FC16 请求帧（写多寄存器）
    static std::vector<uint8_t> buildWriteMultipleRegisters(
        uint16_t tid, uint8_t unitId,
        uint16_t startAddr, const std::vector<uint16_t>& values);

    // ── 解包（解析 Modbus TCP 响应帧） ──
    
    /// @brief 解析 MBAP 头，验证 Transaction ID 和 Protocol ID
    /// @return {tid, length} 或 nullopt（帧太短/校验失败）
    struct MbapHeader {
        uint16_t transactionId;
        uint16_t length;  // 后续字节数（Unit ID + FC + Data）
    };
    static std::optional<MbapHeader> parseMbap(
        const std::vector<uint8_t>& frame);

    /// @brief 检查是否为异常响应
    /// @param fc 原始功能码
    /// @param responseFc 响应中的功能码（若为异常则为 fc | 0x80）
    /// @return exceptionCode（0 = 正常，非0 = 异常码）
    static int checkException(uint8_t fc, uint8_t responseFc, 
                              const std::vector<uint8_t>& data);

    /// @brief 从 FC01 响应中提取线圈数据
    static std::vector<uint8_t> parseCoilResponse(const std::vector<uint8_t>& frame);

    /// @brief 从 FC03 响应中提取寄存器数据
    static std::vector<uint16_t> parseRegisterResponse(const std::vector<uint8_t>& frame);
};

} // namespace plc::protocol::tcp
```

**设计要点**：
- 纯静态函数，无状态，无 I/O — 100% 可单元测试
- 只处理字节级别的拼接/解析，不理解业务语义
- `parseMbap()` 返回 `std::optional` 替代异常 — 零开销边界检查
- 放在 `plc::protocol::tcp` 子命名空间，避免污染 `plc::protocol`

#### 3.2.2 AsioModbusTcpClient — IModbusClient 的真实实现

```cpp
// infrastructure/plc/protocol/AsioModbusTcpClient.h

namespace plc::protocol {

/**
 * @brief 基于 Standalone Asio 的 Modbus TCP 客户端
 *
 * @details
 * 核心设计:
 *   1. 单 io_context + 单工作线程 驱动所有异步 I/O
 *   2. 每个 Modbus 请求使用 async_write + async_read + steady_timer 超时
 *   3. 连接状态机: Disconnected → Connecting → Connected → Disconnected
 *   4. 事务 ID 递增（Monotonic），用于请求-响应配对
 *
 * 线程模型:
 *   - io_context::run() 在独立线程中执行
 *   - 业务线程通过同步封装接口调用 → 内部用 promise/future 桥接异步操作
 *   - 或: 业务线程直接调用 async_xxx（需要上层支持协程/回调）
 *
 * 推荐初始实现: 同步封装（promise/future），简单可靠
 */
class AsioModbusTcpClient : public IModbusClient {
public:
    struct Config {
        std::string host = "192.168.1.88"; // 测试用的汇川PLC IP
        uint16_t    port = 502;
        uint8_t     unitId = 0x01;    // Modbus TCP 默认单元 ID
        uint32_t    timeoutMs = 15;   // 读写超时（必须 ≤ 15ms，适配 20ms 高频轮询；超时宁可本轮数据报废也绝不挂起业务主循环）
        uint32_t    reconnectIntervalMs = 2000; // 断线重连间隔
    };

    explicit AsioModbusTcpClient(const Config& config);
    ~AsioModbusTcpClient() override;

    // ── 连接管理 ──
    
    /// @brief 启动 io_context 并异步连接
    void start();

    /// @brief 停止 io_context，断开连接
    void stop();

    /// @brief 当前连接状态
    bool isConnected() const;

    // ── IModbusClient 接口实现 (读通道) ──
    
    CommunicationResult readCoils(
        uint16_t startAddress, uint16_t count,
        std::vector<uint8_t>& payload) override;

    CommunicationResult readHoldingRegisters(
        uint16_t startAddress, uint16_t count,
        std::vector<uint16_t>& payload) override;

    // ── IModbusClient 接口实现 (写通道) ──
    
    CommunicationResult writeSingleCoil(
        uint16_t address, bool value) override;

    CommunicationResult writeSingleRegister(
        uint16_t address, uint16_t value) override;

    CommunicationResult writeMultipleRegisters(
        uint16_t startAddress,
        const std::vector<uint16_t>& values) override;

private:
    // ── 内部实现 ──

    /// @brief 执行一次完整的 Modbus 请求-响应周期（同步阻塞）
    /// @param frame 完整的请求帧
    /// @return {result, responseFrame}
    CommunicationResult executeTransaction(const std::vector<uint8_t>& frame,
                                            std::vector<uint8_t>& response);

    /// @brief 确保 TCP 已连接（必要时触发重连）
    CommunicationResult ensureConnected();

    /// @brief 获取下一个事务 ID（线程安全）
    uint16_t nextTransactionId();

    // ── Asio 基础设施 ──
    asio::io_context             m_ioctx;
    std::unique_ptr<asio::io_context::work> m_work;  // 防止 io_context 提前退出
    std::thread                  m_worker;
    asio::ip::tcp::socket        m_socket;
    asio::steady_timer           m_timer;   // 超时/重连定时器

    Config                       m_config;
    std::atomic<uint16_t>        m_transactionId{0};
    std::atomic<bool>            m_connected{false};
    std::mutex                   m_socketMutex;  // 保护 socket 操作
};

} // namespace plc::protocol
```

**设计要点**：

1. **事务 ID 单调递增**：每个 Modbus 请求分配唯一 TID，响应帧中校验匹配
2. **连接状态原子化**：`m_connected` 用 `atomic<bool>`，业务线程可快速判断
3. **socket 互斥**：所有 socket 操作串行化，避免并发写导致的帧交织
4. **超时机制**：`asio::steady_timer` + `async_wait`，超时后取消 socket 异步操作
5. **重连策略**：断开后 `reconnectIntervalMs` 间隔重试，避免 CPU 空转

#### 3.2.3 异步-同步桥接（内部实现）

```cpp
// executeTransaction 核心流程伪代码:

CommunicationResult AsioModbusTcpClient::executeTransaction(
    const std::vector<uint8_t>& requestFrame,
    std::vector<uint8_t>& responseFrame)
{
    // 1. 确保连接
    auto connResult = ensureConnected();
    if (!connResult.ok()) return connResult;

    // 2. 同步执行（使用 promise/future 桥接）
    std::promise<CommunicationResult> promise;
    auto future = promise.get_future();

    // 3. Post 到 io_context 线程执行异步读写
    asio::post(m_ioctx, [this, &requestFrame, &responseFrame, 
                         p = std::move(promise)]() mutable {
        // 3a. 异步写
        asio::async_write(m_socket, asio::buffer(requestFrame),
            [this, &responseFrame, p = std::move(p)]
            (std::error_code ec, size_t) mutable {
                if (ec) {
                    p.set_value(CommunicationResult{
                        CommunicationResult::Status::NetworkError, 0, ec.message()
                    });
                    return;
                }
                // ═══════════════════════════════════════════════════════
                // 3b. 异步读：严格两步分帧策略（防粘包/半包 — 致命要点）
                //
                // 错误做法：asio::async_read(socket, buffer(largeBuf), ...)
                //         一次性读取所有可用数据 → 下一帧的字节被误读！
                //
                // 正确做法：第1步 — 先严格读取 7 字节 MBAP 头
                //          第2步 — 解析 Length → 再严格读取 Length 字节 PDU
                // ═══════════════════════════════════════════════════════
                auto headerBuf = std::make_shared<std::array<uint8_t, 7>>();
                asio::async_read(m_socket, asio::buffer(*headerBuf),
                    [this, headerBuf, p = std::move(p)]
                    (std::error_code ec, size_t) mutable {
                        if (ec) {
                            p.set_value(CommunicationResult{
                                CommunicationResult::Status::NetworkError, 0,
                                "MBAP header read: " + ec.message()
                            });
                            return;
                        }
                        // 3c. 解析 MBAP 头，提取 Length 字段（Big-Endian）
                        uint16_t tid  = (static_cast<uint16_t>(headerBuf->at(0)) << 8)
                                      |  static_cast<uint16_t>(headerBuf->at(1));
                        uint16_t pid  = (static_cast<uint16_t>(headerBuf->at(2)) << 8)
                                      |  static_cast<uint16_t>(headerBuf->at(3));
                        uint16_t length = (static_cast<uint16_t>(headerBuf->at(4)) << 8)
                                        |  static_cast<uint16_t>(headerBuf->at(5));
                        uint8_t  uid  = headerBuf->at(6);
                        
                        // PID 必须为 0x0000（Modbus 协议规范）
                        if (pid != 0x0000) {
                            p.set_value(CommunicationResult{
                                CommunicationResult::Status::ProtocolError, 0,
                                "Invalid Protocol ID in MBAP response"
                            });
                            return;
                        }
                        
                        // 3d. 第2步：发起第二次严格读取 —— 精确读取 Length 字节
                        // 这一步是粘包/半包免疫的核心：无论如何只读取 Length 字节
                        auto pduBuf = std::make_shared<std::vector<uint8_t>>(length);
                        asio::async_read(m_socket, asio::buffer(*pduBuf),
                            [tid, length, pduBuf, p = std::move(p)]
                            (std::error_code ec, size_t) mutable {
                                if (ec) {
                                    p.set_value(CommunicationResult{
                                        CommunicationResult::Status::NetworkError, 0,
                                        "PDU read: " + ec.message()
                                    });
                                    return;
                                }
                                // 3e. 校验 Transaction ID（异常响应也必须匹配 TID）
                                //     → 由 ModbusTcpFrame 工具函数完成
                                // 3f. 检查异常响应 (FC & 0x80) + 解析 PDU
                                //     → 由 ModbusTcpFrame::checkException / parseXxx 完成
                                // 3g. 构造成功结果
                                p.set_value(CommunicationResult::ok());
                            });
                    });
            });

        // 3g. 启动超时定时器
        m_timer.expires_after(std::chrono::milliseconds(m_config.timeoutMs));
        m_timer.async_wait([&](std::error_code ec) {
            if (!ec) {
                m_socket.cancel();  // 超时 → 取消 socket 操作
            }
        });
    });

    // 4. 阻塞等待结果
    return future.get();
}
```

**设计要点**：

- 所有 socket 操作在 `io_context` 线程中执行 → 线程安全
- 业务线程通过 `promise/future` 同步等待 → 调用方无感知异步
- 超时通过 `steady_timer` + `socket.cancel()` 实现 → 避免永久阻塞
- `CommunicationResult` 在异步回调中构造 → 诊断信息精确

---

## 4. 文件清单与依赖关系

### 4.1 新增文件

```
infrastructure/plc/protocol/
├── AsioModbusTcpClient.h            ← 本阶段新建 #1
├── AsioModbusTcpClient.cpp          ← 本阶段新建 #2 (实现)
├── ModbusTcpFrame.h                 ← 本阶段新建 #3 (纯静态工具)
│
tests/infrastructure/protocol/
├── test_modbus_tcp_frame.cpp        ← 本阶段新建 #4 (封包/解包 TDD)
├── test_asio_modbus_client.cpp      ← 本阶段新建 #5 (集成测试)
└── test_modbus_tcp_integration.cpp  ← 本阶段新建 #6 (端到端测试)
```

### 4.2 依赖外部库

```
external/
└── asio/                             ← 需要引入 Standalone Asio
    └── asio.hpp                      (仅头文件, 约 1.5MB)
```

### 4.3 CMake 变更

```cmake
# infrastructure/CMakeLists.txt 需要添加:

# 1. Asio 头文件路径
target_include_directories(infrastructure PRIVATE 
    ${CMAKE_SOURCE_DIR}/external/asio/asio/include)

# 2. 如果需要链接系统库
target_link_libraries(infrastructure PUBLIC 
    ws2_32     # Windows Socket API
    # 或 Linux: pthread
)

# 3. 测试目标
add_executable(test_modbus_tcp_frame 
    tests/infrastructure/protocol/test_modbus_tcp_frame.cpp)
target_link_libraries(test_modbus_tcp_frame gtest_main infrastructure)

add_executable(test_asio_modbus_client
    tests/infrastructure/protocol/test_asio_modbus_client.cpp)
target_link_libraries(test_asio_modbus_client gtest_main infrastructure)
```

### 4.4 依赖关系图

```
AsioModbusTcpClient
    ├── IModbusClient.h          (接口契约)
    ├── ISystemDriver.h          (CommunicationResult)
    ├── ModbusTcpFrame.h         (MBAP 封包/解包)
    ├── asio.hpp                 (网络 I/O)
    └── <thread>, <mutex>, <atomic>

ModbusTcpFrame
    └── (无依赖 — 纯字节操作)
```

---

## 5. TDD 开发策略

### 5.1 测试清单与顺序

| # | 测试文件 | 测试对象 | 依赖 Mock | 优先级 |
|---|---------|---------|-----------|--------|
| 1 | `test_modbus_tcp_frame.cpp` | `ModbusTcpFrame` | 无（纯函数） | **P0 — 最先** |
| 2 | `test_asio_modbus_client.cpp` | `AsioModbusTcpClient` | 需真实 TCP 或 Mock socket | **P1 — 其次** |
| 3 | `test_modbus_tcp_integration.cpp` | 完整读/写管线 | 需运行中 Modbus TCP 服务器 | **P2 — 最后** |

### 5.2 测试用例详细设计

#### 5.2.1 test_modbus_tcp_frame.cpp（纯函数测试，无需网络）

```cpp
// ====== MBAP 封包测试 ======

TEST(ModbusTcpFrameTest, BuildReadCoils_ProducesCorrectFrame) {
    auto frame = ModbusTcpFrame::buildReadCoils(0x0001, 0xFF, 0x0064, 16);
    // 期望帧: 
    // MBAP: 0x0001 | 0x0000 | 0x0006 | 0xFF
    // PDU:  0x01   | 0x0064 | 0x0010
    EXPECT_EQ(frame.size(), 12);  // 7 MBAP + 5 PDU
    EXPECT_EQ(frame[0], 0x00);    // TID High
    EXPECT_EQ(frame[1], 0x01);    // TID Low
    EXPECT_EQ(frame[2], 0x00);    // PID High
    EXPECT_EQ(frame[3], 0x00);    // PID Low
    EXPECT_EQ(frame[4], 0x00);    // Length High
    EXPECT_EQ(frame[5], 0x06);    // Length Low = 6 (UnitID + FC + StartAddr + Count)
    EXPECT_EQ(frame[6], 0xFF);    // Unit ID
    EXPECT_EQ(frame[7], 0x01);    // FC = 01 Read Coils
    EXPECT_EQ(frame[8], 0x00);    // StartAddr High
    EXPECT_EQ(frame[9], 0x64);    // StartAddr Low = 100
    EXPECT_EQ(frame[10], 0x00);   // Quantity High
    EXPECT_EQ(frame[11], 0x10);   // Quantity Low = 16
}

TEST(ModbusTcpFrameTest, BuildReadHoldingRegisters_LengthFieldCorrect) {
    auto frame = ModbusTcpFrame::buildReadHoldingRegisters(0x000A, 0x01, 0x007B, 2);
    // Length = 1(UnitID) + 1(FC) + 2(StartAddr) + 2(Count) = 6
    EXPECT_EQ(frame[5], 0x06);
}

TEST(ModbusTcpFrameTest, BuildWriteSingleCoil_ON_ProducesFF00) {
    auto frame = ModbusTcpFrame::buildWriteSingleCoil(1, 0x01, 0x000A, true);
    // Value = 0xFF00
    EXPECT_EQ(frame[frame.size()-2], 0xFF);
    EXPECT_EQ(frame[frame.size()-1], 0x00);
}

TEST(ModbusTcpFrameTest, BuildWriteSingleCoil_OFF_Produces0000) {
    auto frame = ModbusTcpFrame::buildWriteSingleCoil(1, 0x01, 0x000A, false);
    EXPECT_EQ(frame[frame.size()-2], 0x00);
    EXPECT_EQ(frame[frame.size()-1], 0x00);
}

TEST(ModbusTcpFrameTest, BuildWriteMultipleRegisters_ByteCountCalculated) {
    std::vector<uint16_t> values = {0x0000, 0x4348}; // Float 200.0f in CDAB
    auto frame = ModbusTcpFrame::buildWriteMultipleRegisters(5, 0xFF, 24, values);
    // PDU: FC(1) + StartAddr(2) + Quantity(2) + ByteCount(1) + Data(values.size()*2)
    // Length = 7 + values.size()*2
    EXPECT_EQ(frame[5], 0x07 + static_cast<uint8_t>(values.size() * 2));
}

// ====== MBAP 解包测试 ======

TEST(ModbusTcpFrameTest, ParseMbap_ValidFrame_ReturnsHeader) {
    // 模拟 FC03 响应帧
    std::vector<uint8_t> frame = {
        0x00, 0x01,  // TID = 1
        0x00, 0x00,  // PID = 0
        0x00, 0x05,  // Length = 5 (后续 5 字节)
        0xFF,        // Unit ID
        0x03,        // FC = 03
        0x02,        // ByteCount = 2
        0x00, 0x0A   // 1 个寄存器的值 = 10
    };
    auto header = ModbusTcpFrame::parseMbap(frame);
    ASSERT_TRUE(header.has_value());
    EXPECT_EQ(header->transactionId, 1);
    EXPECT_EQ(header->length, 5);
}

TEST(ModbusTcpFrameTest, ParseMbap_FrameTooShort_ReturnsNullopt) {
    std::vector<uint8_t> frame = {0x00, 0x01, 0x00}; // 只有 3 字节
    EXPECT_FALSE(ModbusTcpFrame::parseMbap(frame).has_value());
}

TEST(ModbusTcpFrameTest, CheckException_NormalResponse_ReturnsZero) {
    int code = ModbusTcpFrame::checkException(0x03, 0x03, {});
    EXPECT_EQ(code, 0);
}

TEST(ModbusTcpFrameTest, CheckException_ExceptionResponse_ReturnsCode) {
    // FC03 → 异常 0x83, 异常码 0x02
    std::vector<uint8_t> data = {0x02}; // Exception Code = 0x02
    int code = ModbusTcpFrame::checkException(0x03, 0x83, data);
    EXPECT_EQ(code, 0x02);
}

TEST(ModbusTcpFrameTest, ParseCoilResponse_ExtractsCorrectBits) {
    // 模拟 FC01 响应: 读取 16 个线圈, 响应 2 字节
    std::vector<uint8_t> frame = {
        0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0xFF, // MBAP (length=4)
        0x01,       // FC = 01
        0x02,       // ByteCount = 2
        0x0F, 0x00  // Coil data: 0-3=ON, 4-15=OFF
    };
    auto coils = ModbusTcpFrame::parseCoilResponse(frame);
    EXPECT_EQ(coils.size(), 2);
    EXPECT_EQ(coils[0], 0x0F);
    EXPECT_EQ(coils[1], 0x00);
}

TEST(ModbusTcpFrameTest, ParseRegisterResponse_ExtractsCorrectWords) {
    // 模拟 FC03 响应: 读取 2 个寄存器
    std::vector<uint8_t> frame = {
        0x00, 0x01, 0x00, 0x00, 0x00, 0x05, 0xFF, // MBAP (length=5)
        0x03,       // FC = 03
        0x04,       // ByteCount = 4
        0x00, 0x00, // Reg[0] = 0x0000
        0x43, 0x48  // Reg[1] = 0x4348
    };
    auto regs = ModbusTcpFrame::parseRegisterResponse(frame);
    EXPECT_EQ(regs.size(), 2);
    EXPECT_EQ(regs[0], 0x0000);
    EXPECT_EQ(regs[1], 0x4348);
}
```

#### 5.2.2 test_asio_modbus_client.cpp（需 Mock 或本地 Modbus 服务器）

```cpp
// ====== 连接管理测试 ======

TEST(AsioModbusClientTest, StartStop_Lifecycle) {
    AsioModbusTcpClient::Config config;
    config.host = "127.0.0.1";
    config.port = 502;
    AsioModbusTcpClient client(config);
    
    EXPECT_FALSE(client.isConnected());
    client.start();
    // 注: 无真实 PLC 时 start 后可能处于 connecting 状态
    // 重点测试资源创建/销毁的正确性
    client.stop();
}

// ====== 写接口测试（需真实 PLC 或 Modbus 模拟器） ======

TEST(AsioModbusClientTest, WriteSingleCoil_ReturnsSent_OnSuccess) {
    // 前置条件: 本地运行 Modbus TCP 模拟器
    AsioModbusTcpClient client({"127.0.0.1", 502, 0x01, 1000, 2000});
    client.start();
    
    auto result = client.writeSingleCoil(0x0000, true);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.status, CommunicationResult::Status::Sent);
}

TEST(AsioModbusClientTest, ReadHoldingRegisters_ReturnsPayload_OnSuccess) {
    AsioModbusTcpClient client({"127.0.0.1", 502, 0x01, 1000, 2000});
    client.start();
    
    std::vector<uint16_t> payload;
    auto result = client.readHoldingRegisters(0x0000, 2, payload);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(payload.size(), 2);
}

// ====== 错误场景测试 ======

TEST(AsioModbusClientTest, ConnectToInvalidHost_ReturnsNetworkError) {
    AsioModbusTcpClient client({"192.0.2.1", 502, 0x01, 500, 2000});
    client.start();
    
    std::vector<uint16_t> payload;
    auto result = client.readHoldingRegisters(0x0000, 1, payload);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.isNetworkIssue());
}

TEST(AsioModbusClientTest, SocketTimeout_ReturnsTimeout) {
    // 使用 iptables 或防火墙规则丢弃特定端口流量模拟超时
    // ...
}

TEST(AsioModbusClientTest, PlcReturnsException_ReturnsProtocolError) {
    // 向 PLC 发送非法地址请求，期望收到异常响应
    // ...
}
```

#### 5.2.3 FakeModbusClient 更新（用于上层 PlcPoller/PlcDevice 测试）

现有的 `FakeModbusClient`（在测试代码中）已经实现了 `IModbusClient` 接口。本阶段需要确保：
- `FakeModbusClient` 与 `AsioModbusTcpClient` 共享同一个 `IModbusClient` 接口
- 上层测试（PlcPoller、PlcDevice）可以用 `FakeModbusClient` 验证逻辑
- 网络层测试（test_asio_modbus_client）聚焦于真实的 TCP 通讯

### 5.3 开发环境准备

```bash
# 1. 下载 Standalone Asio（仅头文件）
cd external
git clone https://github.com/chriskohlhoff/asio.git --branch asio-1-28-0 --depth 1

# 2. Modbus TCP 模拟器（用于测试）
# 推荐: diagslave (https://www.modbusdriver.com/diagslave.html)
# 或 modbus-tcp-server (Python: pip install pymodbus)

# 启动模拟器（终端 1）:
python -m pymodbus.server --port 502

# 运行测试（终端 2）:
cd build
ctest -R modbus_tcp
```

---

## 6. 关键设计决策

### 6.1 为什么选择 Standalone Asio 而非 libmodbus

| 维度 | Standalone Asio | libmodbus |
|------|----------------|-----------|
| **协议掌控力** | 100%（自己拼 MBAP 头） | 黑盒（内部自动处理） |
| **异步模型** | 原生 async_read/async_write | 阻塞式 + select |
| **多线程控制** | io_context 单线程驱动，完美可控 | 需要外部管理线程 |
| **内存控制** | 零拷贝（`std::span`, `std::vector`） | 内部缓冲区管理 |
| **现代 C++ 集成** | `std::vector<uint8_t>`, `std::span`, `std::promise/future` | C 风格 API |
| **依赖体积** | 仅头文件 (~1.5MB) | 需编译 .so/.dll |
| **与 PlcPoller 契合度** | 完美匹配 20ms 高频轮询 | 需要额外抽象层 |
| **超时控制** | `steady_timer` + `socket.cancel()` 精确 | `select` + 全局超时 |

**结论**：Standalone Asio 与现代 C++ 风格完全一致，零拷贝直接将网络数据注入 PlcSnapshot，且无需引入整套 Boost。

### 6.2 为什么 ModbusTcpFrame 是独立纯静态类

- **可测试性**：`buildReadCoils()` 可以在无网络环境下完整测试
- **关注点分离**：字节拼接与 socket I/O 完全解耦
- **可复用性**：如果未来需要支持 Modbus RTU（串口），只需替换传输层，`ModbusTcpFrame` 的 PDU 逻辑可复用
- **编译速度**：无 Asio 依赖，修改传输层不需要重编译协议层

### 6.3 为什么选择同步封装（promise/future）而非纯粹异步

| 维度 | 同步封装 (promise/future) | 纯粹异步 (回调链) |
|------|--------------------------|-------------------|
| **调用方复杂度** | 低 — 调用方代码保持同步风格 | 高 — 调用方需处理回调/协程 |
| **与 IModbusClient 兼容** | 直接匹配虚函数签名 | 需要修改接口（破坏现有架构） |
| **调试难度** | 低 — 调用栈完整 | 高 — 异步回调栈撕裂 |
| **性能损失** | 轻微（一次 promise/future 开销 ~100ns） | 无 |
| **线程安全** | 业务线程阻塞等待，安全 | 需处理跨线程回调 |

**结论**：在 PlcPoller 的 20ms 轮询周期中，promise/future 的开销（~100ns）完全可以忽略。同步封装保持了 `IModbusClient` 接口的稳定，且调用方（PlcPoller、PlcDevice）无需感知异步实现细节。

### 6.4 线程模型选择

```
┌────────────────────────────┐
│  业务线程（主循环 20ms）    │
│  PlcPoller::poll()         │
│  → readCoils()             │  ← 同步阻塞等待 future
│  → readHoldingRegisters()  │
│       │                     │
│       │ promise/future      │
│       ▼                     │
│  io_context 线程            │
│  → async_write              │  ← 真正执行 socket I/O
│  → async_read               │
│  → steady_timer             │  ← 超时监控
│  → promise.set_value()      │  ← 唤醒业务线程
└────────────────────────────┘
```

**关键约束**：
1. 所有 socket 操作必须在 `io_context` 线程中执行（Asio 线程安全要求）
2. 连接/重连操作也 post 到 `io_context` 线程
3. `m_socketMutex` 防止并发访问（如 write 和 connect 同时进行）
4. `m_connected` 用 `atomic<bool>` 使业务线程可无锁查询连接状态

---

## 7. 实施防坑指南（必读）

> ⚠️ **致命警告**：以下两个问题是 Modbus TCP 通讯层实现中最常见的致命错误。  
> 在动手写 `AsioModbusTcpClient.cpp` 之前，必须熟读本章节。  
> 违反其中任何一条，都将在上线后导致**上位机卡死**或**数据错乱**的灾难性后果。

---

### 7.1 致命点一：TCP 粘包 / 半包（帧边界破坏）

#### 7.1.1 问题本质

Modbus TCP 帧**不是**基于换行符 `\n` 的分隔协议，也不是固定长度协议。它的帧边界由 MBAP 头中的 **Length 字段**动态决定：

```
┌──────────────────────────┬──────────────────────────────────┐
│  MBAP Header (7 bytes)   │  PDU = UnitID + FC + Data       │
│  TID(2) PID(2) Len(2)    │  ← Length 字节 →                 │
└──────────────────────────┴──────────────────────────────────┘
```

TCP 是**流式协议**，socket 中到达的数据没有帧边界。当调用 `asio::read(socket, buffer(largeBuf))` 时，Asio 返回的是 socket 缓冲区中**任意数量的可用字节**，可能出现：

| 现象 | 说明 | 后果 |
|------|------|------|
| **粘包** | 一次 `read` 返回了两帧（或多帧）的数据 | 第二帧数据被误当作第一帧 PDU |
| **半包** | 一次 `read` 只返回了半帧（如 MBAP 头 7 字节都未收齐） | 解析出的 Length 为垃圾值 → 后续读取错位 |
| **混合** | 一次 `read` 返回了 1.5 帧 | 第 2 帧被拦腰截断 → 所有后续帧全部错位 |

#### 7.1.2 致命错误做法（绝对禁止）

```cpp
// ❌ 致命错误：一次性读取所有可用数据
std::array<uint8_t, 1024> largeBuf;
size_t n = socket.read_some(asio::buffer(largeBuf));
// 然后尝试在 largeBuf 中"查找"帧边界 → 必然失败！
```

这种做法的结果：帧边界错位一次 → 所有后续帧全部解析失败 → 通讯层永久瘫痪，只能重启上位机。

#### 7.1.3 正确做法：严格两步分帧读取

**铁律：必须先发起一次严格读取 7 个字节 (MBAP 头) 的读操作，解析 Length，然后再发起第二次严格读取 Length 个字节。**

```
┌──────────────────────────────────────────────────────────────┐
│  Step 1: async_read(socket, 7 bytes)   ← 严格 7 字节         │
│  ↓                                                           │
│  解析 MBAP: TID=? PID=0x0000 Length=? UnitID=?               │
│  ↓                                                           │
│  Step 2: async_read(socket, Length bytes) ← 严格 Length 字节  │
│  ↓                                                           │
│  得到完整 PDU: UnitID + FC + Data                             │
│  ↓                                                           │
│  校验 TID + 检查异常 + 解析数据                               │
└──────────────────────────────────────────────────────────────┘
```

**关键原理**：
- `asio::async_read()` 与 `asio::read_some()` 不同，它会**持续读取直到接收了指定数量的字节**才回调，天然免疫半包
- 第二次 `async_read` 的长度由第一次解析出的 Length 字段决定，天然免疫粘包（它不会多读一个字节）
- Step 2 完成后，socket 中剩余的数据是**下一帧的起始字节**，由下一次请求的 Step 1 消费

#### 7.1.4 代码实现参考

详见 [3.2.3 节 executeTransaction 伪代码](#323-异步-同步桥接内部实现) 中 Step 3b~3d 的完整实现。

**单元测试覆盖要求**：
- 模拟半包场景：Mock socket 第一次只返回 3 字节 → 验证 `async_read(7)` 会继续等待剩余 4 字节
- 模拟粘包场景：Mock socket 在 7 字节 MBAP 之后，缓冲区中同时含有一帧完整 PDU + 下一帧的 MBAP → 验证第二次 `async_read(Length)` 只读取 Length 字节，不碰下一帧数据

---

### 7.2 致命点二：20ms 高频轮询的超时阻塞控制

#### 7.2.1 问题本质

`PlcPoller` 要求 **每 20ms** 调一次 `poll()`。如果 `AsioModbusTcpClient` 的一次读写操作因网络抖动而被阻塞 **500ms**（甚至更久），会直接卡死业务主循环：

```
Timeline（错误场景）:
0ms    poll() → readCoils() → executeTransaction() → future.get() [阻塞等待]
       ...
500ms  PLC 超时无响应 → socket 操作超时 → 返回 NetworkError
500ms  poll() 返回（已延迟 480ms！下次 poll 本应在 20ms 执行）
520ms  poll() 第二次 → ...
```

**在上位机领域，主循环被挂起 500ms 是不可接受的**：UI 冻结、安全急停无法响应、运动控制失控。

#### 7.2.2 致命错误做法（绝对禁止）

```cpp
// ❌ 致命错误：超时时间过长
struct Config {
    uint32_t timeoutMs = 500;  // ← 这会导致业务主循环卡死 500ms！
};
```

#### 7.2.3 正确做法：短超时 + 快速失败策略

**铁律：底层 socket 超时时间必须 ≤ 15ms。宁可本轮数据报废（`complete = false`），也绝不让业务主循环挂起。**

```
配置原则:
├── timeoutMs = 10~15ms
│   ├── 为什么是 15ms？ 20ms 轮询周期 - 5ms 业务处理余量 = 15ms
│   │   这 5ms 留给：主循环中的其他任务（UI 刷新、运动控制、安全检测）
│   └── 15ms 足够：局域网内 Modbus TCP 往返通常 < 2ms
│       （如果 15ms 内没有收到完整响应，说明网络出现了严重问题）
│
├── 超时发生的处理策略：
│   ├── future.get() 返回 CommunicationResult::NetworkError
│   ├── PlcPoller 收到 result.ok() == false → 本轮 ctx 无新数据
│   ├── 主循环继续执行（不会卡死！）
│   └── 下次 poll() (20ms 后) 正常重试
│
└── 不需要全局重试机制。
    协议的容错由上层 PlcPoller 的周期性轮询自然提供。
    本轮读失败 → 下次自动重试 → 网络恢复后 20ms 内即恢复正常数据流。
```

#### 7.2.4 超时控制实现策略

**方案一（推荐）：Asio 原生异步超时**

```cpp
// 在每个 executeTransaction 中：
m_timer.expires_after(std::chrono::milliseconds(m_config.timeoutMs)); // 15ms
m_timer.async_wait([this](std::error_code ec) {
    if (!ec) {
        // 15ms 到了 → 强制取消所有 socket 操作
        m_socket.cancel();
    }
});
```

- `socket.cancel()` 会触发所有待处理的异步操作以 `operation_aborted` 错误码回调
- `promise.set_value()` 被调用 → `future.get()` 解除阻塞 → 业务线程继续
- 整个周期 ≤ 15ms，绝不超过 `timeoutMs`

**方案二（备选）：同步 socket 超时**

如果必须使用同步阻塞接口，直接在 socket 上设置 SO_RCVTIMEO：

```cpp
// setsockopt 设置毫秒级超时（Windows/Linux 可移植）
struct timeval tv;
tv.tv_sec  = 0;
tv.tv_usec = m_config.timeoutMs * 1000;  // 15ms = 15000μs
setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, 
           (const char*)&tv, sizeof(tv));
```

**注意**：方案二是最后的保底手段。如果使用方案一（Asio async_read + steady_timer），则不需要设置 SO_RCVTIMEO，因为异步超时由定时器精确控制。

#### 7.2.5 验证标准

| 测试场景 | 期望行为 | 验证方法 |
|---------|---------|---------|
| 正常网络 | 往返 < 3ms，成功返回 | 集成测试 |
| 网络延迟 30ms | 15ms 超时触发 → 返回 NetworkError | 使用 `tc qdisc` / 防火墙模拟 |
| PLC 断电 | 连接断开 → 返回 NetworkError | 集成测试 |
| 连续 100 次轮询 | 每次 poll() < 20ms 总耗时 | 压力测试计时 |

---

### 7.3 两个致命点的交互关系

这两个问题并非独立存在，它们在实际运行中会**相互放大**：

```
如果粘包/半包处理错误：
  帧解析失败 → 协议错位 → 所有后续请求/响应永久错位
  → PlcPoller 持续收到 ProtocolError
  → 依赖 20ms 轮询自愈 → 但因为帧边界永久破坏，永远无法恢复！
  
如果超时设置过长：
  单次超时 500ms → 在这 500ms 内累积 25 个 poll() 周期 (500/20=25)
  → UI 25 帧冻结 → 用户感知到"卡死"
  → 如果恰好粘包也发生 → 雪上加霜，上位机彻底失控
```

**因此，这两个致命点必须同时满足：**
1. **两步分帧读取** → 保证不产生伪故障
2. **15ms 短超时** → 保证故障发生时不影响主循环节奏

---

## 8. 实施顺序

### 阶段 1：引入 Asio 依赖 + ModbusTcpFrame（纯函数，无网络）

```
Step 1.1  下载 Standalone Asio 到 external/asio/
Step 1.2  更新 CMakeLists.txt 添加 Asio include path
Step 1.3  编写 ModbusTcpFrame.h（封包/解包接口声明）
Step 1.4  编写 test_modbus_tcp_frame.cpp（TDD）
Step 1.5  实现 ModbusTcpFrame 直到所有测试通过
```

**验证标准**：`ctest -R modbus_tcp_frame` 全部通过（8+ 条用例）

### 阶段 2：AsioModbusTcpClient 基础实现（连接管理）

```
Step 2.1  编写 AsioModbusTcpClient.h（类声明 + Config + 接口）
Step 2.2  实现构造函数/Destructor/start()/stop()
Step 2.3  实现 ensureConnected()（TCP 连接 + 状态机）
Step 2.4  编写 test_asio_modbus_client.cpp（连接生命周期测试）
Step 2.5  运行测试验证连接/断开/重连
```

**验证标准**：
- `StartStop_Lifecycle` 通过
- `ConnectToInvalidHost_ReturnsNetworkError` 通过

### 阶段 3：读写协议实现（核心）

```
Step 3.1  实现 executeTransaction()（异步-同步桥接）
Step 3.2  实现 readCoils() / readHoldingRegisters()
Step 3.3  实现 writeSingleCoil() / writeSingleRegister() / writeMultipleRegisters()
Step 3.4  实现超时 + 异常响应处理
Step 3.5  编写完整集成测试（需 Modbus 模拟器）
```

**验证标准**：
- `WriteSingleCoil_ReturnsSent_OnSuccess` 通过
- `ReadHoldingRegisters_ReturnsPayload_OnSuccess` 通过
- `SocketTimeout_ReturnsTimeout` 通过（用防火墙规则模拟）
- `PlcReturnsException_ReturnsProtocolError` 通过

### 阶段 4：端到端集成验证

```
Step 4.1  编写 test_modbus_tcp_integration.cpp
Step 4.2  模拟 PlcPoller 真实轮询场景（连续 FC01 + FC03 请求）
Step 4.3  模拟 PlcDevice 真实写场景（FC06 + FC16 序列）
Step 4.4  压力测试：1000 次连续请求验证稳定性
Step 4.5  断线重连测试：拔网线 → 恢复 → 验证自动重连
```

**验证标准**：
- 连续 1000 次读写无失败
- 断线重连 < `reconnectIntervalMs` + 1 秒
- 所有 `CommunicationResult` 状态正确映射

### 阶段 5：生产就绪（可选增强）

```
Step 5.1  添加 TRACE 日志（连接状态变化、通讯耗时统计）
Step 5.2  添加 metrics（请求成功率、平均延迟、超时次数）
Step 5.3  添加单元测试覆盖率报告
Step 5.4  内存泄漏检查（Valgrind / Dr. Memory）
```

---

## 附录 A：与现有代码的关系

### A.1 本阶段不需要修改的现有文件

| 文件 | 原因 |
|------|------|
| `IModbusClient.h` | 接口契约已稳定，本阶段是实现 |
| `ISystemDriver.h` | `CommunicationResult` 已定义完整 |
| `PlcPoller.h/.cpp` | 只消费 `IModbusClient`，不感知传输层 |
| `PlcDevice.h` | 同上 |
| `RegisterRegistry.h` | 纯数据模型，与网络无关 |
| `PlcSnapshot.h` | 纯数据模型，与网络无关 |
| `FakePLC.h` | 测试替身，保持原有实现 |

### A.2 本阶段可能需要适配的文件

| 文件 | 可能的变更 | 原因 |
|------|-----------|------|
| `infrastructure/CMakeLists.txt` | 添加 Asio include + 新源文件 + 测试目标 | 编译配置 |
| `tests/CMakeLists.txt` | 添加新测试目标 | 测试配置 |
| `tests/infrastructure/test_fake_plc.cpp` | 可能需要更新 FakeModbusClient 引用 | 如果 FakeModbusClient 定义在此文件中 |

### A.3 未来阶段的前置依赖

本阶段完成后，后续 `ModbusTcpDriver`（实现 `ISystemDriver`）将组合 `AsioModbusTcpClient` + `PlcPoller` + `PlcDevice`：

```cpp
// 未来 ModbusTcpDriver 的简化结构:
class ModbusTcpDriver : public ISystemDriver {
    AsioModbusTcpClient m_client;
    PlcPoller m_poller;
    PlcDevice m_device;

    CommunicationResult send(const SystemCommand& cmd) override {
        return m_device.dispatch(cmd, m_client);  // 通过本阶段的 client 写
    }

    void pollFeedback(SystemContext& ctx) override {
        auto req = m_poller.prepare();
        // 通过本阶段的 client 读
        auto snap = m_poller.execute(req, m_client);
        // 注入 ctx
    }
};
```

---

## 附录 B：常见陷阱与规避

| 陷阱 | 规避措施 |
|------|---------|
| **MBAP Length 计算错误** | Length = UnitID(1) + FC(1) + Data(N)。不含 MBAP 自己的 7 字节。用 TDD 逐字节断言验证。 |
| **Transaction ID 回绕** | `uint16_t` 循环 0→65535。回绕后 TID=0 可能与历史响应碰撞。解决：在 `executeTransaction` 中等待已发出的 TID 完成后再分配相同 TID。 |
| **TCP 粘包/半包（致命）** | ⚠️ 详见 [第 7.1 章](#71-致命点一tcp-粘包--半包帧边界破坏)。铁律：**禁止**使用 `read_some()` 或 `async_read(socket, largeBuf)` 一次性读取所有数据。**必须**先用 `async_read(socket, 7)` 读 MBAP → 解析 Length → 再用 `async_read(socket, Length)` 读 PDU。违反此规则将导致帧边界永久错位，通讯层瘫痪。 |
| **轮询超时阻塞（致命）** | ⚠️ 详见 [第 7.2 章](#72-致命点二20ms-高频轮询的超时阻塞控制)。铁律：`timeoutMs` **必须 ≤ 15ms**。如果设置为 500ms，一次网络抖动即可卡死业务主循环 25 个周期，导致 UI 冻结、急停无响应。超时后立即返回 `NetworkError`，由 PlcPoller 的下次轮询自然重试。 |
| **Big-Endian 字节序** | Modbus 协议使用 Big-Endian。`buildReadCoils` 中的 `startAddr` 和 `count` 需要 `htons()` 或手动字节交换。建议封装 `writeUint16BE()` 工具函数。 |
| **io_context 提前退出** | 使用 `asio::io_context::work`（C++11 兼容）或 `asio::executor_work_guard`（C++17）保持 `run()` 不退出。 |
| **析构时 socket 未关闭** | `~AsioModbusTcpClient()` 中先调用 `stop()` → `m_socket.close()` → `m_worker.join()`。严格按此顺序。 |
| **异常响应的 TID 校验** | 即使收到异常响应（FC+0x80），TID 仍必须匹配请求的 TID。否则可能是延迟到达的旧响应。 |

---

> **文档版本**: v1.0  
> **生成日期**: 2026-05-27  
> **适用架构**: servingV6 — ProtocolRuntime P4 + Infrastructure 五层架构  
> **状态**: 实施指导（非最终代码）
