# ISystemDriver 重构设计说明 -- 基于当前代码库的完整分析方案

> 版本: v2.0  
> 日期: 2026-05-15  
> 范围: Command -> Send -> Poll -> Feedback 闭环，从当前代码到生产可用  
> 变更: v2.0 新增 §1.2 三层成功分析、§1.6 工业现场案例、CommunicationResult 设计

---

## 目录

1. [当前架构的真实状况](#1-当前架构的真实状况)
2. [五个核心问题逐一定性](#2-五个核心问题逐一定性)
3. [设计原理: 工业控制中的 Command / Feedback 双系统](#3-设计原理-工业控制中的-command--feedback-双系统)
4. [推荐的目标架构](#4-推荐的目标架构)
5. [重构阶段路线](#5-重构阶段路线)
6. [关键接口定义](#6-关键接口定义)
7. [测试适配策略](#7-测试适配策略)
8. [未来 EtherCAT / ARM 扩展](#8-未来-ethercat--arm-扩展)
9. [常见质疑与回答](#9-常见质疑与回答)

---

## 1. 当前架构的真实状况

### 1.1 ISystemDriver -- 当前真实代码

```cpp
// infrastructure/ISystemDriver.h（当前真实代码）
class ISystemDriver {
public:
    virtual ~ISystemDriver() = default;
    virtual void send(const SystemCommand& cmd) = 0;
};
```

**现状**: 只有一个 `send()`，无返回值。接口简洁，但存在一个关键表达能力缺陷：**`void` 返回值抹杀了"通讯是否成功"这一层信息**。

### 1.2 send(void) 的深层问题 -- 三层成功被混为一谈

在工业控制系统中，一个命令从发出到执行完成，涉及三个截然不同的"成功"层级：

```
层级 1: 通讯帧到达 PLC
  ├─ Modbus TCP 帧发送成功 + CRC 校验通过 + PLC 返回 ACK
  ├─ 失败场景: Socket 断线、超时、CRC 错误、网络抖动、PLC Busy
  └─ 知道者: Driver（send 调用瞬间即可知道）
       
层级 2: PLC 接受并执行命令
  ├─ PLC 将命令写入内部执行队列，伺服驱动器开始响应
  ├─ 失败场景: PLC 逻辑拒绝（如轴不存在）、命令码无效、PLC 内部错误
  └─ 知道者: PLC 状态寄存器 -> 下一个 pollFeedback 周期才能知道
       
层级 3: 物理动作完成
  ├─ 电机真正开始转动 / 使能真正上电 / 急停真正生效
  ├─ 失败场景: 伺服报警、堵转、过载、限位触发
  └─ 知道者: Axis 状态寄存器 -> 持续 pollFeedback 确认
```

**当前 `void send()` 的问题**: 它把层级 1 的信息完全丢弃了。

- 当 `send()` 内部遭遇 `Modbus` 断线，驱动只能默默记日志
- 调用者（UseCase）完全不知道"命令可能根本没发出去"
- 调用者只能赌下一次 `pollFeedback()` 会反映出问题
- 但如果没有 `pollFeedback()`（例如在单次命令场景），这个信息就永远丢失了

**注意**: 这不是说 `send()` 应该返回"PLC 执行成功/失败"（那是层级 2/3 的事），而是说 `send()` 应该返回"通讯帧是否成功送达"。

### 1.3 当前的 Command 流（Send 方向）-- 结构良好，通讯反馈缺失

```
UI / ViewModel
    ↓
UseCase::execute(manager, groupName, axisId, params)
    ↓ 阶段 0: SystemManager::tryGetGroup()        -> ContextRejection
    ↓ 阶段 1: SystemContext::tryGetAxis()          -> ContextRejection (含安全/龙门拦截)
    ↓ 阶段 2: Axis::enable/moveAbsolute/jog/...   -> RejectionReason (领域规则校验)
    ↓ 阶段 3: if (axis->hasPendingCommand())
              drv->send(AxisCommandWithId{axisId, cmd})
    ↓
FakeAxisDriver::send() -> std::visit -> handle() -> plc_.writeRegister()
    ↓
FakePLC 寄存器更新
```

**这段链路的优点**:
- 统一命令总线：`SystemCommand` variant 承载所有命令类型
- 四层拦截：Manager -> Context -> Axis 领域 -> Driver
- UseCase 无状态，可重复调用
- 错误返回类型安全：`UseCaseError = variant<monostate, ContextRejection, RejectionReason, ...>`

**当前链路的结构缺陷**:
- `send()` 返回 `void` -- 通讯是否成功无法表达，UseCase 无法根据通讯状态做决策
- 没有 `pollFeedback()` -- 层级 2/3 的状态无法回传

### 1.4 当前的 Feedback 流（Receive 方向）-- 完全缺失

```
生产环境:   ???  (不存在)
测试环境:   FakePLC::tick() -> syncA() -> axis->applyFeedback(fb)
                                      -> ctx->emergencyStopController().applyFeedback(bool)
                                      -> ctx->gantryCouplingController().applyFeedback(fb)
```

**现状**: 测试中用手动 `syncA(id)` 泵送反馈，这是**正确的测试模式**。但生产环境没有任何对应的基础设施----没有一个**主动拉取 PLC 寄存器、解包、分发给各领域实体**的机制。

### 1.5 关键代码证据

**Axis.h** -- applyFeedback 是反馈的唯一入口：
```cpp
void applyFeedback(const AxisFeedback& feedback);
```

**EmergencyStopController.h** -- feedback-driven 状态机：
```cpp
/// @brief 接收 PLC 的"急停中"状态反馈，驱动本地状态机
/// 这是所有 PLC Feedback 的单一入口
void applyFeedback(bool plcEmergencyStopped);
```

**test_system_integration.cpp** -- 手动泵送模式（测试中正确，生产不可用）：
```cpp
void syncA(AxisId id) {
    Axis* a = nullptr;
    ContextRejection r;
    if (ctxA->tryGetAxis(id, a, r) && a) {
        a->applyFeedback(plcA.getFeedback(id));
    }
}
```

**EnableUseCase.h** -- `send()` 通讯结果丢失的关键点：
```cpp
// 阶段 2：轴领域层状态判定
if (!axis->enable(active)) {
    return axis->lastRejection();  // ← 返回 bool 只代表"领域规则校验通过"
}
// 阶段 3：若产生了待发送命令，通过统一命令总线包装下发
if (axis->hasPendingCommand()) {
    if (auto* drv = group->driver()) {
        drv->send(AxisCommandWithId{axisId, axis->getPendingCommand()});
        // ← void send() -> 通讯失败被静默吞没
    }
}
return std::monostate{};  // ← UI 看到"成功"，但命令可能根本没发出去
```

### 1.6 为什么"通讯成功"不能被忽略 -- 来自工业现场的案例

**场景 A: Modbus 断线后发送使能命令**

```
时间线:
  T0: 用户点击"使能"按钮
  T1: EnableUseCase::execute() -> axis->enable(true) -> 领域规则通过
  T2: drv->send(AxisCommandWithId{Y, EnableCommand}) -> Modbus 写寄存器
  T3: 但此时网线松了！Socket write() 返回 -1
  T4: 当前: send() 返回 void，UseCase 得到 monostate（"成功"）
  T5: 用户看到 UI 显示"使能成功"
  T6: 3 秒后 pollFeedback 读到轴状态仍是 Disabled
  T7: 用户困惑: "我点了使能，为什么轴没反应？"
```

**场景 B: PLC Busy 时发送位置命令**

```
  T0: 上一段运动刚结束，PLC 处于 Busy 清理状态（持续 20ms）
  T1: MoveAbsoluteUseCase::execute() -> 目标校验通过
  T2: drv->send(...) -> Modbus 写寄存器
  T3: PLC Busy -> 返回 Exception Code 0x06 (Server Device Busy)
  T4: 当前: send() 静默吞下这个异常
  T5: 后续 pollFeedback 发现位置没变，但已经不知道是哪次 send 失败
```

**结论**: `send()` 需要返回一个轻量的通讯结果，只表达"命令是否送达 PLC 的寄存器"。这不是让驱动"逆向教导"上层，而是让上层能区分"命令没发出去"和"发出去了但设备没执行"。

---

## 2. 五个核心问题逐一定性

### 问题 0: send(void) 无法表达通讯结果 -- "命令到底发出去了没有？"

**严重程度**: 🔴 阻塞级

**现状**: `send()` 返回 `void`。当 Modbus 断线、Socket 超时、CRC 错误、PLC Busy 时，调用者完全无法区分"命令没发出去"和"发出去了但物理状态没变"。

**分析**: 这本质上是 §1.2 中描述的"层级 1 信息丢失"问题。通讯失败的信息在 `send()` 内部就流失了----驱动只能记日志，UseCase 拿到 `monostate`（成功），UI 显示"操作成功"，但 3 秒后 `pollFeedback` 发现轴纹丝不动。用户困惑："我明明看到成功了？"

**正确的语义边界**:
- `send()` 应该返回 **"通讯帧是否成功送达 PLC"**（层级 1）
- `send()` 绝不应该返回 **"PLC 是否执行成功"**（层级 2）----那是 `pollFeedback` 的职责
- `send()` 更不应该返回 **"物理动作是否完成"**（层级 3）

**推荐方案**: `send()` 返回 `CommunicationResult`，只表达通讯层的结果。详见 §4.1。

### 问题 1: ISystemDriver 没有 receive / feedback 路径

**严重程度**: 🔴 阻塞级

**现状**: `ISystemDriver` 定义了"怎么发"，但没有定义"怎么收"。

**在生产环境中（Modbus TCP 驱动）**:
- 驱动需要周期性地读取 PLC 的输入寄存器（AxisFeedback、急停状态位、龙门联动状态位等）
- 读取到的数据需要被翻译成领域结构体（`AxisFeedback`、`GantryFeedback` 等）
- 翻译后的数据需要注入到领域实体（`Axis::applyFeedback()`、`EmergencyStopController::applyFeedback()` 等）

**当前**: 这三个步骤----拉取（Poll）、翻译（Translate）、分发（Dispatch）----在架构上完全没有定义。

### 问题 2: bool 返回值的语义混淆

**严重程度**: 🟡 设计债务

**现状**: `Axis::enable()` / `Axis::jog()` / `Axis::moveAbsolute()` 等方法返回 `bool`，而 `lastRejection()` 返回具体的错误原因。

**混淆风险**:
- `false` 可能意味着"领域规则不通过"（如状态不匹配）
- `false` 也可能意味着"参数不合法"（如目标超出限位）
- `false` **绝不**意味着"硬件拒绝"----因为硬件反馈根本没回来
- 但 `bool` 的语义天然暗示着"成功/失败"，容易让调用者误以为 `true` = "硬件已经执行了"

**当前补救**: 代码注释已经说明了这一点（"阶段 2：轴领域层状态判定"），但注释是脆弱的----重构或新人接手时很容易被忽略。

### 问题 3: FakeAxisDriver 混合了 Translator 职责

**严重程度**: 🟢 当前低优先级（生产驱动可实现后自然分离）

**现状**: `FakeAxisDriver` 内部通过 `std::visit` + 多个 `handle()` 重载，将 `SystemCommand` 直接映射为 `FakePLC` 的寄存器写入。

```
FakeAxisDriver 当前承担了三件事:
  1. 驱动适配 (Driver)   -- 接收 send() 调用
  2. 协议翻译 (Translator) -- SystemCommand -> PLC 寄存器地址 + 值
  3. 硬件模拟 (FakePLC)   -- 内存驻留的寄存器状态
```

这是"快速验证"阶段的合理设计，但当真正的 Modbus TCP 驱动到来时，Translator 部分应该被提取为独立的、可复用的层。

### 问题 4: 生产反馈环路完全缺失

**严重程度**: 🔴 阻塞级

**现状**: 测试中用 `syncA()` 手动泵送反馈。这在一个有主循环的生产程序中需要被替换为一个**自动化的、周期性的**拉取-翻译-分发循环。

---

## 3. 设计原理: 工业控制中的 Command / Feedback 双系统

### 3.1 为什么不能用简单的 Request-Response 模式

在工业控制中，以下假设是**致命的**:

| 假设 | 真相 |
|------|------|
| "我发了一个使能命令 -> 轴就使能了" | 可能电机驱动器故障、接线松动、急停触发 |
| "我发了位置目标 -> 轴到达了" | 可能堵转、过载、中途报警 |
| "PLC 返回了 OK -> 一切正常" | OK 只代表通信帧校验通过，不代表物理状态 |

**物理真相不来自命令的返回值，而来自持续的反馈信号。**

### 3.2 正确的职责划分

```
┌─────────────────────────────────────────────────────────────────┐
│                      应用层 (Application)                       │
│  UseCase / Orchestrator: 只管"意图表达"和"领域规则校验"          │
│  不关心: 命令怎么发到硬件、反馈怎么从硬件回来                     │
│  send() 返回的 CommunicationResult: 只用于判断"命令发出去了没"    │
└───────────────────────────┬─────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                     领域层 (Domain)                              │
│  Axis / EmergencyStopController / GantryCouplingController      │
│  职责: 意图生成 (produce intent) + 反馈驱动 (apply feedback)      │
│  不关心: 寄存器地址、通信协议、通讯是否成功                       │
└───────────────────────────┬─────────────────────────────────────┘
                            │
                ┌───────────┴───────────┐
                ▼                       ▼
┌───────────────────────┐   ┌───────────────────────────────────┐
│   命令通路 (Send)      │   │    反馈通路 (Poll / Receive)       │
│                       │   │                                   │
│  ISystemDriver::send  │   │  ISystemDriver::pollFeedback      │
│  -> CommunicationResult│   │       │                           │
│       │               │   │       ▼                           │
│       ▼               │   │  Translator::                     │
│  Translator::         │   │    toEntity()                     │
│    toHardware()       │   │       │                           │
│       │               │   │       ▼                           │
│       ▼               │   │  [AxisFeedback / GantryFeedback   │
│  [Modbus TCP 帧]      │   │   / SafetyState / ...]            │
│  [EtherCAT PDO]       │   │       │                           │
│  [CANopen SDO]        │   │       ▼                           │
│                       │   │  dispatch(feedback):              │
│                       │   │    -> axis->applyFeedback()        │
│                       │   │    -> eStop.applyFeedback()        │
│                       │   │    -> coupling.applyFeedback()     │
└───────────────────────┘   └───────────────────────────────────┘
```

**关键原则**:
1. **命令通路**: `send()` 返回通讯是否送达（层级 1）。物理结果通过反馈通路确认（层级 2/3）。
2. **反馈通路**: 主动拉取（Poll）或被动接收（Callback），将硬件状态翻译为领域结构体后注入。
3. **Translator 层**: 双向翻译----命令->硬件协议、硬件数据->领域结构体。
4. **领域实体不关心协议**: `Axis` 只知道 `AxisFeedback`，不知道 Modbus 寄存器地址。

---

## 4. 推荐的目标架构

### 4.1 send() 应该返回"通讯结果"----引入 CommunicationResult

**核心原则**: `send()` 只表达层级 1（通讯帧是否送达），不表达层级 2/3（PLC 执行 / 物理动作完成）。

```cpp
/// @brief 通讯层结果 -- 只表达"帧是否成功送达 PLC 的寄存器"
///
/// 不表达:
///   - PLC 是否接受了该命令（层级 2 -- pollFeedback 才知道）
///   - 物理动作是否执行（层级 3 -- pollFeedback 持续确认）
struct CommunicationResult {
    enum class Status {
        Sent,     // 通讯帧成功写入 PLC 寄存器（收到 ACK/正常响应）
        Failed,   // 通讯失败（Socket 超时、断线、CRC 错误、网络抖动）
        Busy,     // PLC 忙（Modbus Exception 0x06 / 设备忙信号）
    };
    Status status = Status::Sent;
    std::string diagnostic;  // 诊断信息（仅日志用，不参与控制流）
    
    bool ok() const { return status == Status::Sent; }
};
```

**CommunicationResult 在 UseCaseError 中的位置**:

```cpp
// 当前
using UseCaseError = std::variant<
    std::monostate,          // 成功
    ContextRejection,        // 分组/轴查找失败
    RejectionReason,         // 领域规则拒绝
    GantryRejection,         // 龙门操作拒绝
    SafetyRejection          // 安全操作拒绝
>;

// 阶段 1 之后（新增 CommunicationResult）
using UseCaseError = std::variant<
    std::monostate,           // 通讯 + 领域规则全部通过
    ContextRejection,         // 分组/轴查找失败
    RejectionReason,          // 领域规则拒绝（命令未生成，未发送）
    CommunicationResult,      // 领域规则通过，但通讯失败（命令已生成但未送达）
    GantryRejection,          // 龙门操作拒绝
    SafetyRejection           // 安全操作拒绝
>;
```

**为什么 CommunicationResult 不放在 RejectionReason 里？**

因为 `RejectionReason` 是领域层的拒绝理由（状态不匹配、超限位等），而 `CommunicationResult` 是基础设施层的通讯失败。混在一起会导致：
- 领域层引入通讯语义（违反分层）
- 调用者无法区分"我发没发"和"发了但设备不执行"
- 未来换 EtherCAT 驱动时，`Failed` 的语义完全不同

### 4.2 UseCase 中的新调用模式

```cpp
// 当前 EnableUseCase（简化）
auto result = axis->enable(active);
if (!result) return axis->lastRejection();
if (axis->hasPendingCommand()) {
    drv->send(AxisCommandWithId{axisId, axis->getPendingCommand()});
    // ← void send() -> 通讯失败被吞没
}
return std::monostate{};  // ← 调用者以为一切OK

// 阶段 1 之后 EnableUseCase（新）
auto result = axis->enable(active);
if (!result) return axis->lastRejection();
if (axis->hasPendingCommand()) {
    auto commResult = drv->send(AxisCommandWithId{axisId, axis->getPendingCommand()});
    if (!commResult.ok()) return commResult;  // ← 通讯失败明确传出
}
return std::monostate{};
```

**效果**: 如果网线松了，`send()` 返回 `CommunicationResult{Status::Failed, "Socket write error: 10054"}`，UseCase 直接把这个结果返回给 UI 层，UI 可以在 3ms 内显示"通讯失败，请检查网络连接"----而不是等 `pollFeedback` 3 秒后才暴露问题。

### 4.3 扩展后的 ISystemDriver 接口

```cpp
class ISystemDriver {
public:
    virtual ~ISystemDriver() = default;

    // ===== 命令通路 =====

    /// @brief 向硬件发送一个统一命令
    /// @return CommunicationResult -- 只表达通讯帧是否成功送达 PLC 的寄存器
    ///         不表达 PLC 是否执行成功（层级 2）或物理动作是否完成（层级 3）
    /// @throw 不抛异常（所有错误通过返回值表达）
    virtual CommunicationResult send(const SystemCommand& cmd) = 0;

    // ===== 反馈通路 =====

    /// @brief 从硬件拉取反馈并分发给 SystemContext 内的所有领域实体
    ///
    /// 每主循环周期调用一次（典型周期: 10ms）
    ///
    /// 内部执行:
    ///   1. Read:  从硬件读取当前状态
    ///   2. Translate: 将硬件数据翻译为领域反馈结构体
    ///   3. Dispatch: 注入 Axis::applyFeedback / EmergencyStopController::applyFeedback / ...
    ///
    /// 失败策略: 通信失败时保留上次已知反馈值，不更新，记 TRACE_WARN
    ///
    /// @param ctx 目标分组上下文，反馈将注入其内部领域实体
    virtual void pollFeedback(SystemContext& ctx) = 0;
};
```

**为什么 pollFeedback 接受 SystemContext 而非只返回裸数据？**

因为反馈分发是有结构的----AxisFeedback 去 `Axis`，bool 去 `EmergencyStopController`，`GantryFeedback` 去 `GantryCouplingController`。如果驱动只返回一堆原始结构体，上层还需要写一个 Dispatcher。把这个职责放在驱动内部（或 Translator 内部），是因为**只有驱动知道每个寄存器对应哪个领域实体**。

### 4.4 引入 IProtocolTranslator（提取 Translator -- 阶段 2）

```cpp
/// @brief 协议翻译器接口 -- 双向翻译
///
/// 命令方向: SystemCommand -> 硬件协议数据
/// 反馈方向: 硬件协议数据 -> 领域结构体
///
/// 此接口独立于通信层，可被多种物理协议复用:
///   - ModbusTCPTranslator
///   - EtherCATTranslator
///   - CANopenTranslator
class IProtocolTranslator {
public:
    virtual ~IProtocolTranslator() = default;

    // ===== 命令方向 =====

    /// @brief 将领域命令翻译为硬件写操作集合
    virtual HardwareWriteBatch translate(const SystemCommand& cmd) = 0;

    // ===== 反馈方向 =====

    /// @brief 将硬件原始数据翻译为领域反馈集合
    virtual DomainFeedbackBatch translate(const HardwareReadBuffer& raw) = 0;
};
```

**DomainFeedbackBatch 结构**:
```cpp
struct DomainFeedbackBatch {
    // 每轴反馈
    std::map<AxisId, AxisFeedback> axisFeedbacks;

    // 安全状态
    std::optional<bool> plcEmergencyStopped;

    // 龙门状态
    std::optional<GantryFeedback> gantryFeedback;
};
```

### 4.5 Modbus TCP 驱动的典型实现

```cpp
class ModbusTcpAxisDriver : public ISystemDriver {
public:
    ModbusTcpAxisDriver(const std::string& host, uint16_t port,
                        std::unique_ptr<IProtocolTranslator> translator)
        : m_client(host, port), m_translator(std::move(translator))
    {}

    CommunicationResult send(const SystemCommand& cmd) override {
        auto batch = m_translator->translate(cmd);
        for (const auto& op : batch.operations) {
            auto result = m_client.writeRegister(op.address, op.value);
            if (!result.ok()) {
                TRACE_WARN("Modbus write failed: {}", result.error());
                return CommunicationResult{
                    Status::Failed,
                    result.error()
                };
            }
        }
        return CommunicationResult{};  // 全部写入成功
    }

    void pollFeedback(SystemContext& ctx) override {
        // 1. 批量读取所有输入寄存器
        auto raw = m_client.readHoldingRegisters(
            INPUT_REGISTER_BASE, INPUT_REGISTER_COUNT);

        if (!raw.ok) {
            TRACE_WARN("Modbus read failed, keeping last known feedback");
            return;  // 不更新 ---- 保留上次已知的物理状态
        }

        // 2. 翻译
        auto batch = m_translator->translate(raw);

        // 3. 分发
        for (auto& [axisId, fb] : batch.axisFeedbacks) {
            Axis* axis = nullptr;
            ContextRejection r;
            if (ctx.tryGetAxis(axisId, axis, r) && axis) {
                axis->applyFeedback(fb);
            }
        }
        if (batch.plcEmergencyStopped.has_value()) {
            ctx.emergencyStopController().applyFeedback(
                *batch.plcEmergencyStopped);
        }
        if (batch.gantryFeedback.has_value()) {
            ctx.gantryCouplingController().applyFeedback(
                *batch.gantryFeedback);
        }
    }

private:
    ModbusTcpClient m_client;
    std::unique_ptr<IProtocolTranslator> m_translator;
};
```

### 4.6 生产主循环

```cpp
// 生产环境主循环（伪代码）
SystemManager manager;
manager.createGroup("Machine_A", reason);
auto* ctx = manager.getGroup("Machine_A");

// 注入真实驱动
ctx->setDriver(new ModbusTcpAxisDriver("192.168.1.100", 502,
    std::make_unique<ModbusTCPTranslator>()));

while (running) {
    // ===== 命令阶段 =====
    // 处理 UI 事件 -> UseCase 执行 -> send() 写入硬件
    // send() 返回的 CommunicationResult 即时反馈给 UI
    // 例: if (!commResult.ok()) { showError("网络连接失败"); }

    // ===== 反馈阶段 =====
    if (auto* drv = ctx->driver()) {
        drv->pollFeedback(*ctx);  // 一个调用完成拉取->翻译->分发
    }

    // ===== 编排器推进 =====
    // GantryOrchestrator::tick() 检查状态 -> 决定下一步

    sleep_ms(10);  // 可选: 仅在 RTOS / 独立线程中使用
}
```

### 4.7 解决 bool 问题: 引入 CommandResult 替代 bool（阶段 3）

```cpp
// 当前
bool axis->enable(active);           // true/false 语义模糊
RejectionReason axis->lastRejection();

// 推荐
CommandResult axis->enable(active);  // 富类型

struct CommandResult {
    enum class Status {
        Accepted,    // 领域规则校验通过，已生成待发送命令
        Idempotent,  // 已经在目标状态，无操作
        Rejected,    // 领域规则拒绝（原因见 rejection）
    };
    Status status;
    RejectionReason rejection = RejectionReason::None;
    bool hasPendingCommand() const { return status == Status::Accepted; }
};
```

**但这可以放在阶段 3 再做** -- 当前 bool + lastRejection() 模式虽然语义有瑕疵，但**功能完全正确**，且有注释说明。在第一阶段只做 feedback 闭环的建立 + CommunicationResult 引入，因为那才是阻塞问题。

---

## 5. 重构阶段路线

### 阶段 0（当前已具备）: 架构基础 ✅

- [x] ISystemDriver 接口干净（仅 send，无返回值）
- [x] 统一命令总线（SystemCommand variant）
- [x] 领域实体 feedback-driven（Axis、EmergencyStopController、GantryCouplingController 都有 applyFeedback）
- [x] UseCase 无状态 + 四层拦截
- [x] FakePLC + FakeAxisDriver 可用于测试

### 阶段 1: 补齐 Feedback 闭环 + 引入 CommunicationResult（🔴 阻塞级，建议最先做）

**目标 1**: ISystemDriver 增加 `pollFeedback(SystemContext&)` 纯虚方法。

**目标 2**: `send()` 从 `void` 改为 `CommunicationResult` 返回值。
- 将 `CommunicationResult` 加入 `UseCaseError` variant
- 修改所有 UseCase 中的 `drv->send()` 调用点，检查通讯结果

**影响范围**:
| 文件 | 变更 |
|------|------|
| `infrastructure/ISystemDriver.h` | `send()` 签名改为 `CommunicationResult send(...)`；增加 `pollFeedback()` 纯虚方法 |
| `infrastructure/FakeAxisDriver.h` | `send()` 返回 `CommunicationResult{Sent}`；实现 `pollFeedback()` |
| `application/UseCaseError.h` | 增加 `CommunicationResult` 到 variant |
| `application/axis/` 下 5 个 UseCase | `drv->send()` 调用后检查 `commResult.ok()` |
| `application/policy/GantryOrchestrator.h` | 同上 |
| `application/safety/` 下 2 个 UseCase | 同上 |
| 生产 Modbus TCP 驱动 | 新建类，实现真实 send + poll |

**FakeAxisDriver 的 pollFeedback 实现**:
```cpp
void FakeAxisDriver::pollFeedback(SystemContext& ctx) override {
    plc_.tick(10);  // 推进硬件模拟
    for (auto& axisId : ALL_AXIS_IDS) {
        Axis* axis = nullptr;
        ContextRejection r;
        if (ctx.tryGetAxis(axisId, axis, r) && axis) {
            axis->applyFeedback(plc_.getFeedback(axisId));
        }
    }
    ctx.emergencyStopController().applyFeedback(plc_.isEmergencyStopped());
    ctx.gantryCouplingController().applyFeedback(plc_.getGantryFeedback());
}
```

### 阶段 2: 提取 IProtocolTranslator（🟡 设计改善）

**目标**: 将 FakeAxisDriver 内部的翻译逻辑提取为独立接口。

**为什么放在阶段 2 而非阶段 1**:
- 阶段 1 的 `pollFeedback()` 实现可以直接内联翻译逻辑（因为 FakePLC 的 `getFeedback()` 已经返回领域结构体）
- 等有了真实的 Modbus TCP 驱动后，Translator 的边界才会真正显现
- 过早抽象可能导致偏颇的接口设计

### 阶段 3: 替换 bool 为 CommandResult（🟢 类型安全改善）

**目标**: Axis 的 `enable()` / `jog()` / `moveAbsolute()` 等方法返回 `CommandResult` 替代 `bool`。

**影响**: 所有 UseCase 中的 `if (!axis->enable(active))` 需要改为检查 `result.status`。

**优先级**: 可以在阶段 2 之后、任何时间点进行，因为它只影响领域层和 UseCase 层，不影响驱动接口。

### 阶段 4: Modbus TCP 真实驱动（生产上线）

**目标**: 实现 `ModbusTcpAxisDriver` + `ModbusTCPTranslator`。

**依赖**:
- 阶段 1 的 `pollFeedback()` 接口定义 + `CommunicationResult` 返回值
- 阶段 2 的 Translator 接口（可以先把 Translator 内联在驱动中）

---

## 6. 关键接口定义（阶段 1 完成后）

### 6.1 ISystemDriver 最终形态

```cpp
// infrastructure/ISystemDriver.h
#pragma once
#include "domain/command/SystemCommand.h"
#include <string>

class SystemContext;  // 前向声明

/**
 * @brief 通讯层结果 -- 只表达"帧是否成功送达 PLC 的寄存器"
 *
 * 不表达:
 *   - PLC 是否接受了该命令（层级 2 -- pollFeedback 才知道）
 *   - 物理动作是否执行（层级 3 -- pollFeedback 持续确认）
 */
struct CommunicationResult {
    enum class Status {
        Sent,     // 通讯帧成功写入 PLC 寄存器（收到 ACK/正常响应）
        Failed,   // 通讯失败（Socket 超时、断线、CRC 错误、网络抖动）
        Busy,     // PLC 忙（Modbus Exception 0x06 / 设备忙信号）
    };
    Status status = Status::Sent;
    std::string diagnostic;  // 诊断信息（仅日志用，不参与控制流）
    bool ok() const { return status == Status::Sent; }
};

/**
 * @brief 工业控制系统驱动的统一接口
 *
 * 设计原则:
 *   1. Command / Feedback 双通路: send() 发命令，pollFeedback() 收反馈。
 *   2. send() 返回通讯结果（层级 1）-- 只表达"帧是否送达"，不表达"PLC 是否执行"。
 *   3. pollFeedback() 是主动拉取 -- 负责层级 2/3 的物理状态回传。
 *   4. 驱动不返回领域错误 -- 物理异常通过 Feedback 反映在领域实体状态中。
 */
class ISystemDriver {
public:
    virtual ~ISystemDriver() = default;

    // ===== 命令通路 =====

    /// @brief 向硬件发送一个统一命令
    /// @return CommunicationResult -- 只表达通讯帧是否成功送达 PLC 的寄存器。
    ///         不表达 PLC 是否执行成功（层级 2）或物理动作是否完成（层级 3）。
    ///         通信失败时驱动内部记 TRACE_WARN，不抛异常。
    /// @param cmd 统一命令 variant（AxisCommandWithId / EmergencyStopCommand / ...）
    virtual CommunicationResult send(const SystemCommand& cmd) = 0;

    // ===== 反馈通路 =====

    /// @brief 从硬件拉取反馈并分发给 SystemContext 内的所有领域实体
    ///
    /// 每主循环周期调用一次（典型周期: 10ms）。
    ///
    /// 内部执行:
    ///   1. Read:  从硬件读取当前状态（Modbus 寄存器 / EtherCAT PDO / ...）
    ///   2. Translate: 将硬件数据翻译为领域反馈结构体
    ///   3. Dispatch: 注入 Axis::applyFeedback / EmergencyStopController::applyFeedback / ...
    ///
    /// 失败策略:
    ///   - 通信超时/失败: 保留上次已知反馈值，不更新，记 TRACE_WARN
    ///   - 部分寄存器读取失败: 不影响成功读取的部分
    ///
    /// @param ctx 目标分组上下文，反馈将注入其内部领域实体
    virtual void pollFeedback(SystemContext& ctx) = 0;
};
```

### 6.2 FakeAxisDriver 新实现

```cpp
// infrastructure/FakeAxisDriver.h
#pragma once
#include "infrastructure/ISystemDriver.h"
#include "infrastructure/FakePLC.h"
#include "domain/entity/SystemContext.h"

class FakeAxisDriver : public ISystemDriver {
public:
    explicit FakeAxisDriver(FakePLC& plc) : plc_(plc) {}

    // ===== 命令通路 =====
    CommunicationResult send(const SystemCommand& cmd) override {
        std::visit([this](const auto& concrete) {
            this->handle(concrete);
        }, cmd);
        // Fake 驱动永远通讯成功（没有真实网络）
        return CommunicationResult{};
    }

    // ===== 反馈通路 =====
    void pollFeedback(SystemContext& ctx) override {
        // 1. 推进硬件模拟一个周期
        plc_.tick(10);

        // 2. 读取所有 Feedback 并通过 applyFeedback 注入
        constexpr std::array<AxisId, 4> ALL_AXIS_IDS = {AxisId::X, AxisId::Y, AxisId::Z, AxisId::U};
        for (auto& axisId : ALL_AXIS_IDS) {
            Axis* axis = nullptr;
            ContextRejection r;
            if (ctx.tryGetAxis(axisId, axis, r) && axis) {
                axis->applyFeedback(plc_.getFeedback(axisId));
            }
        }
        // 3. 注入安全状态反馈
        ctx.emergencyStopController().applyFeedback(plc_.isEmergencyStopped());
        // 4. 注入龙门状态反馈
        ctx.gantryCouplingController().applyFeedback(plc_.getGantryFeedback());
    }

private:
    FakePLC& plc_;

    // ===== 命令翻译（当前内联在驱动内，阶段 2 提取到 Translator）=====
    void handle(const AxisCommandWithId& cmd) { /* ... 现有逻辑保持不变 ... */ }
    void handle(const EmergencyStopCommand& cmd) { /* ... */ }
    // ... 其他 handle 重载 ...
};
```

### 6.3 UseCaseError 最终形态

```cpp
// application/UseCaseError.h
#pragma once
#include "domain/entity/ContextRejection.h"
#include "domain/entity/RejectionReason.h"       // 假设路径
#include "domain/gantry/GantryRejection.h"
#include "domain/safety/SafetyRejection.h"
#include "infrastructure/ISystemDriver.h"         // 引入 CommunicationResult
#include <variant>

/// @brief 用例层错误类型 -- 覆盖从查找、领域校验到通讯的全部失败路径
using UseCaseError = std::variant<
    std::monostate,          // 成功（领域规则通过 + 通讯送达）
    ContextRejection,        // 分组/轴查找失败（阶段 0/1）
    RejectionReason,         // 领域规则拒绝（阶段 2 -- 命令未生成，未发送）
    CommunicationResult,     // 通讯失败（阶段 3 -- 命令已生成但未送达 PLC）
    GantryRejection,         // 龙门操作拒绝
    SafetyRejection          // 安全操作拒绝
>;
```

### 6.4 各 UseCase 修改模板

所有 UseCase 的修改模式一致，以 `EnableUseCase` 为例：

**当前代码**:
```cpp
if (axis->hasPendingCommand()) {
    if (auto* drv = group->driver()) {
        drv->send(AxisCommandWithId{axisId, axis->getPendingCommand()});
    }
}
```

**阶段 1 之后**:
```cpp
if (axis->hasPendingCommand()) {
    if (auto* drv = group->driver()) {
        auto commResult = drv->send(AxisCommandWithId{axisId, axis->getPendingCommand()});
        if (!commResult.ok()) {
            TRACE_WARN("EnableUseCase: send failed for axis {}, reason: {}",
                       axisId_to_string(axisId), commResult.diagnostic);
            return commResult;  // 通讯失败作为 UseCaseError 返回
        }
    }
}
```

**影响的 7 个调用点**:
| 文件 | 调用点特征 |
|------|-----------|
| `application/axis/EnableUseCase.h` | `drv->send(AxisCommandWithId{axisId, ...})` |
| `application/axis/JogAxisUseCase.h` | 同上 |
| `application/axis/MoveAbsoluteUseCase.h` | 同上 |
| `application/axis/MoveRelativeUseCase.h` | 同上 |
| `application/axis/StopAxisUseCase.h` | 同上 |
| `application/safety/EmergencyStopUseCase.h` | `drv->send(EmergencyStopCommand{...})` |
| `application/safety/ReleaseEmergencyStopUseCase.h` | `drv->send(ReleaseEmergencyStopCommand{...})` |

---

## 7. 测试适配策略

### 7.1 现有测试的影响分析

| 测试文件 | 受影响？ | 说明 |
|----------|---------|------|
| `tests/infrastructure/test_fake_plc.cpp` | ✅ 无影响 | FakePLC 没有改变 |
| `tests/infrastructure/test_system_integration.cpp` | ⚠️ 需适配 | `syncA()` 手动泵送改为 `pollFeedback()` 调用 |
| `tests/application/test_enable_usecase.cpp` | ⚠️ 需适配 | `send()` 返回 `CommunicationResult`，需检查 `.ok()` |
| `tests/application/test_jog_usecase.cpp` | ⚠️ 需适配 | 同上 |
| `tests/application/test_move_absolute_usecase.cpp` | ⚠️ 需适配 | 同上 |
| `tests/application/test_move_relative_usecase.cpp` | ⚠️ 需适配 | 同上 |
| `tests/application/test_stop_usecase.cpp` | ⚠️ 需适配 | 同上 |
| `tests/application/test_system_manager.cpp` | ✅ 无影响 | SystemManager 不变 |
| `tests/application/safety/test_emergency_stop_usecase.cpp` | ⚠️ 需适配 | 同上 |
| `tests/domain/test_*.cpp` | ✅ 无影响 | 领域层不变 |
| `tests/presentation/viewmodel/test_axis_viewmodel_core.cpp` | ✅ 无影响 | ViewModel 不变 |

### 7.2 测试适配模板（test_system_integration.cpp）

**当前测试模式**:
```cpp
// 手动泵送反馈
void syncA(AxisId id) {
    Axis* a = nullptr;
    ContextRejection r;
    if (ctxA->tryGetAxis(id, a, r) && a) {
        a->applyFeedback(plcA.getFeedback(id));
    }
}

// 测试
syncA(AxisId::X);
// 断言轴状态...
```

**阶段 1 之后的新模式**:
```cpp
// 注入 FakeDriver 并触发 pollFeedback
FakePLC plc;
FakeAxisDriver driver(plc);
ctx->setDriver(&driver);

// 一行替代手动泵送
driver.pollFeedback(*ctx);
// 断言轴状态与 plc 寄存器一致...
```

**优点**:
- 测试代码更接近生产代码路径（真实的 `pollFeedback` -> `applyFeedback` 调用链）
- 不需要在测试中手动遍历所有 `AxisId`
- FakePLC 的 `tick()` 由 `pollFeedback` 内部自动调用

### 7.3 测试适配模板（test_enable_usecase.cpp）

**当前断言模式**:
```cpp
auto error = EnableUseCase::execute(manager, "Group1", AxisId::X, true);
ASSERT_TRUE(std::holds_alternative<std::monostate>(error));
```

**阶段 1 之后**:
```cpp
auto error = EnableUseCase::execute(manager, "Group1", AxisId::X, true);

// 断言成功（领域规则通过 + FakeDriver 通讯永远成功）
ASSERT_TRUE(std::holds_alternative<std::monostate>(error));
```

**无需改变**----FakeAxisDriver 的 `send()` 永远返回 `CommunicationResult{Sent}`，所以对于 Fake 驱动的测试，`monostate` 仍然正确。唯一需要新增的是 **通讯失败场景的测试**：

```cpp
// 新增测试：通讯失败应返回 CommunicationResult
TEST(EnableUseCase, ReturnsCommunicationResultWhenSendFails) {
    // 使用一个 send() 返回 Failed 的 Mock/Fake 驱动
    auto error = EnableUseCase::execute(manager, "Group1", AxisId::X, true);
    ASSERT_TRUE(std::holds_alternative<CommunicationResult>(error));
    auto& cr = std::get<CommunicationResult>(error);
    EXPECT_EQ(cr.status, CommunicationResult::Status::Failed);
}
```

---

## 8. 未来 EtherCAT / ARM 扩展

### 8.1 ISystemDriver 接口对 EtherCAT 的适用性

当前设计的 `ISystemDriver` 接口不绑定 Modbus（没有任何寄存器地址、功能码等概念），因此对 EtherCAT 同样适用：

| ISystemDriver 方法 | EtherCAT 实现对应 |
|-------------------|------------------|
| `send(SystemCommand)` | 写入 RxPDO（过程数据对象） |
| `pollFeedback(SystemContext)` | 读取 TxPDO + 分发到领域实体 |

**EtherCAT 驱动的伪代码**:
```cpp
class EtherCATAxisDriver : public ISystemDriver {
public:
    CommunicationResult send(const SystemCommand& cmd) override {
        auto pdoData = m_translator->toPDO(cmd);
        auto result = m_master.writeRxPDO(m_slaveAddr, pdoData);
        if (!result.ok()) {
            return CommunicationResult{Status::Failed, result.error()};
        }
        return CommunicationResult{};
    }

    void pollFeedback(SystemContext& ctx) override {
        // EtherCAT 主站周期性地通过 DC 同步获取 TxPDO
        auto txPdo = m_master.readTxPDO(m_slaveAddr);
        if (!txPdo.valid) {
            TRACE_WARN("EtherCAT TxPDO read failed, keeping last feedback");
            return;
        }
        auto batch = m_translator->fromPDO(txPdo);
        dispatchToContext(batch, ctx);
    }

private:
    EtherCATMaster m_master;
    uint16_t m_slaveAddr;
    std::unique_ptr<IEtherCATTranslator> m_translator;
};
```

### 8.2 ARM 平台适配

当前设计基于标准 C++17，不依赖任何平台特定 API。对于 ARM Linux 平台（如 Raspberry Pi、i.MX、STM32MP1）：

| 组件 | ARM 适配要点 |
|------|-------------|
| `CommunicationResult` | 无变化 -- 纯 C++ 结构体 |
| `ISystemDriver` | 无变化 -- 纯虚接口 |
| `ModbusTcpAxisDriver` | 需链接 ARM 交叉编译的 libmodbus |
| `EtherCATAxisDriver` | 需链接 IgH EtherCAT Master 或 SOEM 的用户空间库 |
| `pollFeedback` 调用 | 在 QML 主循环的定时器中调用（与 x86 一致） |
| 实时性 | 如需硬实时，将驱动放入 RT 线程；接口不变 |

**关键结论**: ISystemDriver 接口对 ARM 平台**零修改**。

### 8.3 多协议共存能力

由于 `ISystemDriver` 是纯虚接口，一个 `SystemContext` 可以注入任意实现：

```cpp
// Modbus 控制 3 个轴
auto ctx1 = manager.createGroup("ModbusGroup", reason);
ctx1->setDriver(new ModbusTcpAxisDriver("192.168.1.100", 502, ...));

// EtherCAT 控制 5 个轴
auto ctx2 = manager.createGroup("EtherCATGroup", reason);
ctx2->setDriver(new EtherCATAxisDriver(...));

// 测试环境
auto ctx3 = manager.createGroup("TestGroup", reason);
ctx3->setDriver(new FakeAxisDriver(plc));
```

上层的 UseCase / Orchestrator / ViewModel **完全不需要知道**底层是 Modbus 还是 EtherCAT。

---

## 9. 常见质疑与回答

### Q1: "当前的 void send() 也能工作，为什么不先做其他功能？"

**A**: 当前能工作是因为 **Fake 驱动从不失败**。只要一切到真实硬件（Modbus TCP 走网线），通信失败就是一个高频事件----网线松动、交换机重启、PLC 忙、Socket 超时。`void send()` 在这些场景下会导致：
- 用户收到"操作成功"的 UI 反馈，但轴根本没动
- 排查问题时日志里只有一条 TRACE_WARN，无法追踪是哪次 send 失败
- 后续 pollFeedback 发现状态不一致时，已经丢失了"send 失败"这个关键因果信息

**CommunicationResult 的引入不是过度设计，而是工业控制系统的基本要求。**

### Q2: "pollFeedback 放在 SystemContext 里是不是让驱动知道太多领域细节？"

**A**: 这是一个合理的架构关注。设计选择是：

- **方案 A（当前推荐）**: `pollFeedback(SystemContext&)` -- 驱动负责分发
- **方案 B（备选）**: `pollFeedback() -> DomainFeedbackBatch` -- 驱动返回裸数据

选择方案 A 的理由：
1. **只有驱动知道寄存器映射** -- 哪个寄存器对应 AxisFeedback::actualPosition？哪个位对应急停？这个映射天然属于驱动/Translator 的知识范围
2. **避免上层重复写 Dispatcher** -- 如果驱动返回 `DomainFeedbackBatch`，那每个调用者（主循环、测试、仿真）都需要写一遍 `for (auto& [id, fb] : batch.axisFeedbacks) { axis->applyFeedback(fb); }`
3. **SystemContext 本身就是分组隔离的** -- 驱动只知道它所属的那个分组的上下文，这符合物理拓扑（一个 PLC 控制一组设备）

**如果未来发现驱动对 SystemContext 的依赖过多，可以重构为方案 B（阶段 2 提取 IProtocolTranslator 后很容易切换）。**

### Q3: "为什么 CommunicationResult 有 diagnostic 字段？这不是日志的活吗？"

**A**: `diagnostic` 字段的目的是让 **UI 层能做出决策**，而不仅仅是记录日志。

```cpp
// 没有 diagnostic -> UI 只能显示"通讯失败"
showError("通讯失败");

// 有 diagnostic -> UI 可以给出有针对性的提示
if (commResult.status == Status::Failed) {
    showError("网络连接失败: " + commResult.diagnostic);
} else if (commResult.status == Status::Busy) {
    showError("PLC 繁忙，请稍后重试");
}
```

这直接提升了操作员的体验----"网络连接失败 (Socket timeout: 192.168.1.100:502)"远比"操作失败"有用。

### Q4: "这不是过度工程吗？我们就一个 Modbus PLC"

**A**: 当前的接口设计**不是为了支持多种协议而做**，而是因为：
1. **Command / Feedback 双通路**是工业控制的本质----不管你用 Modbus 还是 EtherCAT，命令和反馈都是两条独立通路
2. `send()` -> `CommunicationResult` 是纠正了一个**客观上存在的语义缺陷**
3. `pollFeedback()` 是**填补了一个架构空洞**----测试中有手动泵送，生产中却没有对应机制

这些修改没有引入任何"为了未来扩展"的抽象层（IProtocolTranslator 被明确推迟到阶段 2），只解决当前代码中真实存在的问题。

### Q5: "阶段 1 一次性改 7 个 UseCase，风险会不会太大？"

**A**: 风险可控，因为：
1. 所有修改的模式完全一致----在每个 `drv->send(...)` 后增加 3 行检查
2. Fake 驱动永远返回 `Sent`，所有现有测试无需修改断言
3. 编译器会在编译期检查所有调用点----`send()` 从 `void` 变成 `CommunicationResult`，漏改的地方会直接编译失败
4. 如果担心风险，可以先在 `ISystemDriver` 中增加 `pollFeedback()`，`send()` 暂时保持 `void`，分两次 PR 提交

---

## 附录 A: 重构前后问题严重程度总览

| # | 问题 | 严重程度 | 阶段 0 | 阶段 1 后 | 阶段 2 后 | 阶段 3 后 |
|---|------|---------|--------|----------|----------|----------|
| 0 | send(void) 丢失通讯结果 | 🔴 | ❌ | ✅ CommunicationResult | ✅ | ✅ |
| 1 | 无 pollFeedback 路径 | 🔴 | ❌ | ✅ ISystemDriver::pollFeedback | ✅ | ✅ |
| 2 | bool 语义混淆 | 🟡 | ⚠️ 有注释 | ⚠️ 仍有 | ⚠️ 仍有 | ✅ CommandResult |
| 3 | FakeAxisDriver 混合 Translator | 🟢 | ⚠️ 可接受 | ⚠️ 可接受 | ✅ IProtocolTranslator | ✅ |
| 4 | 生产反馈环路缺失 | 🔴 | ❌ | ✅ 主循环 pollFeedback | ✅ | ✅ |

## 附录 B: 与现有架构文档的关系

| 现有文档 | 本文档关联点 |
|---------|------------|
| `统一命令总线与反馈分发 -- 架构重构思考.md` | 本文是那篇思考文档的**可执行实现方案**，将"应该有反馈通路"细化到接口签名、影响范围和测试适配 |
| `单轴设置功能----数据流与架构设计.md` | 该文档的 Command 数据流在阶段 1 后增加 `CommunicationResult` 检查点 |
| `设备安全急停----领域设计与实现思路.md` | 急停的 Feedback 通路通过 `pollFeedback` -> `EmergencyStopController::applyFeedback(bool)` 实现 |

## 附录 C: 重构实施时间估算

| 阶段 | 工作量 | 风险 | 预估时间 |
|------|--------|------|---------|
| 阶段 1 | 约 150 行代码变更 | 低（编译期检查，测试不变） | 4-6 小时 |
| 阶段 2 | 约 200 行代码新增 | 中（提取接口可能需调整） | 4-6 小时 |
| 阶段 3 | 约 300 行代码变更 | 中（影响所有 UseCase） | 4-6 小时 |
| 阶段 4 | 约 500 行代码新增 | 高（涉及真实硬件） | 2-3 天 |

**总计**: 约 3-4 天（不含硬件调试时间）

---

> **文档状态**: ✅ 完整  
> **下次评审建议**: 阶段 1 实施前，确认 `CommunicationResult` 的三态（Sent/Failed/Busy）是否覆盖所有 Modbus 异常码；确认 `pollFeedback` 在 QML 主循环中的调用频率（建议 10-20ms）
