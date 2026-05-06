# 下一阶段 TDD 开发任务 — 龙门子系统

> 版本: v1.0 | 状态: Draft
> 依据: `双轴龙门系统_TDD架构设计文档.md` + 20条业务约束 + 当前代码库分析
> 配套文档: `domain/docs/龙门系统接口业务语义约束.md`
> 创建时间: 2026-05-06

---

## 0. 当前状态总览

### 0.1 已完成部分 ✅

| 层次 | 产出 | 文件 |
|------|------|------|
| 值对象 | GantryMode, MotionDirection, CouplingCondition, GantryPosition, PositionConsistency, SafetyCheckResult | `domain/value/*.h` |
| 实体 | PhysicalAxis, LogicalAxis, GantrySystem (骨架), SystemContext, AxisId | `domain/entity/*.h` |
| 事件 | GantryEvents (9种事件) | `domain/event/GantryEvents.h` |
| 端口 | IGantryFeedbackPort, IGantryCommandPort, IGantryEventBus, IGantryEventPublisher, IGantryStateQuery | `domain/port/*.h` |
| 服务 | GantryCouplingService (联动条件判断), GantrySafetyService (安全检查), GantryStateAggregator (状态聚合) | `domain/service/*.h` |
| 测试 | 值对象+实体+服务+联动/安全/方向/位置/同步 单测 | `tests/domain/gantry/*.cpp` |
| 基础设施 | FakePLC, FakeAxisDriver | `infrastructure/FakePLC.h`, `infrastructure/FakeAxisDriver.h` |

### 0.2 待完成部分 ❌

| 层次 | 待完成 | 优先级 |
|------|--------|:---:|
| **GantrySystem 聚合根** | 实现完整 API（模式管理/运动命令/状态聚合/事件管理） | **P0 阻塞** |
| **Fake 端口实现** | FakeGantryFeedbackPort, FakeGantryCommandPort, FakeGantryEventBus | **P0** |
| **IGantryStateQuery 实现** | GantrySystem 实现 `IGantryStateQuery` 接口 | **P0** |
| **PhysicalAxisState** | 新建值对象 DTO | P1 |
| **Operability 枚举** | 新建操作可行性枚举 | P1 |
| **CommandResult** | 新建命令结果结构体 | P1 |
| **PhysicalAxis::syncState()** | 新增状态同步方法 | P1 |
| **FakePLC 扩展** | 补充 enabled/busy/jog 命令寄存器 | P1 |
| **集成测试** | GantrySystem 全流程集成测试 | **P0** |
| **Application GantryOrchestrator** | 龙门编排器（协调 GantrySystem + 端口） | P2 |

---

## 阶段一：新增值对象和基础类型（1~2天）

### T1.1 创建 `PhysicalAxisState` DTO

**文件:** `domain/entity/PhysicalAxisState.h` (或 `domain/value/PhysicalAxisState.h`)

```cpp
struct PhysicalAxisState {
    bool enabled         = false;
    double position      = 0.0;
    bool alarmed         = false;
    bool posLimitActive  = false;
    bool negLimitActive  = false;
};
```

**对应测试:** `tests/domain/gantry/test_physical_axis_state.cpp`

**测试用例：**
| # | 用例名 | Given | When | Then |
|---|--------|-------|------|------|
| TS1.1.1 | 默认构造全为 false/0 | 默认构造 | 检查字段 | enabled=false, pos=0.0, alarmed=false, limits=false |
| TS1.1.2 | posLimit 与 negLimit 不可同时 true | posLimit=true, negLimit=true | 构造 | 断言失败或行为定义明确 |
| TS1.1.3 | alarmed 为 true 时 enabled 应为 false | alarmed=true, enabled=true | 构造 | 建议触发断言（约束17） |
| TS1.1.4 | 值语义拷贝独立 | 构造 s1, 拷贝 s2, 修改 s2 | s1 值不变 | s1 和 s2 独立 |

---

