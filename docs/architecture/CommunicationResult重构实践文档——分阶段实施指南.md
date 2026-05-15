# CommunicationResult 重构实践文档 —— 分阶段实施指南

> 版本: v1.0
> 日期: 2026-05-15
> 基于: 《ISystemDriver重构设计说明——Cline分析方案.md》v2.0
> 变更: 将 CommunicationResult 从 3 态细化为 7 态，完整覆盖 Modbus TCP 工业现场的错误层级

---

## 目录

1. [设计原理：工业现场 Modbus TCP 的三层错误模型](#1-设计原理工业现场-modbus-tcp-的三层错误模型)
2. [CommunicationResult 最终设计](#2-communicationresult-最终设计)
3. [各状态与工业现场场景的精确映射](#3-各状态与工业现场场景的精确映射)
4. [分阶段实施计划总览](#4-分阶段实施计划总览)
5. [阶段 A：CommunicationResult 定义与 ISystemDriver 接口修改](#5-阶段-acommunicationresult-定义与-isystemdriver-接口修改)
6. [阶段 B：FakeAxisDriver 适配](#6-阶段-bfakeaxisdriver-适配)
7. [阶段 C：UseCaseError 扩展与所有 UseCase 修改](#7-阶段-cuseCaseError-扩展与所有-useCase-修改)
8. [阶段 D：ISystemDriver::pollFeedback 引入](#8-阶段-disystemdriverpollfeedback-引入)
9. [阶段 E：测试适配](#9-阶段-e测试适配)
10. [阶段 F：生产 Modbus TCP 驱动实现指南](#10-阶段-f生产-modbus-tcp-驱动实现指南)
11. [附录 A：文件变更清单](#附录-a文件变更清单)
12. [附录 B：状态机决策表](#附录-b状态机决策表)

---

## 1. 设计原理：工业现场 Modbus TCP 的三层错误模型

### 1.1 为什么 3 态不够用

原设计方案中的 3 态：

```cpp
enum class Status { Sent, Failed, Busy };
```

存在以下问题：

| 场景 | 原 3 态表达 | 缺失的信息 |
|------|-----------|-----------|
| 网线断开 | `Failed` | 无法区分"网线断了"还是"超时"，二者处理策略不同 |
| TCP connect 失败 | `Failed` | 与 Socket 写超时混在一起，UI 无法展示针对性信息 |
| PLC 忙 (0x06) | `Busy` | 缺少与其他 Modbus Exception 的区分 |
| 非法地址 (0x02) | 无表达 | 这是编程错误，应该走完全不同的处理路径 |
| 返回数据非法 | 无表达 | CRC 通过但数据语义不对，这既不是 `Failed` 也不是 `Busy` |
| 当前未连接 | 无表达 | 与 `Failed` 不同——主动断线和被动断线语义不同 |

### 1.2 工业现场的真实错误层级

```
Qt 应用层
    ↓
ISystemDriver::send()
    ↓
Modbus TCP Client (libmodbus / QModbusTcpClient)
    ↓
TCP Socket (OS 内核)
    ↓  ← 层级 1: 网络层错误
    │   ├─ 网线断开 → ECONNRESET / EHOSTUNREACH
    │   ├─ PLC 掉电 → ETIMEDOUT / ECONNREFUSED
    │   ├─ socket connect() 失败 → EHOSTUNREACH
    │   └─ TCP RST → ECONNRESET
    │
    ↓  ← 层级 2: 传输层超时
    │   ├─ PLC 扫描周期过长 → write()/read() 超时
    │   ├─ 交换机抖动 → 丢包 → 超时重传失败
    │   └─ PLC 忙但不返回异常 → 静默超时
    │
    ↓  ← 层级 3: Modbus 应用层
    │   ├─ 0x01 Illegal Function → ProtocolError
    │   ├─ 0x02 Illegal Address  → ProtocolError
    │   ├─ 0x03 Illegal Value    → ProtocolError
    │   └─ 0x06 Device Busy      → Busy
    │
    ↓  ← 层级 4: 数据语义层
        ├─ CRC 通过但数据格式不对 → InvalidResponse
        └─ 返回值超出预期范围    → InvalidResponse
```

**核心认知**：

- **NetworkError ≠ Timeout**：网线断开是"永久性"故障，Timeout 是"暂时性"故障。断开后不应重试，超时后应该重试。
- **ProtocolError ≠ Busy**：Busy 是可重试的（等待 PLC 空闲），其他 Exception Code 是编程/配置错误，不应重试。
- **Disconnected ≠ NetworkError**：Disconnected 是主动/已知的未连接状态，NetworkError 是意外断连。

---

## 2. CommunicationResult 最终设计

### 2.1 完整定义

```cpp
/// @brief 通讯层结果 — 精确表达 Modbus TCP 通讯的每一类失败
///
/// 设计原则:
///   1. 不表达 PLC 执行结果（层级 2）—— 那是 pollFeedback 的职责
///   2. 不表达物理动作状态（层级 3）—— 那是 pollFeedback 的职责
///   3. 只表达"命令是否成功写入 PLC 的寄存器"
///
/// 错误层级（从低到高）:
///   L1 — 网络层:    NetworkError  (socket 断连/拒绝，不可重试)
///   L2 — 传输层:    Timeout       (超时，可重试)
///   L3 — Modbus层:  ProtocolError (异常响应，保留 exceptionCode)
///                   Busy           (PLC忙，可重试)
///   L4 — 数据层:    InvalidResponse (数据非法，不可重试)
///   L0 — 会话层:    Disconnected   (当前未连接，不可重试)
///
struct CommunicationResult {

    enum class Status {

        /// 成功写入 PLC 寄存器（收到正常 Modbus 响应，无异常码）
        Sent,

        /// TCP 连接失败 / 网线断开 / PLC 掉电 / Socket 错误
        /// 包括: ECONNRESET, ECONNREFUSED, EHOSTUNREACH, ENETUNREACH
        NetworkError,

        /// 通讯超时（Socket write/read 超时，交换机抖动，PLC 扫描周期过长）
        /// 注意: 超时不等于断线，超时后网络可能立即恢复
        Timeout,

        /// PLC 忙（Modbus Exception Code 0x06 — Server Device Busy）
        /// 注意: 这是可重试的——稍等 PLC 空闲后重发
        Busy,

        /// Modbus 异常响应（非 Busy 的其他 Exception Code）
        /// 如: 0x01 Illegal Function, 0x02 Illegal Address, 0x03 Illegal Value
        /// 保留 exceptionCode 供诊断
        ProtocolError,

        /// 返回数据非法（CRC 通过但数据格式/语义不符合预期）
        /// 如: 响应长度错误、寄存器值超出物理范围
        InvalidResponse,

        /// 当前未连接（驱动处于未初始化/已断开状态）
        /// 与 NetworkError 的区别: Disconnected 是已知的未连接状态，
        /// NetworkError 是通信尝试中发生的意外断连
        Disconnected
    };

    Status status = Status::Sent;

    /// Modbus Exception Code（仅在 ProtocolError 时有效）
    /// 0x01 = Illegal Function
    /// 0x02 = Illegal Address
    /// 0x03 = Illegal Value
    /// 0x06 = Device Busy（此时 status 为 Busy 而非 ProtocolError）
    int exceptionCode = 0;

    /// 诊断信息（用于日志/UI，不参与控制流）
    /// 例: "192.168.1.100:502 connect failed: ECONNREFUSED"
    std::string diagnostic;

    // ==================== 便捷判断方法 ====================

    [[nodiscard]]
    bool ok() const {
        return status == Status::Sent;
    }

    /// @brief 是否可重试（工业现场决策核心）
    ///
    /// 可重试:
    ///   - Timeout: PLC 可能只是忙，稍后重试
    ///   - Busy:    PLC 显式告知忙，等待后重试
    ///
    /// 不可重试:
    ///   - NetworkError:    网线断开/掉电，重试无意义
    ///   - ProtocolError:   编程/配置错误，重试同样会失败
    ///   - InvalidResponse: 数据格式问题，重试无意义
    ///   - Disconnected:    未连接，需先建立连接
    ///
    [[nodiscard]]
    bool retryable() const {
        switch (status) {
            case Status::Timeout:
            case Status::Busy:
                return true;
            default:
                return false;
        }
    }

    /// @brief 是否为网络相关问题（用于统一处理网络层）
    [[nodiscard]]
    bool isNetworkIssue() const {
        switch (status) {
            case Status::NetworkError:
            case Status::Timeout:
            case Status::Disconnected:
                return true;
            default:
                return false;
        }
    }

    /// @brief 是否为 Modbus 协议层问题（不含 Busy）
    [[nodiscard]]
    bool isProtocolIssue() const {
        return status == Status::ProtocolError;
    }
};
```

### 2.2 与原 3 态设计的对比

| 维度 | 原 3 态设计 | 新 7 态设计 |
|------|-----------|-----------|
| 网线断开 | `Failed` | `NetworkError` + `diagnostic: "ECONNRESET"` |
| TCP 连接失败 | `Failed` | `NetworkError` + `diagnostic: "ECONNREFUSED"` |
| 超时 | `Failed` | `Timeout` |
| PLC 忙 | `Busy` | `Busy`（不变） |
| 非法功能码 | 无表达 | `ProtocolError` + `exceptionCode: 0x01` |
| 非法地址 | 无表达 | `ProtocolError` + `exceptionCode: 0x02` |
| 数据异常 | 无表达 | `InvalidResponse` |
| 未连接 | 无表达 | `Disconnected` |
| retryable 条件 | `Timeout` / `Busy` | `Timeout` / `Busy`（不变） |
| 状态数量 | 3 | 7 |

---

## 3. 各状态与工业现场场景的精确映射

### 3.1 Status::Sent

```cpp
// Modbus 层面:
//   → 发送功能码 0x06 (Write Single Register) 或 0x10 (Write Multiple Registers)
//   → PLC 返回正常响应（功能码回显，无异常码）
//   → CRC 校验通过
```

### 3.2 Status::NetworkError

| 现场场景 | OS 错误 | diagnostic 示例 |
|---------|---------|----------------|
| 网线物理断开 | `ECONNRESET` | `"192.168.1.100:502 write failed: ECONNRESET"` |
| PLC 掉电 | `ECONNREFUSED` / `EHOSTUNREACH` | `"192.168.1.100:502 connect failed: EHOSTUNREACH"` |
| Socket connect() 失败 | `EHOSTUNREACH` | `"192.168.1.100:502 connect failed: EHOSTUNREACH"` |
| 交换机断电 | `ENETUNREACH` | `"192.168.1.100:502 no route to host"` |
| TCP RST 包 | `ECONNRESET` | `"192.168.1.100:502 connection reset by peer"` |

**处理策略**：不可重试，提示用户检查物理连接，UI 可显示"网络断开"图标。

### 3.3 Status::Timeout

| 现场场景 | 原因 | diagnostic 示例 |
|---------|------|----------------|
| PLC 扫描周期过长 | PLC 程序复杂，扫描周期 50ms+ | `"192.168.1.100:502 write timeout after 1000ms"` |
| 交换机抖动 | 瞬时丢包，TCP 重传超时 | `"192.168.1.100:502 write timeout after 1000ms"` |
| PLC 处理繁忙 | PLC 内部队列满，未及时响应 | `"192.168.1.100:502 read timeout after 1000ms"` |
| 网络拥塞 | 带宽被占用，延迟飙升 | `"192.168.1.100:502 write timeout after 1000ms"` |

**处理策略**：可重试（retryable = true）。工业现场中 Timeout 非常常见，不等于断线。UI 可显示"通讯超时，正在重试..."。

**关键区分**：

```
网线断开：拔线的瞬间 → ECONNRESET → NetworkError（不应重试）
PLC 太忙：扫描周期 50ms → 超时 → Timeout（应重试，50ms 后可能就正常了）

两者在现象上可能都表现为"发送后没收到响应"，
但底层 OS 错误码完全不同：
  - NetworkError: send()/connect() 直接返回 -1，errno 是明确的网络错误码
  - Timeout:       send()/recv() 阻塞超时（select/poll/epoll 返回超时事件）
```

### 3.4 Status::Busy

| 现场场景 | Modbus 响应 | diagnostic 示例 |
|---------|-----------|----------------|
| PLC 处于忙碌状态 | Exception 0x06 | `"PLC busy (Exception 0x06)"` |
| 伺服驱动器忙 | Exception 0x06 | `"Device busy (Exception 0x06)"` |

**处理策略**：可重试（retryable = true）。与 Timeout 不同，Busy 是 PLC **显式告知**的忙状态——TCP 通信是成功的，但 PLC 拒绝执行。

**为什么 Busy 不是 ProtocolError？**

因为 Busy 是**暂时性**的运行时状态，且处理策略完全不同（重试 vs 报错）。而 ProtocolError（如 0x02 Illegal Address）是**永久性**的编程/配置错误。

### 3.5 Status::ProtocolError

| 现场场景 | Modbus Exception | diagnostic 示例 |
|---------|-----------------|----------------|
| 功能码不支持 | 0x01 | `"Modbus Exception 0x01: Illegal Function"` |
| 寄存器地址非法 | 0x02 | `"Modbus Exception 0x02: Illegal Address at 0x1000"` |
| 写入值非法 | 0x03 | `"Modbus Exception 0x03: Illegal Value (target: 99999)"` |
| 从站设备故障 | 0x04 | `"Modbus Exception 0x04: Slave Device Failure"` |

**处理策略**：不可重试（retryable = false）。这是编程/配置错误，应该立即停止操作并上报。`exceptionCode` 字段保留原始 Modbus 异常码。

### 3.6 Status::InvalidResponse

| 现场场景 | 原因 | diagnostic 示例 |
|---------|------|----------------|
| 响应字节数不符 | 请求写 2 字节，响应只有 1 字节 | `"Invalid response length: expected 8, got 5"` |
| 响应功能码不匹配 | 发出 0x10，响应 0x90 | `"Response function code mismatch: sent 0x10, got 0x90"` |
| 数据语义非法 | 寄存器值 0xFFFF 但只允许 0~10000 | `"Response data out of range: value 65535"` |

**处理策略**：不可重试。CRC 通过了说明物理链路正常，但数据格式/语义有问题。

### 3.7 Status::Disconnected

| 现场场景 | 原因 | diagnostic 示例 |
|---------|------|----------------|
| 驱动未初始化 | 尚未调用 connectToHost() | `"Not connected: driver not initialized"` |
| 主动断开 | 用户点击"断开连接" | `"Not connected: disconnected by user"` |
| 连接关闭 | 上次操作后 socket 被关闭 | `"Not connected: socket closed"` |

**处理策略**：不可重试。需要先建立连接。

**Disconnected vs NetworkError 的关键区别**：

```cpp
// 场景 1: 连接中发送 → 突然网线断开
auto result = driver.send(cmd);
// result.status == NetworkError  ← 操作过程中发生的意外

// 场景 2: driver.connect() 还没调用
auto result = driver.send(cmd);
// result.status == Disconnected  ← 操作前的已知状态
```

---

## 4. 分阶段实施计划总览

```
阶段 A: CommunicationResult 定义与 ISystemDriver 接口修改
  ├─ 在 infrastructure/ISystemDriver.h 中定义 CommunicationResult
  ├─ ISystemDriver::send() 返回值: void → CommunicationResult
  ├─ 影响: 1 个文件
  └─ 预估: 30 分钟

阶段 B: FakeAxisDriver 适配
  ├─ FakeAxisDriver::send() 返回值: void → CommunicationResult
  ├─ 实现 driver 状态跟踪（connected 标记）
  └─ 预估: 20 分钟

阶段 C: UseCaseError 扩展与所有 UseCase 修改
  ├─ UseCaseError variant 增加 CommunicationResult
  ├─ 修改 7 个 UseCase 的 send() 调用点
  ├─ 影响: 8 个文件
  └─ 预估: 1.5 小时

阶段 D: ISystemDriver::pollFeedback 引入
  ├─ 接口新增纯虚方法 pollFeedback(SystemContext&)
  ├─ FakeAxisDriver 实现 pollFeedback
  ├─ 影响: 2 个文件
  └─ 预估: 1 小时

阶段 E: 测试适配
  ├─ test_system_integration.cpp: syncA() → pollFeedback()
  ├─ 新增 CommunicationResult 相关测试
  ├─ 影响: 2-3 个测试文件（可选扩展）
  └─ 预估: 1.5 小时

阶段 F: 生产 Modbus TCP 驱动实现
  ├─ 新建 ModbusTcpAxisDriver
  ├─ 完整实现 send() + pollFeedback()
  └─ 预估: 2-3 天（含硬件调试）
```

---

## 5. 阶段 A：CommunicationResult 定义与 ISystemDriver 接口修改

### 5.1 目标

1. 在 `ISystemDriver.h` 中定义完整的 `CommunicationResult` 结构体
2. 将 `send()` 返回值从 `void` 改为 `CommunicationResult`
3. 在接口文档中描述 `pollFeedback()` 方法（纯虚定义，阶段 D 实现）

### 5.2 文件变更

| 文件 | 变更类型 | 内容 |
|------|---------|------|
| `infrastructure/ISystemDriver.h` | 修改 | 新增 CommunicationResult 定义、修改 send() 签名、新增 pollFeedback() 声明 |

### 5.3 具体实施

**步骤 1**：打开 `infrastructure/ISystemDriver.h`

**步骤 2**：在文件开头（`#pragma once` 和现有 include 之后）添加 `CommunicationResult` 结构体定义（完整代码见 §2.1）

**步骤 3**：修改 `ISystemDriver` 类，将：

```cpp
virtual void send(const SystemCommand& cmd) = 0;
```

改为：

```cpp
/// @brief 向硬件发送一个统一命令
/// @return CommunicationResult — 表达通讯帧是否成功送达 PLC
virtual CommunicationResult send(const SystemCommand& cmd) = 0;
```

**步骤 4**：在 `send()` 声明之后，新增 `pollFeedback()` 纯虚方法声明：

```cpp
/// @brief 从硬件拉取反馈并分发给 SystemContext 内的所有领域实体
///
/// 每主循环周期调用一次（典型周期: 10ms）
///
/// 内部执行:
///   1. Read:  从硬件读取当前状态
///   2. Translate: 硬件数据 → 领域反馈结构体
///   3. Dispatch: 注入 axis->applyFeedback() / emergencyStopController.applyFeedback() / ...
///
/// 失败策略: 通信失败时保留上次已知反馈值，不更新，记 TRACE_WARN
///
/// @param ctx 目标分组上下文
virtual void pollFeedback(SystemContext& ctx) = 0;
```

**步骤 5**：需要新增 include：
- `#include <string>`（用于 `std::string diagnostic`）
- 前向声明 `class SystemContext;`（pollFeedback 参数）

### 5.4 阶段 A 完成后 ISystemDriver.h 代码

```cpp
#pragma once

#include "domain/command/SystemCommand.h"
#include <string>

class SystemContext;  // 前向声明

/**
 * @brief 通讯层结果 — 精确表达 Modbus TCP 通讯的每一类失败
 *
 * 层级划分:
 *   L1 — 网络层:    NetworkError   (socket 断连/拒绝，不可重试)
 *   L2 — 传输层:    Timeout        (超时，可重试)
 *   L3 — Modbus层:  ProtocolError  (异常响应，保留 exceptionCode)
 *                    Busy           (PLC忙，可重试)
 *   L4 — 数据层:    InvalidResponse (数据非法，不可重试)
 *   L0 — 会话层:    Disconnected   (当前未连接，不可重试)
 */
struct CommunicationResult {

    enum class Status {

        /// 成功写入 PLC 寄存器
        Sent,

        /// TCP 连接失败 / 网线断开 / PLC 掉电 / Socket 错误
        NetworkError,

        /// 通讯超时
        Timeout,

        /// PLC 忙（Modbus Exception Code 0x06 — Server Device Busy）
        Busy,

        /// Modbus 异常响应（0x01/0x02/0x03/0x04）
        ProtocolError,

        /// 返回数据非法
        InvalidResponse,

        /// 当前未连接
        Disconnected
    };

    Status status = Status::Sent;

    /// Modbus Exception Code（仅在 ProtocolError 时有效）
    int exceptionCode = 0;

    /// 日志/界面诊断信息
    std::string diagnostic;

    [[nodiscard]]
    bool ok() const {
        return status == Status::Sent;
    }

    [[nodiscard]]
    bool retryable() const {
        switch (status) {
            case Status::Timeout:
            case Status::Busy:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]]
    bool isNetworkIssue() const {
        switch (status) {
            case Status::NetworkError:
            case Status::Timeout:
            case Status::Disconnected:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]]
    bool isProtocolIssue() const {
        return status == Status::ProtocolError;
    }
};

/**
 * @brief 工业控制系统驱动的统一接口
 *
 * 设计原则:
 *   1. Command / Feedback 双通路: send() 发命令，pollFeedback() 收反馈。
 *   2. send() 返回通讯结果（层级 1）— 只表达"帧是否送达"，不表达"PLC 是否执行"。
 *   3. pollFeedback() 是主动拉取 — 负责层级 2/3 的物理状态回传。
 */
class ISystemDriver {
public:
    virtual ~ISystemDriver() = default;

    // ===== 命令通路 =====

    /// @brief 向硬件发送一个统一命令
    /// @return CommunicationResult — 只表达通讯帧是否成功送达 PLC
    virtual CommunicationResult send(const SystemCommand& cmd) = 0;

    // ===== 反馈通路 =====

    /// @brief 从硬件拉取反馈并分发给 SystemContext 内的所有领域实体
    ///
    /// 每主循环周期调用一次（典型周期: 10ms）
    ///
    /// 失败策略: 通信失败时保留上次已知反馈值，不更新
    ///
    /// @param ctx 目标分组上下文，反馈将注入其内部领域实体
    virtual void pollFeedback(SystemContext& ctx) = 0;
};
```

---

## 6. 阶段 B：FakeAxisDriver 适配

### 6.1 目标

1. `send()` 返回值从 `void` 改为 `CommunicationResult`
2. 增加连接状态跟踪（`m_connected`），支持 `Disconnected` 状态
3. 实现 `pollFeedback()` 方法

### 6.2 文件变更

| 文件 | 变更类型 | 内容 |
|------|---------|------|
| `infrastructure/FakeAxisDriver.h` | 修改 | 返回值适配、新增 pollFeedback 实现 |

### 6.3 具体实施

**步骤 1**：将 `send()` 的返回值类型从 `void` 改为 `CommunicationResult`

```cpp
// 修改前
void send(const SystemCommand& cmd) override {
    std::visit([this](auto&& c) { handle(c); }, cmd);
}

// 修改后
CommunicationResult send(const SystemCommand& cmd) override {
    if (!m_connected) {
        return CommunicationResult{
            CommunicationResult::Status::Disconnected,
            0,
            "Fake driver not connected"
        };
    }
    std::visit([this](auto&& c) { handle(c); }, cmd);
    return CommunicationResult{};  // Fake 驱动永远通讯成功
}
```

**步骤 2**：添加连接状态成员和测试辅助方法

```cpp
// 新增私有成员（放在 FakePLC& m_plc; 附近）
bool m_connected = true;  // Fake 驱动默认已连接

// 新增公开方法（放在 send() 之后）
/// @brief 模拟网络断开（测试用）
void disconnect() { m_connected = false; }

/// @brief 模拟网络恢复（测试用）
void connect() { m_connected = true; }
```

**步骤 3**：实现 `pollFeedback()`

```cpp
void pollFeedback(SystemContext& ctx) override {
    // 1. 推进硬件模拟一个周期
    m_plc.tick(10);

    // 2. 读取所有反馈并通过 applyFeedback 注入
    constexpr std::array<AxisId, 6> ALL_AXIS_IDS = {
        AxisId::X, AxisId::X1, AxisId::X2, AxisId::Y, AxisId::Z, AxisId::R
    };
    for (auto& axisId : ALL_AXIS_IDS) {
        Axis* axis = nullptr;
        ContextRejection r;
        if (ctx.tryGetAxis(axisId, axis, r) && axis) {
            axis->applyFeedback(m_plc.getFeedback(axisId));
        }
    }
    // 3. 注入安全状态反馈
    ctx.emergencyStopController().applyFeedback(m_plc.isEmergencyStopped());
    // 4. 注入龙门状态反馈
    ctx.gantryCouplingController().applyFeedback(m_plc.getGantryFeedback());
}
```

**注意**: `pollFeedback()` 需要 include `domain/entity/SystemContext.h`，需要确保 `FakeAxisDriver.h` 已包含或在 `.cpp` 中实现。

---

## 7. 阶段 C：UseCaseError 扩展与所有 UseCase 修改

### 7.1 目标

1. 在 `UseCaseError` variant 中增加 `CommunicationResult`
2. 修改所有 UseCase 中的 `drv->send()` 调用点，检查返回的 `CommunicationResult`

### 7.2 文件变更清单

| 文件 | 变更内容 |
|------|---------|
| `application/UseCaseError.h` | variant 增加 `CommunicationResult` |
| `application/axis/EnableUseCase.h` | `send()` 后检查 `ok()` |
| `application/axis/JogAxisUseCase.h` | `send()` 后检查 `ok()`（含 stop 方法） |
| `application/axis/MoveAbsoluteUseCase.h` | `send()` 后检查 `ok()` |
| `application/axis/MoveRelativeUseCase.h` | `send()` 后检查 `ok()` |
| `application/axis/StopAxisUseCase.h` | `send()` 后检查 `ok()` |
| `application/safety/EmergencyStopUseCase.h` | `send()` 后检查 `ok()` |
| `application/safety/ReleaseEmergencyStopUseCase.h` | `send()` 后检查 `ok()` |

### 7.3 UseCaseError.h 修改

```cpp
// 新增 include
#include "infrastructure/ISystemDriver.h"  // CommunicationResult

using UseCaseError = std::variant<
    std::monostate,          // 成功（领域规则通过 + 通讯送达）
    ContextRejection,        // 分组/轴查找失败
    RejectionReason,         // 领域规则拒绝（命令未生成，未发送）
    CommunicationResult,     // 通讯失败（命令已生成但未送达）
    GantryRejection,         // 龙门操作拒绝
    SafetyRejection          // 安全操作拒绝
>;
```

### 7.4 UseCase 修改模板（以 EnableUseCase 为例）

**修改前**：

```cpp
if (axis->hasPendingCommand()) {
    if (auto* drv = group->driver()) {
        drv->send(AxisCommandWithId{axisId, axis->getPendingCommand()});
    }
}
```

**修改后**：

```cpp
if (axis->hasPendingCommand()) {
    if (auto* drv = group->driver()) {
        auto commResult = drv->send(AxisCommandWithId{axisId, axis->getPendingCommand()});
        if (!commResult.ok()) {
            TRACE_WARN(LogLayer::APP, "EnableUC",
                       "Send failed for axis {}, status: {}, diagnostic: {}",
                       axisId_to_string(axisId),
                       static_cast<int>(commResult.status),
                       commResult.diagnostic);
            return commResult;  // 通讯失败作为 UseCaseError 返回
        }
    }
}
```

### 7.5 各 UseCase 特殊处理

#### EnableUseCase.h

- `send()` 调用点：1 处（阶段 3）
- 修改：增加 `CommunicationResult` 检查 + 日志

#### JogAxisUseCase.h

- `send()` 调用点：2 处（`execute()` 阶段 3 + `stop()`）
- `stop()` 方法返回 `void`，通讯失败时仅记日志不改变签名（停止是安全操作，不应因通讯失败而终止）

```cpp
// stop() 方法中的修改
if (axis->stopJog(dir)) {
    if (auto* drv = group->driver()) {
        auto commResult = drv->send(AxisCommandWithId{axisId, axis->getPendingCommand()});
        if (!commResult.ok()) {
            LOG_WARN(LogLayer::APP, "JogUC",
                     "Stop send failed for axis {}, diagnostic: {}",
                     axisId_to_string(axisId), commResult.diagnostic);
            // stop 方法不返回错误，仅记录日志
        }
    }
}
```

#### MoveAbsoluteUseCase.h / MoveRelativeUseCase.h

- `send()` 调用点：各 1 处
- 修改：同模板，增加 `CommunicationResult` 检查

#### StopAxisUseCase.h

- `send()` 调用点：1 处
- 修改：同模板。注意 StopAxisUseCase 在领域层不可拒绝，但通讯层可能失败，需要返回 `CommunicationResult`

#### EmergencyStopUseCase.h / ReleaseEmergencyStopUseCase.h

- `send()` 调用点：各 1 处，使用 `controller.popPendingCommand()`
- 修改：同模板

```cpp
// EmergencyStopUseCase 修改后
if (controller.hasPendingCommand()) {
    if (auto* drv = group->driver()) {
        auto commResult = drv->send(controller.popPendingCommand());
        if (!commResult.ok()) {
            TRACE_WARN(LogLayer::APP, "EmergencyStopUC",
                       "Send failed, status: {}, diagnostic: {}",
                       static_cast<int>(commResult.status),
                       commResult.diagnostic);
            return commResult;
        }
    }
}
```

---

## 8. 阶段 D：ISystemDriver::pollFeedback 引入

### 8.1 目标

在 `ISystemDriver` 中增加 `pollFeedback()` 纯虚方法，并在 `FakeAxisDriver` 中实现。

### 8.2 说明

此阶段在阶段 A 中已经完成了接口定义（`pollFeedback` 纯虚方法声明），阶段 B 中完成了 `FakeAxisDriver` 的实现。阶段 D 的内容实质上已分布在前两个阶段中，不需要额外文件变更。

如果希望分两次提交（先改 `send()` 返回值，再增加 `pollFeedback()`），则需要在阶段 A 时暂时不添加 `pollFeedback()` 声明，到阶段 D 再添加。

---

## 9. 阶段 E：测试适配

### 9.1 受影响测试文件

| 文件 | 影响程度 | 说明 |
|------|---------|------|
| `tests/infrastructure/test_system_integration.cpp` | ⚠️ 中等 | `syncA()` 手动泵送 → `pollFeedback()` |
| `tests/application/test_enable_usecase.cpp` | ✅ 无需修改 | FakeDriver 永远返回 `Sent` |
| `tests/application/test_jog_usecase.cpp` | ✅ 无需修改 | 同上 |
| `tests/application/test_move_absolute_usecase.cpp` | ✅ 无需修改 | 同上 |
| `tests/application/test_move_relative_usecase.cpp` | ✅ 无需修改 | 同上 |
| `tests/application/test_stop_usecase.cpp` | ✅ 无需修改 | 同上 |
| `tests/application/safety/test_emergency_stop_usecase.cpp` | ✅ 无需修改 | 同上 |
| `tests/domain/*` | ✅ 无影响 | 领域层不变 |
| 新增测试 | 🆕 可选 | CommunicationResult 通讯失败测试 |

### 9.2 test_system_integration.cpp 修改

修改前（手动泵送模式）：

```cpp
void syncA(AxisId id) {
    Axis* a = nullptr;
    ContextRejection r;
    if (ctxA->tryGetAxis(id, a, r) && a) {
        a->applyFeedback(plcA.getFeedback(id));
    }
}
```

修改后（通过 FakeDriver 的 pollFeedback）：

```cpp
// 在测试夹具中注入驱动
FakePLC plcA;
FakeAxisDriver driverA(plcA);
ctxA->setDriver(&driverA);

// 一行替代手动泵送
driverA.pollFeedback(*ctxA);
```

### 9.3 新增测试（建议）

```cpp
// 测试: 通讯失败应返回 CommunicationResult

TEST(EnableUseCase, ReturnsCommunicationResultWhenDisconnected) {
    SystemManager manager;
    ContextRejection r;
    auto* ctx = manager.createGroup("Group1", r);
    ASSERT_NE(ctx, nullptr);

    FakePLC plc;
    FakeAxisDriver driver(plc);
    driver.disconnect();  // 模拟未连接状态
    ctx->setDriver(&driver);

    auto error = EnableUseCase::execute(manager, "Group1", AxisId::X, true);

    ASSERT_TRUE(std::holds_alternative<CommunicationResult>(error));
    auto& cr = std::get<CommunicationResult>(error);
    EXPECT_EQ(cr.status, CommunicationResult::Status::Disconnected);
}
```

---

## 10. 阶段 F：生产 Modbus TCP 驱动实现指南

### 10.1 类设计

```cpp
class ModbusTcpAxisDriver : public ISystemDriver {
public:
    ModbusTcpAxisDriver(const std::string& host, uint16_t port);

    CommunicationResult send(const SystemCommand& cmd) override;
    void pollFeedback(SystemContext& ctx) override;

private:
    std::string m_host;
    uint16_t m_port;
    bool m_connected = false;

    // --- 内部工具方法 ---

    CommunicationResult ensureConnected();
    CommunicationResult writeRegister(uint16_t address, uint16_t value);
    CommunicationResult writeMultipleRegisters(uint16_t startAddress, const std::vector<uint16_t>& values);
};
```

### 10.2 send() 实现的核心逻辑（伪代码）

```cpp
CommunicationResult ModbusTcpAxisDriver::send(const SystemCommand& cmd) {

    // 步骤 1: 确保连接
    if (!m_connected) {
        return {Status::Disconnected, 0, "Not connected"};
    }

    // 步骤 2: 验证连接有效性
    auto connResult = ensureConnected();
    if (!connResult.ok()) {
        return connResult;
    }

    // 步骤 3: 翻译 SystemCommand → Modbus 寄存器操作
    // 这里用 std::visit 分发到不同 handle()
    // handle(AxisCommandWithId) → 写目标位置/使能寄存器
    // handle(EmergencyStopCommand) → 写急停命令寄存器

    // 步骤 4: 执行 Modbus 写入
    auto writeResult = writeRegister(address, value);
    return writeResult;  // 直接返回通讯结果
}

CommunicationResult ModbusTcpAxisDriver::writeRegister(uint16_t address, uint16_t value) {
    // 伪代码 — 具体实现取决于 libmodbus / QModbusTcpClient

    // 尝试 modbus_write_register()
    int rc = modbus_write_register(ctx, address, value);

    if (rc == 1) {
        return {Status::Sent};  // 成功
    }

    // 错误分析
    if (errno == ETIMEDOUT) {
        return {Status::Timeout, 0, fmt::format("{}:{} write timeout", m_host, m_port)};
    }
    if (errno == ECONNRESET || errno == ECONNREFUSED || errno == EHOSTUNREACH) {
        m_connected = false;
        return {Status::NetworkError, 0, fmt::format("{}:{} write failed: {}", m_host, m_port, strerror(errno))};
    }

    // 检查是否有 Modbus Exception
    uint8_t exceptionCode = 0;
    if (modbus_get_response_timeout(ctx, &...)) {
        // ...获取 exception code
    }
    if (exceptionCode == 0x06) {
        return {Status::Busy, 0, "PLC busy (Exception 0x06)"};
    }
    if (exceptionCode > 0) {
        return {Status::ProtocolError, exceptionCode,
                fmt::format("Modbus Exception 0x{:02X}", exceptionCode)};
    }

    return {Status::NetworkError, 0, fmt::format("{}:{} unknown error", m_host, m_port)};
}
```

### 10.3 pollFeedback() 实现（伪代码）

```cpp
void ModbusTcpAxisDriver::pollFeedback(SystemContext& ctx) {
    // 1. 批量读取输入寄存器
    auto raw = readInputRegisters(INPUT_REGISTER_BASE, INPUT_REGISTER_COUNT);
    if (!raw.ok) {
        TRACE_WARN("Modbus read failed, keeping last feedback");
        return;  // 保留上次已知值
    }

    // 2. 翻译 → 领域反馈结构体
    DomainFeedbackBatch batch = translateToDomain(raw);

    // 3. 分发
    for (auto& [axisId, fb] : batch.axisFeedbacks) {
        Axis* axis = nullptr;
        ContextRejection r;
        if (ctx.tryGetAxis(axisId, axis, r) && axis) {
            axis->applyFeedback(fb);
        }
    }
    if (batch.plcEmergencyStopped.has_value()) {
        ctx.emergencyStopController().applyFeedback(*batch.plcEmergencyStopped);
    }
    if (batch.gantryFeedback.has_value()) {
        ctx.gantryCouplingController().applyFeedback(*batch.gantryFeedback);
    }
}
```

### 10.4 主循环调用

```cpp
// 在 QML 主循环或 QTimer 回调中
void onTimerTick() {
    for (auto& [name, ctx] : allGroups) {
        if (auto* drv = ctx->driver()) {
            drv->pollFeedback(*ctx);
        }
    }
    // ...更新 UI
}
```

---

## 11. 附录 A：文件变更清单

### 一次完整提交的变更量

| # | 文件 | 操作 | 估计行数变更 |
|---|------|------|------------|
| 1 | `infrastructure/ISystemDriver.h` | 修改 | +120 |
| 2 | `infrastructure/FakeAxisDriver.h` | 修改 | +35 |
| 3 | `application/UseCaseError.h` | 修改 | +3 |
| 4 | `application/axis/EnableUseCase.h` | 修改 | +7 |
| 5 | `application/axis/JogAxisUseCase.h` | 修改 | +13 |
| 6 | `application/axis/MoveAbsoluteUseCase.h` | 修改 | +7 |
| 7 | `application/axis/MoveRelativeUseCase.h` | 修改 | +7 |
| 8 | `application/axis/StopAxisUseCase.h` | 修改 | +7 |
| 9 | `application/safety/EmergencyStopUseCase.h` | 修改 | +7 |
| 10 | `application/safety/ReleaseEmergencyStopUseCase.h` | 修改 | +7 |
| 11 | `tests/infrastructure/test_system_integration.cpp` | 修改 | -15 / +10 |

**总计**: 约 +223 行，-15 行（净增 208 行）

### 分两次提交的建议

**Commit 1**: `send()` 返回值修改（阶段 A + B + C）
- 文件 1-10
- 编译即检查——所有调用点会被编译器强制适配

**Commit 2**: `pollFeedback()` 引入（阶段 D + E）
- 文件 1、2、11
- 可以独立测试反馈通路

---

## 12. 附录 B：状态机决策表

### 12.1 UI 层处理决策

| status | retryable | UI 建议动作 |
|--------|-----------|------------|
| `Sent` | N/A | 正常，检查 pollFeedback 结果 |
| `NetworkError` | ❌ | 显示"网络连接丢失"，提示检查网线/PLC电源 |
| `Timeout` | ✅ | 显示"通讯超时，正在重试..."，自动重试（有限次数） |
| `Busy` | ✅ | 显示"PLC 忙，等待中..."，延迟重试（如 100ms） |
| `ProtocolError` | ❌ | 显示"协议错误：异常码 0xXX"，提示检查程序配置 |
| `InvalidResponse` | ❌ | 显示"数据异常"，触发日志上报 |
| `Disconnected` | ❌ | 显示"未连接"，提示用户点击连接按钮 |

### 12.2 自动重试策略（UseCase 层的可选增强）

```cpp
// 示例: 在 UseCase 中实现自动重试
if (axis->hasPendingCommand()) {
    if (auto* drv = group->driver()) {
        auto commResult = drv->send(AxisCommandWithId{axisId, axis->getPendingCommand()});
        if (!commResult.ok()) {
            if (commResult.retryable()) {
                // 可重试类型: 稍后重发
                TRACE_INFO("Retrying send due to: {}", commResult.diagnostic);
                // 将命令放入重试队列，下一帧重新 send
                m_retryQueue.push({axisId, axis->getPendingCommand()});
                return std::monostate{};  // 不向上报错，静默重试
            } else {
                // 不可重试: 向上报告
                return commResult;
            }
        }
    }
}
```

**注意**: 自动重试是可选的增强功能，可以在阶段 C 之后按需添加。初始实现建议先直接返回 `CommunicationResult` 让 UI 层处理。

---

> **文档状态**: ✅ 完整
> **下一步**: 按阶段 A → B → C → D → E 顺序实施
> **风险等级**: 低（编译期类型检查，Fake 驱动测试不敏感）
> **预估总工时**: 约 8 小时（不含生产驱动实现）