### T1.2 创建 `Operability` 枚举

**文件:** `domain/value/Operability.h`

```cpp
enum class Operability {
    Allowed,
    TargetNotOperableInMode,
    AlarmActive,
    LimitTriggered,
    LimitBlocksDirection,
    NotEnabled,
    CommandSlotBusy,
    NotIdle,
    DeviationExceeded,
};
```

**对应测试:** `tests/domain/gantry/test_operability.cpp`

**测试用例：**
| # | 用例名 | Given | When | Then |
|---|--------|-------|------|------|
| TS1.2.1 | Operability 枚举可比较 | Allowed vs AlarmActive | `==` 比较 | 不相等 |
| TS1.2.2 | Allowed 表示通过 | Allowed | 判断 | 为唯一通过值 |

---

### T1.3 创建 `CommandResult` 结构体

**文件:** `domain/value/CommandResult.h`

```cpp
struct CommandResult {
    Operability verdict;
    std::string detail;
    bool isAccepted() const;
    static CommandResult accept();
    static CommandResult reject(Operability r, const std::string& d);
};
```

**对应测试:** `tests/domain/gantry/test_command_result.cpp`

**测试用例：**
| # | 用例名 | Given | When | Then |
|---|--------|-------|------|------|
| TS1.3.1 | accept() 返回 Allowed | accept() | isAccepted() | true |
| TS1.3.2 | reject() 返回非 Allowed | reject(AlarmActive) | isAccepted() | false, verdict=AlarmActive |

---

## 阶段二：PhysicalAxis 扩展 — syncState()（0.5天）

### T2.1 PhysicalAxis 新增 `syncState()` 方法

**文件:** `domain/entity/PhysicalAxis.h`

```cpp
// 在 PhysicalAxis 类中新增：
void syncState(const PhysicalAxisState& state);
```

**对应测试:** 在现有 `tests/domain/test_axis.cpp` 或新增 `tests/domain/gantry/test_physical_axis_sync.cpp`

**测试用例：**
| # | 用例名 | Given | When | Then |
|---|--------|-------|------|------|
| TS2.1.1 | syncState 更新 position | Axis position=0 | syncState({position:100}) | position()==100 |
| TS2.1.2 | syncState 更新 enabled | Axis disabled | syncState({enabled:true}) | enabled()==true |
| TS2.1.3 | syncState 更新 alarmed | Axis normal | syncState({alarmed:true}) | 内部 alarm 标志更新 |
| TS2.1.4 | syncState 更新限位状态 | Axis 无限位 | syncState({posLimitActive:true}) | 限位标志更新 |
| TS2.1.5 | alarmed 时 enabled 自动为 false | 正常→alarmed | syncState({alarmed:true}) | enabled 变为 false |
| TS2.1.6 | 连续两次 syncState | 第一次→第二次 | 第二次覆盖第一次 | 状态始终为最新 |

---

## 阶段三：Fake 端口实现（1天）

### T3.1 创建 `FakeGantryFeedbackPort`

**文件:** `infrastructure/FakeGantryFeedbackPort.h`

```cpp
#include "domain/port/IGantryFeedbackPort.h"
#include "infrastructure/FakePLC.h"

class FakeGantryFeedbackPort : public IGantryFeedbackPort {
    FakePLC& m_plc;
public:
    explicit FakeGantryFeedbackPort(FakePLC& plc);
    PhysicalAxisState getX1Feedback() const override;
    PhysicalAxisState getX2Feedback() const override;
    void resetAlarm() override;
    bool isAnyAlarm() const override;
    bool isAnyLimit() const override;
};
```

**对应测试:** `tests/infrastructure/test_fake_gantry_feedback_port.cpp`

**测试用例：**
| # | 用例名 | Given | When | Then |
|---|--------|-------|------|------|
| TS3.1.1 | getX1Feedback 读取 FakePLC 状态 | PLC X1 pos=42, enabled=true | getX1Feedback() | position=42, enabled=true |
| TS3.1.2 | getX2Feedback 读取 FakePLC 状态 | PLC X2 pos=-42 | getX2Feedback() | position=-42 |
| TS3.1.3 | resetAlarm 清除报警 | PLC X1 alarmed | resetAlarm() → getX1Feedback() | alarmed=false |
| TS3.1.4 | isAnyAlarm 两轴均正常 | 无报警 | isAnyAlarm() | false |
| TS3.1.5 | isAnyAlarm X1 报警 | X1 alarmed | isAnyAlarm() | true |
| TS3.1.6 | isAnyLimit X1 正限位 | X1 posLimitActive | isAnyLimit() | true |

---

### T3.2 创建 `FakeGantryCommandPort`

**文件:** `infrastructure/FakeGantryCommandPort.h`

```cpp
#include "domain/port/IGantryCommandPort.h"
#include "infrastructure/FakePLC.h"

class FakeGantryCommandPort : public IGantryCommandPort {
    FakePLC& m_plc;
public:
    explicit FakeGantryCommandPort(FakePLC& plc);

    // 单轴命令
    bool jogAxis(int axis, MotionDirection direction) override;
    bool moveAbsoluteAxis(int axis, double position) override;
    bool moveRelativeAxis(int axis, double delta) override;
    bool stopAxis(int axis) override;

    // 龙门命令（内部调用单轴命令 + 镜像）
    bool jogGantry(MotionDirection direction) override;
    bool moveAbsoluteGantry(double position) override;
    bool moveRelativeGantry(double delta) override;
    bool stopGantry() override;

    bool isAxisSlotFree(int axis) const override;

    // 测试辅助：获取最后下发的命令（用于验证）
    struct LastCommand { int axis; MotionDirection dir; double pos; };
    std::vector<LastCommand> commandLog;  // 测试用
};
```

**对应测试:** `tests/infrastructure/test_fake_gantry_command_port.cpp`

**测试用例：**
| # | 用例名 | Given | When | Then |
|---|--------|-------|------|------|
| TS3.2.1 | jogAxis(1,Forward) 下发到 FakePLC | axis=1, dir=Forward | jogAxis() → 检查 commandLog | axis=1, dir=Forward |
| TS3.2.2 | jogAxis 非法 axis 返回 false | axis=3 | jogAxis() | 返回 false |
| TS3.2.3 | jogGantry(Forward) 原子下发 X1/X2 | dir=Forward | jogGantry() | X1=Forward, X2=Backward |
| TS3.2.4 | jogGantry(Backward) 原子下发 | dir=Backward | jogGantry() | X1=Backward, X2=Forward |
| TS3.2.5 | moveAbsoluteGantry(p) 镜像 | pos=100 | moveAbsoluteGantry(100) | X1=100, X2=-100 |
| TS3.2.6 | moveRelativeGantry(d) 镜像 | delta=10 | moveRelativeGantry(10) | X1=+10, X2=-10 |
| TS3.2.7 | stopGantry 同时 stop X1/X2 | — | stopGantry() | X1 stop, X2 stop |
| TS3.2.8 | isAxisSlotFree 空闲 | FakePLC axis free | isAxisSlotFree(1) | true |
| TS3.2.9 | isAxisSlotFree 忙碌 | FakePLC axis busy | isAxisSlotFree(1) | false |
| TS3.2.10 | X1成功 X2失败时 X1 自动 Stop | X2 故障 | jogGantry() | X1 被 stop |

---

### T3.3 创建 `FakeGantryEventBus`

**文件:** `infrastructure/FakeGantryEventBus.h`

```cpp
#include "domain/port/IGantryEventBus.h"

class FakeGantryEventBus : public IGantryEventBus {
    std::vector<GantryEvents::Event> m_publishedEvents;
public:
    void publish(const GantryEvents::Event& event) override;
    const std::vector<GantryEvents::Event>& publishedEvents() const;
    void clear();
};
```

**对应测试:** `tests/infrastructure/test_fake_gantry_event_bus.cpp`

**测试用例：**
| # | 用例名 | Given | When | Then |
|---|--------|-------|------|------|
| TS3.3.1 | publish 追加到列表 | AlarmRaised | publish() | 事件存入列表 |
| TS3.3.2 | publishAll 保持顺序 | [Coupled, PositionUpdated] | publishAll() | 列表顺序一致 |
| TS3.3.3 | clear 清空列表 | 已发布 2 条 | clear() | 列表为空 |

---

## 阶段四：FakePLC 扩展（0.5天）

### T4.1 扩展 FakePLC 寄存器

**文件:** `infrastructure/FakePLC.h`

**新增字段（现有 `Axis` 结构体内）：**
```cpp
struct Axis {
    // ... 现有字段 ...
    bool servoReady = false;   // 使能状态（新增）
    bool busy = false;         // 命令槽占用（新增）
    bool jogForward = false;   // Jog 正向命令（新增）
    bool jogBackward = false;  // Jog 负向命令（新增）
    bool stop = false;         // Stop 命令（新增）
    double targetPos = 0.0;    // 绝对定位目标（新增）
    bool startMove = false;    // Move 触发（新增）
};
```

**对应测试:** 扩展 `tests/infrastructure/test_fake_plc.cpp`

**测试用例：**
| # | 用例名 | Given | When | Then |
|---|--------|-------|------|------|
| TS4.1.1 | servoReady 默认 false | 新构造 | 检查 | servoReady==false |
| TS4.1.2 | busy 状态可读写 | 设置 busy=true | 检查 | busy==true |
| TS4.1.3 | jogForward 写入后 jogBackward 互斥 | jogForward=true | jogBackward=true | 行为定义（通常写入互斥） |

---

## 阶段五：GantrySystem 聚合根完整实现（P0 阻塞，2~3天）

这是整个下一阶段的核心任务。当前 `GantrySystem.h` 仅有骨架。

### T5.1 GantrySystem 实现 IGantryStateQuery

**修改文件:** `domain/entity/GantrySystem.h` + 新建 `domain/entity/GantrySystem.cpp`

```cpp
class GantrySystem : public IGantryStateQuery, public IGantryEventPublisher {
    PhysicalAxis m_x1, m_x2;
    GantryMode m_mode = GantryMode::Decoupled;
    std::vector<GantryEvents::Event> m_events;
    IGantryCommandPort* m_commandPort = nullptr;

public:
    GantrySystem();
    void setCommandPort(IGantryCommandPort* port);

    // ── IGantryStateQuery 实现 ──
    GantryMode mode() const override;
    AxisState aggregatedState() const override;
    bool isCoupled() const override;
    double position() const override;
    double x1Position() const override;
    double x2Position() const override;
    bool x1Enabled() const override;
    bool x2Enabled() const override;
    bool isAnyAlarm() const override;
    bool isAnyLimit() const override;
    bool canAcceptCommand() const override;
    bool isTargetOperable(AxisId target) const override;
    std::string stateDescription() const override;

    // ── IGantryEventPublisher 实现 ──
    void publish(const GantryEvents::Event& event) override;

    // ── 模式管理 ──
    CommandResult requestCoupling();
    void requestDecoupling(const std::string& reason);

    // ── 操作可行性 ──
    Operability checkOperability(AxisId target, MotionDirection dir) const;
    Operability checkOperabilityForMove(AxisId target) const;

    // ── 运动命令 ──
    bool jog(AxisId target, MotionDirection dir);
    bool moveAbsolute(AxisId target, double position);
    bool moveRelative(AxisId target, double delta);
    bool stop(AxisId target);

    // ── 状态聚合 ──
    void aggregateState();

    // ── 事件管理 ──
    std::vector<GantryEvents::Event> drainEvents();
    const std::vector<GantryEvents::Event>& events() const;

    // ── 物理轴同步（供外部注入 PhysicalAxisState） ──
    void syncX1State(const PhysicalAxisState& state);
    void syncX2State(const PhysicalAxisState& state);
};
```

**对应测试:** 扩展 `tests/domain/gantry/test_gantry_system_integration.cpp` +
新增以下专项测试文件：

### T5.2 模式管理测试

**测试文件:** `tests/domain/gantry/test_gantry_system_mode.cpp`

**测试用例：**
| # | 用例名 | Given | When | Then |
|---|--------|-------|------|------|
| TS5.2.1 | 默认模式为 Decoupled | 新构造 | mode() | Decoupled |
| TS5.2.2 | requestCoupling 两轴 enabled→成功 | X1/X2 enabled | requestCoupling() | isCoupled()==true, 事件 Coupled |
| TS5.2.3 | requestCoupling X1 未 enabled→拒绝 | X1 disabled | requestCoupling() | 拒绝原因=NotEnabled |
| TS5.2.4 | requestCoupling X2 未 enabled→拒绝 | X2 disabled | requestCoupling() | 拒绝原因=NotEnabled |
| TS5.2.5 | requestCoupling X1/X2 均 disabled→拒绝 | 均 disabled | requestCoupling() | 拒绝, 事件 CouplingRefused |
| TS5.2.6 | requestCoupling 已 Coupled→幂等 | Coupled | requestCoupling() | 仍为 Coupled, 无新 Coupling 事件 |
| TS5.2.7 | requestCoupling 报警状态→拒绝 | X1 alarmed | requestCoupling() | 拒绝原因=AlarmActive |
| TS5.2.8 | requestDecoupling 正常解联 | Coupled | requestDecoupling("user") | Decoupled, 事件 Decoupled |
| TS5.2.9 | requestDecoupling 已 Decoupled→无操作 | Decoupled | requestDecoupling() | 仍为 Decoupled, 无事件 |

### T5.3 操作可行性测试

**测试文件:** `tests/domain/gantry/test_gantry_system_operability.cpp`

**测试用例：**
| # | 用例名 | Given | When | Then |
|---|--------|-------|------|------|
| TS5.3.1 | Coupled 模式 X 可操作 | Coupled, 无报警 | isTargetOperable(X) | true |
| TS5.3.2 | Coupled 模式 X1 不可操作 | Coupled | isTargetOperable(X1) | false |
| TS5.3.3 | Coupled 模式 X2 不可操作 | Coupled | isTargetOperable(X2) | false |
| TS5.3.4 | Decoupled 模式 X 不可操作 | Decoupled | isTargetOperable(X) | false |
| TS5.3.5 | Decoupled 模式 X1 可操作 | Decoupled | isTargetOperable(X1) | true |
| TS5.3.6 | Decoupled 模式 X2 可操作 | Decoupled | isTargetOperable(X2) | true |
| TS5.3.7 | checkOperability 报警→AlarmActive | X1 alarmed | checkOperability(X1, Forward) | AlarmActive |
| TS5.3.8 | checkOperability 目标不可操作 | Coupled, X1 | checkOperability(X1, Forward) | TargetNotOperableInMode |
| TS5.3.9 | checkOperability 未使能 | Decoupled, X1 disabled | checkOperability(X1, Forward) | NotEnabled |
| TS5.3.10 | checkOperabilityForMove 无限位→Allowed | 无限位 | checkOperabilityForMove(X) | Allowed |
| TS5.3.11 | checkOperabilityForMove 正限位→LimitTriggered | X1 posLimit | checkOperabilityForMove(X1) | LimitTriggered |
| TS5.3.12 | checkOperabilityForMove 负限位→LimitTriggered | X1 negLimit | checkOperabilityForMove(X1) | LimitTriggered |
| TS5.3.13 | Jog 正限位禁Forward允Backward | posLimitActive | checkOperability(X1,Forward) | LimitBlocksDirection |
| TS5.3.14 | Jog 正限位允Backward | posLimitActive | checkOperability(X1,Backward) | Allowed |
| TS5.3.15 | Jog 负限位禁Backward允Forward | negLimitActive | checkOperability(X1,Backward) | LimitBlocksDirection |
| TS5.3.16 | 多原因按优先级返回 Alarm>Limit>NotEnabled>... | alarmed+disabled+limit | checkOperability() | AlarmActive (最高优先) |

### T5.4 运动命令测试

**测试文件:** `tests/domain/gantry/test_gantry_system_motion.cpp`

**测试用例：**
| # | 用例名 | Given | When | Then |
|---|--------|-------|------|------|
| TS5.4.1 | jog(X,Forward) Coupled→X1Fwd+X2Bwd | Coupled, 正常 | jog(X,Forward) | X1=Forward, X2=Backward |
| TS5.4.2 | jog(X,Backward) Coupled→X1Bwd+X2Fwd | Coupled, 正常 | jog(X,Backward) | X1=Backward, X2=Forward |
| TS5.4.3 | jog(X1,Forward) Decoupled→仅 X1 | Decoupled, 正常 | jog(X1,Forward) | X1=Forward, X2 无命令 |
| TS5.4.4 | jog(X1,Backward) Decoupled→仅 X1 | Decoupled, 正常 | jog(X1,Backward) | X1=Backward, X2 无命令 |
| TS5.4.5 | jog 校验未通过→拒绝+事件 | X1 posLimit | jog(X1,Forward) | 拒绝, CommandRejected 事件 |
| TS5.4.6 | moveAbsolute(X,100) Coupled→X1=100,X2=-100 | Coupled, 正常 | moveAbsolute(X,100) | X1 目标100, X2 目标-100 |
| TS5.4.7 | moveAbsolute 校验未通过→拒绝 | X1 limit | moveAbsolute(X1,100) | 拒绝 |
| TS5.4.8 | moveRelative(X,10) Coupled→X1=+10,X2=-10 | Coupled, 正常 | moveRelative(X,10) | X1 增量+10, X2 增量-10 |
| TS5.4.9 | stop(X) Coupled→X1+X2 Stop | Coupled | stop(X) | X1 stop, X2 stop |
| TS5.4.10 | stop(X1) Decoupled→仅 X1 Stop | Decoupled | stop(X1) | X1 stop, X2 无命令 |
| TS5.4.11 | stop 无条件接受 | 任何状态 | stop(X) | 接受，下发 stop |

### T5.5 状态聚合测试

**测试文件:** `tests/domain/gantry/test_gantry_system_aggregation.cpp`

**测试用例：**
| # | 用例名 | Given | When | Then |
|---|--------|-------|------|------|
| TS5.5.1 | 正常→aggregatedState=Idle | X1/X2 enabled, idle | aggregateState() | aggregatedState()==Idle |
| TS5.5.2 | 报警→aggregatedState=Error | X1 alarmed | aggregateState() | aggregatedState()==Error |
| TS5.5.3 | 报警→Aggregated 优先级最高 | X1 alarmed+X2 jogging | aggregateState() | Error |
| TS5.5.4 | 限位→aggregatedState=Limit | X1 posLimit | aggregateState() | 反映 Limit |
| TS5.5.5 | Coupled 偏差超阈值→强制 Decoupled | Coupled, X1≠X2 | aggregateState() | Decoupled + DeviationFault 事件 |
| TS5.5.6 | Coupled 偏差正常→维持 Coupled | Coupled, X1≈X2 | aggregateState() | 仍为 Coupled |
| TS5.5.7 | alarmed 边沿→AlarmRaised 事件 | 从正常→alarmed | aggregateState() | AlarmRaised 事件 |
| TS5.5.8 | 限位边沿→LimitTriggered 事件 | 从无限位→限位 | aggregateState() | LimitTriggered 事件 |
| TS5.5.9 | 位置变化→PositionUpdated 事件 | position 变化 | aggregateState() | PositionUpdated 事件 |
| TS5.5.10 | X逻辑位置 = X1物理位置 | X1=42, X2=-42 | aggregateState() | position()==42 |

### T5.6 事件管理测试

**测试文件:** 合并入 `test_gantry_system_integration.cpp`

**测试用例：**
| # | 用例名 | Given | When | Then |
|---|--------|-------|------|------|
| TS5.6.1 | drainEvents 返回所有事件并清空 | 2个事件入队 | drainEvents() | 返回2个, 队列为空 |
| TS5.6.2 | events() 只读不消耗 | 1个事件入队 | events()×2 | 每次返回1个, 队列不清空 |
| TS5.6.3 | 同周期不重复发布相同事件 | 已发布 Coupled | 再次 publish(Coupled) | 不重复入队 |

---

## 阶段六：集成测试（1~2天）

### T6.1 GantrySystem + Fake 端口 全流程集成测试

**测试文件:** `tests/domain/gantry/test_gantry_full_integration.cpp`

**测试用例：**

| # | 用例名 | Given | When | Then |
|---|--------|-------|------|------|
| TS6.1.1 | Feedback→Aggregation→Event→Command 完整链路 | FakePLC 正常状态 | 完整扫描周期 | 状态正确传递 |
| TS6.1.2 | Coupled 联动建立→Jog→偏差检测→自动解联 | X1/X2 enabled | 建立联动→Jog→X1 卡住→偏差超限 | 自动解联+DeviationFault |
| TS6.1.3 | 限位触发→Jog 方向限制→反方向可退 | X1 正限位 | Jog(Forward)拒绝, Jog(Backward)通过 | 方向限制正确 |
| TS6.1.4 | 报警触发→禁止所有运动命令 | X1 alarmed | Jog/Move 全部拒绝 | 全部 AlarmActive |
| TS6.1.5 | 报警复位→恢复运动能力 | alarmed→resetAlarm() | 运动命令 | 通过 |

---

## 任务优先级排序与依赖

```
阶段一 (值对象扩展)
  ├── T1.1 PhysicalAxisState DTO ──────┐
  ├── T1.2 Operability 枚举 ────────────┤
  └── T1.3 CommandResult 结构体 ────────┤
                                        ▼
阶段二 (PhysicalAxis 扩展)
  └── T2.1 syncState() ────────────────┐
                                        ▼
阶段四 (FakePLC 扩展)                     |
  └── T4.1 新增寄存器 ──────────────────┤
                                        ▼
阶段三 (Fake 端口)  ◄──── 依赖 T1.1+T4.1
  ├── T3.1 FakeGantryFeedbackPort
  ├── T3.2 FakeGantryCommandPort
  └── T3.3 FakeGantryEventBus
                                        │
                                        ▼
阶段五 (GantrySystem 完整实现)  ◄── 依赖 阶段一~四所有产出
  ├── T5.1 实现 IGantryStateQuery
  ├── T5.2 模式管理
  ├── T5.3 操作可行性
  ├── T5.4 运动命令
  ├── T5.5 状态聚合
  └── T5.6 事件管理
                                        │
                                        ▼
阶段六 (集成测试)  ◄── 依赖 阶段五
  └── T6.1 全流程集成
```

**总预估工作量: 5~8 天**

**推荐执行顺序:**
1. 阶段一 (值对象) → 2. 阶段四 (FakePLC) → 3. 阶段二 (PhysicalAxis) → 4. 阶段三 (Fake端口) → 5. 阶段五 (GantrySystem) → 6. 阶段六 (集成)

阶段一/二/四可并行开发。阶段五是阻塞项，必须等待阶段三完成后再开始。

---

> **参考文档:**
> - `domain/docs/双轴龙门系统_TDD架构设计文档.md` — 20条业务约束与测试套件设计
> - `domain/docs/龙门系统接口业务语义约束.md` — 接口前置/后置条件与不变式
