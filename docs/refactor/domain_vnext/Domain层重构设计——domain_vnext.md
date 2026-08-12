# PLC Domain 层重构设计 —— `domain_vnext`

> 文档状态：设计确认稿
> 日期：2026-08-12
> 适用工程：`PLC_re`、`servoV6`
> 基线：`PLC_re` 当前工作区（2026-08-10），地址依据《PLC变量协议_Modbus最终地址表.md》
> 目标：仿照 `plc_vnext`（已替换旧 `infrastructure/plc`）的做法，以独立库 `domain_vnext` 替换旧 `domain` 层；纯领域 DTO + 状态机 + TDD 驱动，与旧层并行迁移。

---

## 1. 总体思路：照搬 `plc_vnext` 的「独立库 + 纯化 + 边界」套路

`plc_vnext` 之所以能无痛替换旧 `infrastructure/plc`，靠的是三条铁律，本方案原样搬到 Domain 层：

1. **独立 STATIC 库** `domain_vnext`（命名空间 `domain_vnext`），不 include 旧 `domain/*`、不 include `ISystemDriver`、不 include Qt/Modbus/网络，只有纯领域状态机与 DTO。
2. **以 `plc_vnext` 的 contracts 作为基础设施边界**。`plc_vnext::contracts::*` 是纯 DTO（无 I/O、无业务），`domain_vnext` 可安全消费它作为「外设侧快照 / 命令」的端口（依赖方向：domain → plc_vnext contracts），再通过一个薄的 adapter/gateway 在边界完成翻译。
3. **TDD 驱动、按 Phase 递进**，与 `plc_vnext` 的 Step1→Step12 完全同构。

### 1.1 关键分层结论（对齐《PLC16槽位动态轴领域模型与职责配置协议.md》§15）

```text
职责配置(AxisTopology)     -> 决定槽位身份 / A/B 分组 / 功能轴 / HmiVisible / 联动拓扑期望
PLC参数/反馈(D0..D160 等)  -> 决定单轴参数、位置、运动、限位、告警真相
PLC龙门(GantryStatus)      -> 决定联动实际状态
AxisSystem(组合根)         -> 16 槽位注册 + (组,功能) 功能映射 + 全局急停
Axis(统一单轴接口)         -> SingleAxis 普通轴 / CoupledAxis 普通能力 + 联动数据
```

---

## 2. 核心设计决策（对照需求逐条落点）

| 需求 | 决策 |
|---|---|
| 两个分组，各有 X1 X2 Y Z R X | `AxisSystem` 内建 `GroupModel[2]`；每个 `GroupModel` 聚合 `(AxisFunction → Axis&)`。`Axis` 实体本体放在全局 16 槽位注册表，分组只是「功能视图」。功能绑定与 PLC 约定：`Role[0]=X1、Role[1]=X2、Role[2]=Y、Role[3]=Z、Role[4]=R、Role[5]=X（逻辑轴）`。 |
| 展示与否看 `Group[i].Role[i].HmiVisible` | 从 `TopologySnapshot::groups[g].roles[r].hmiVisible`（+ `group.valid/hmiVisible`）构建；只影响 UI 展示，不影响轮询与安全，**不**作为控制准入。 |
| 单轴/联动轴逻辑几乎一样 | 统一 `Axis` 基类提供全部单轴能力；`CoupledAxis` 只追加联动配置 + 联动反馈字段。 |
| 13 项设置 | 领域快照 `AxisParameterSet` 逐项映射（见 §4.1）。 |
| 14 项触发 | 领域命令枚举 `AxisCommandKind` 逐项映射（见 §4.2）。 |
| 全局：设备急停 / 解除 | `SafetyController` 映射 M224/M225，五态锁存状态机。 |
| 龙门：GantryParam 启动/解除联动、GantryStatus 联动状态 | `GantryCouplingController` 从 `GantryStatusSnapshot` 映射出领域联动状态，产生 `GantryRequest`(Couple/Decouple/Reset) + RequestSeq 事务；APPLICATION 是否消费由上层决定（见 §4.5）。 |

---

## 3. 目录结构（对标 `plc_vnext` 的 Step 分层）

```
domain_vnext/                        # 独立 STATIC 库，namespace domain_vnext
├── CMakeLists.txt                   # add_library(domain_vnext STATIC ...) + add_subdirectory(tests)
├── README.md
├── model/                           # 纯值对象 / 领域 DTO（无 I/O）
│   ├── AxisKey.h                    #   (PlcGroupIndex, AxisFunction)
│   ├── AxisFunction.h               #   X/X1/X2/Y/Z/R
│   ├── AxisState.h                  #   MotionState / LimitState / SoftLimitControl
│   ├── AxisParameterSet.h           #   13 项设置快照（读写）
│   ├── AxisCommand.h                #   14 触发 + 参数写命令枚举 + DTO
│   ├── GantryStatus.h               #   GantryStatus 领域映射（State/许可/InGear/Skew）
│   ├── GantryParam.h                #   GantryParam 领域映射（方向/齿轮比/阈值/超时）
│   └── SafetyState.h                #   急停五态
├── state/                           # 状态机（纯领域）
│   ├── AxisStateMachine.h           #   单轴：意图 -> 校验 -> 命令入 Outbox
│   ├── CommandOutbox.h              #   批量意图槽位（解决单一 pending intent 覆盖）
│   ├── GantryCouplingStateMachine.h #   GantryStatus -> 领域联动状态机 + 请求序列
│   └── SafetyStateMachine.h         #   M224/M225 五态
├── system/                          # 组合根（聚合）
│   ├── AxisRegistry.h               #   16 槽位 -> Axis 实体
│   ├── GroupModel.h                 #   (功能 -> Axis&) + gantry 控制器 + HmiVisible
│   ├── AxisSystem.h                 #   组合根：注册表 + GroupModel[2] + Safety
│   ├── SystemBoot.h                 #   TopologySnapshot -> AxisSystem 动态初始化
│   └── FeedbackDispatcher.h         #   RuntimeSnapshot/GantryStatus -> 实体注入
├── command/                         # 命令产出边界
│   ├── SystemCommand.h              #   variant<AxisCommandEnvelope, GantryAction, EStopCommand>
│   └── CommandMapper.h              #   domain command -> plc_vnext::contracts
├── gateway/                         # 领域依赖的驱动抽象（反向，见 §7）
│   └── IPlcDriver.h                 #   面向领域的读/写门面（实现由 plc_vnext 适配）
└── tests/                           # TDD（target: domain_vnext_tests）
    ├── model/test_axis_parameter_set.cpp
    ├── state/test_axis_state_machine.cpp
    ├── state/test_command_outbox.cpp
    ├── state/test_safety_state_machine.cpp
    ├── system/test_system_boot.cpp
    └── command/test_command_mapper.cpp
```

---

## 4. 领域模型逐项映射

### 4.1 `AxisParameterSet` —— 13 项设置

> 关键点：前 7 项由 `plc_vnext::contracts::AxisRuntimeSnapshot` 提供（反馈侧）；后 6 项（8~13）是 RW 参数区，**当前不在** RuntimeSnapshot 里，需要在 plc_vnext 侧补一个参数快照（见 §7）。

```cpp
struct AxisParameterSet {
    // 反馈侧（来自 RuntimeSnapshot，只读注入）
    float manualSpeed       = 0.f;   // 1 手动速度    D(0+2s)
    float positioningSpeed  = 0.f;   // 2 定位速度    D(32+2s)
    float absPosition       = 0.f;   // 3 绝对位置    D(64+2s) R
    float relPosition       = 0.f;   // 4 相对位置    D(96+2s) R
    int16_t motionState     = 0;     // 5 运动状态    D(128+s) R
    int16_t motionLimit     = 0;     // 6 运动限制    D(144+s) R
    uint16_t alarmWord      = 0;     // 7 告警码      D(160+s) R
    // 参数区（RW，需要 plc_vnext 补 AxisParameterSnapshot）
    float relZeroRecord     = 0.f;   // 8 相对原点记录  D(1064+2s)
    float absMoveDistance   = 0.f;   // 9 绝对定位距离  D(1096+2s)
    float relMoveDistance   = 0.f;   //10 相对定位距离  D(1128+2s)
    float softNegLimit      = 0.f;   //11 软件负限位    D(1160+2s)
    float softPosLimit      = 0.f;   //12 软件正限位    D(1192+2s)
    uint16_t softLimitControl = 0;   //13 软限位控制    D(1228+s) bit0正 bit1负
    bool trusted = false;            // 本次读取是否可信
};
```

### 4.2 `AxisCommand` —— 14 项触发 + 参数写

```cpp
enum class AxisCommandKind {
    // ---- 14 项触发（映射到 plc_vnext::PlcAxisCommand，见 §7）----
    EnableAxis,       //  1 M0    使能轴控
    ClearRelZero,     //  2 M16   相对原点清除
    ClearAbsPosition, //  3 M32   绝对位置清零
    TriggerAbsMove,   //  4 M48   绝对定位触发
    TriggerRelMove,   //  5 M64   相对定位触发
    JogForward,       //  6 M80   点动正转
    JogBackward,      //  7 M96   点动反转
    ResetAlarm,       //  8 M112  报警解除触发   ⚠ 需 plc_vnext 补
    EnableMotor,      //  9 M128  使能电机
    StopRelMove,      // 10 M144  相对定位终止
    StopAbsMove,      // 11 M160  绝对定位终止
    SetRelZero,       // 12 M176  相对原点设置
    JogHeartbeat,     // 13 M192  点动心跳
    ClearAlarmWord,   // 14 M208  告警码置零     ⚠ 需 plc_vnext 补
    // ---- 参数写（RW）----
    SetManualSpeed,       // D0
    SetPositioningSpeed,  // D32
    SetAbsDistance,       // D1096
    SetRelDistance,       // D1128
    SetSoftNegLimit,      // D1160   ⚠ 需 plc_vnext 补
    SetSoftPosLimit,      // D1192   ⚠ 需 plc_vnext 补
    SetSoftLimitControl,  // D1228   ⚠ 需 plc_vnext 补
    SetRelZeroRecord,     // D1064   ⚠ 需 plc_vnext 补（如需要直接写）
};
struct AxisCommand { AxisCommandKind kind; float value = 0.f; bool level = false; };
struct AxisCommandEnvelope { plc_vnext::contracts::PlcAxisSlot slot; AxisCommand cmd; };
```

> **解耦铁律（沿用设计稿 §6.5 / §8）**：`SetAbsDistance` 与 `TriggerAbsMove`、`SetRelDistance` 与 `TriggerRelMove` 必须是**四个独立接口**，领域层绝不合并为「设置目标 + 触发」的合并运动接口；写入失败不得继续触发。

### 4.3 命令 Outbox（对标设计稿 §6.9，解决旧 `Axis::m_pending_intent` 单一覆盖问题）

```cpp
struct AxisSequencedCommand { int seq; AxisCommand cmd; };  // 定位设置/触发/终止，严格有序
struct AxisPulseCommand    { AxisCommand cmd; };            // 清零/报警解除等触发
class CommandOutbox {
    DirtyParameterSet params;                 // 普通参数按字段去重
    std::deque<AxisSequencedCommand> motion;  // 保序：set target -> wait accepted -> trigger
    std::deque<AxisPulseCommand> otherPulses; // 其他触发
public:
    void push(const AxisCommand&);
    bool hasAny() const;
    std::vector<AxisCommand> drain();         // 供 CommandMapper 一次取走
};
```

### 4.4 全局急停 `SafetyController`

- 保留旧 `EmergencyStopController` 的五态语义：`NotSynchronized / Running / EmergencyStopping / EmergencyStopped / ReleasingEmergencyStop`。
- 映射：`EmergencyStopCommand{true}` → 写 **M224 ON（锁存保持）**；`{false}` → 写 **M225 上升沿解除**（PLC 自复位并清 M224）。
- 反馈：按设计稿决策②「无独立急停反馈寄存器，M224 ON 即视为已急停」，`applyFeedback` 由我们写 M224 后读回驱动；`isSystemLocked()` 拦截全部轴控制，但遥测仍可读。

### 4.5 龙门映射（`GantryCouplingController` + `GantryParam`）

```cpp
// GantryParam（D1600 + 22g，纯配置 / 只读注入，领域不改写）
struct GantryParamModel {
    bool valid; bool readyToCouple;             // +0 bit0 / GantryReadyToCouple
    int16_t dirX1, dirX2;                       // +1/+2
    int16_t ratioNumX1, ratioDenX1, ratioNumX2, ratioDenX2;  // +3..+6
    float positionOffsetX1, positionOffsetX2;   // +7..+10
    float coupleSkewLimit, runningSkewLimit;    // +11..+14
    int32_t skewDelayMs, coupleTimeoutMs, decoupleTimeoutMs; // +15..+20
    bool trusted;
};

// GantryStatus（D190 + 18g）-> 领域联动状态机映射
struct GantryStatusModel {
    GantryCouplingState coupling;      // 由 raw state 映射
    int16_t rawState;                  // 0未配置 1已解除 2建立中 3已联动 4解除中 5故障
    int32_t ackSeq;                    // 与 RequestSeq 对齐判定
    int16_t commandResult;             // 0/1/2/3/4
    int16_t commandErrorCode;
    bool readyToCouple;                        // +6.bit0
    bool readyToDecouple;                      // +6.bit1  （2026-08-12 地址表新增，解除准入用）
    bool memberControlAllowed, logicalControlAllowed;  // +6.bit2/.bit3
    bool x1InGear, x2InGear;                   // +6.bit4/.bit5
    float x1Position, x2Position, logicalPosition, skew;
    bool fault; int16_t faultCode;             // +15.bit0 / +16
    bool trusted;
};

// 状态映射表（State -> 领域状态）
//  0 -> Unconfigured | 1 -> Decoupled | 2 -> CouplingRequested
//  3 -> Coupled      | 4 -> DecouplingRequested | 5 -> Fault
```

- **启动联动 / 解除联动**：`GantryCouplingController.requestCouple() / requestDecouple() / requestReset()` 产出 `GantryRequest{Couple|Decouple|Reset, requestSeq}`；`requestSeq` 由领域 / 会话自增，交给 `gateway.submitGantryRequestDetailed(g, req)`，写 `D180..D182`（Command → RequestSeq 有序两笔）。

- **事务闭环（2026-08-12 地址表 §10 多条件判定，替换旧的单条件 `ackSeq==requestSeq && commandResult==2`）**：
  - 建立成功：`AckSeq == requestSeq && CommandResult == 2 && State == 3 && X1InGear && X2InGear && LogicalControlAllowed == TRUE`；
  - 解除成功：`AckSeq == requestSeq && CommandResult == 2 && State == 1 && X1InGear == FALSE && X2InGear == FALSE && MemberControlAllowed == TRUE`；
  - 复位：`Command == 3`，PLC 先停 SYN0、再依次 GearOut，上位机不得跳过清理。

- **准入（建 / 解 / 复位，§10）**：建立需 `ConfigValid == TRUE && State == 1 && ReadyToCouple == TRUE && Fault == FALSE`；解除 / 复位需基于 `readyToDecouple` / `fault` 判定，由 `GantryCouplingController` 在产出命令前校验。

- **APPLICATION 不一定用**：领域层只负责把 `GantryStatusSnapshot` 映射成 `GantryStatusModel` 并维护联动状态机；上层若用 `GantryOrchestrator` 之类的流程，可完全基于该领域状态再编排，也可忽略。

### 4.6 A 组使能入口路由与命令写策略（2026-08-12 地址表补充）

**a) 使能入口路由（关键修正）**

`MAIN.LD` 已做如下使能同步接线：

```text
使能轴控[0] := 使能轴控[13]    使能轴控[1] := 使能轴控[13]
使能电机[0] := 使能电机[13]    使能电机[1] := 使能电机[13]
```

因此 **A 组统一控制时，`EnableAxis` / `EnableMotor` 一律路由到逻辑轴入口 `M13` / `M141`（槽位 13），只发一份，不得再并行写 `M0/M1`、`M128/M129`**。domain 在 `CommandMapper` / `AxisSystem` 增加「组内使能入口 = 逻辑轴槽位」路由：龙门成员（X1/X2）的使能命令改写为逻辑轴槽位；普通轴（Y/Z/R）仍写各自槽位。

**b) 命令写策略表（区分 PLC 自复位 / 保持电平 / 周期刷新 / 锁存）**

| 类别 | 命令 | 上位机规则 |
|---|---|---|
| 保持电平 | `EnableAxis` / `EnableMotor` / `JogForward` / `JogBackward` | 按启停写 ON/OFF |
| PLC 自复位（只写 ON，读回确认；PLC 置 ON 后自动复位 OFF） | `TriggerAbsMove` / `TriggerRelMove` / `StopRelMove` / `StopAbsMove` / `ClearRelZero` / `ClearAbsPosition` / `SetRelZero` / `ClearAlarmWord` | 只写 ON，无需配对 OFF |
| 周期刷新 | `JogHeartbeat` | 周期写 ON，PLC 扫描清 OFF |
| 锁存 / 上升沿 | 急停 `M224` / 解除 `M225` | M224 保持；M225 沿解除并清 M224 |

> 注：`TriggerAbsMove` / `TriggerRelMove` / `StopRelMove` / `StopAbsMove` 已确认由 **PLC 自复位**（写 ON 后 PLC 自动置 OFF），与清零 / 原点 / 告警类同为「只写 ON，读回确认」，**无需客户端回写 OFF**。domain 命令产出需携带写策略，由 `CommandMapper` 落为 plc_vnext 的 `CommandWritePolicy`（保持 / 自复位 / 周期 / 锁存）。

### 4.7 运动状态 / 运动限制编码（2026-08-12 地址表 §3）

```cpp
enum class MotionState : std::uint16_t {
    ControlNotEnabled = 0,  // 轴控入口未使能
    Idle = 1,
    JogForward = 2,  JogBackward = 3,
    MovingAbsolute = 4, MovingRelative = 5
};
enum class LimitState : std::uint16_t {
    None = 0, PositiveSoftware = 1, NegativeSoftware = 2,
    PositiveHardware = 3, NegativeHardware = 4
};
```

（旧领域枚举 `AxisState` 的 `Disabled/Idle/Jogging/MovingAbsolute/MovingRelative/Error` 与前者对齐；`AxisFeedback.motionState` 直接映射 `D128`，`motionLimit` 直接映射 `D144`，不再由旧 Coil 组合推导。）

---

## 5. 两条核心链路

### 5.1 反馈注入（plc_vnext → domain_vnext）

```
IPlcRuntimeGateway.readTopology() -> TopologySnapshot
   -> SystemBoot::initialize(AxisSystem&, topology)      // 动态建轴 / 分组 / 映射 + HmiVisible
IPlcRuntimeGateway.readRuntime()  -> RuntimeSnapshot (+AxisParameterSnapshot)
   -> FeedbackDispatcher::dispatch(AxisSystem&, runtime, params)
        for g in 0..1:
            for (function, axis) in GroupModel[g]:
                axis.applyFeedback(slotSnapshot)          // 前 7 项
                axis.applyParameters(paramSnapshot)       // 8~13 项
            GroupModel[g].gantryCoupling.applyFeedback(gantrySnapshot)  // 龙门状态
        AxisSystem.safety().applyFeedback(estop)          // M224 读回
```

### 5.2 命令产出（domain_vnext → plc_vnext）

```
ApplicationUseCase
  -> AxisSystem.find(group=0, function=X) -> axis
  -> axis.startJog(dir)                     // 领域校验 + 意图入 Outbox
  -> axis.outbox().drain() -> vector<AxisCommand>
  -> CommandMapper::map -> vector<plc_vnext::contracts::PlcAxisCommand>
  -> gateway.writeAxis(slot, cmd)           // IPlcRuntimeGateway
龙门:
  GroupModel[0].gantryController.requestCouple()
  -> CommandMapper -> plc_vnext::contracts::GantryRequest + seq
  -> gateway.submitGantryRequestDetailed(g, req)
```

---

## 6. 依赖方向与 `gateway/` 抽象

- `domain_vnext/model | state | system` 只依赖自身 + `plc_vnext::contracts`（纯 DTO），**不**依赖 `plc_vnext` 的 transport / layout 实现细节。
- `domain_vnext/gateway/IPlcDriver.h` 是领域侧定义的抽象（读拓扑 / 读运行 / 写轴 / 提交龙门），由一层适配器（可放 `infrastructure/plc_vnext` 或一个 `PlcRuntimeDriverAdapter`）实现为对 `IPlcRuntimeGateway` 的调用。这样 domain 保持「面向抽象」，测试用 `FakePlcRuntimeGateway`（plc_vnext 已提供）。
- **明确不依赖**：旧 `domain/*`、`infrastructure/ISystemDriver`、Qt、Modbus。

---

## 7. 需要 `plc_vnext`（基础设施侧）补充的点

`plc_vnext` 已覆盖大部分，但按 14 触发 / 13 设置清单还差几项，需在 plc_vnext（保持其纯 DTO 原则）增补：

1. `PlcAxisCommandKind` 增加：`ResetAlarm`(M112)、`ClearAlarmWord`(M208)、`SetSoftNegLimit`(D1160)、`SetSoftPosLimit`(D1192)、`SetSoftLimitControl`(D1228)、`SetRelZeroRecord`(D1064)。
2. 新增单槽位参数反馈快照 `contracts::AxisParameterSnapshot`（相对原点记录 / 绝对定位距离 / 相对定位距离 / 软负 / 软正 / 软限位控制）+ decoder + 并入读计划（`AxisSlotRegisterLayout` 公式已具备）。
3. `GantryParam` 读回快照（D1600 + 22g）目前在 `IPlcRuntimeGateway` / 读计划中尚无对应 read；若 domain 需要 `readyToCouple` / 阈值做准入，需补 `GantryParamSnapshot` 读取。
4. **命令写策略归类**：`PlcAxisCommand` 需携带写策略（保持电平 / PLC 自复位 / 周期刷新 / 锁存），供 `CommandMapper` 按 §4.6b 表落为 `CommandWritePolicy`；其中 `TriggerAbsMove / TriggerRelMove / StopRelMove / StopAbsMove` 已确认由 **PLC 自复位**（写 ON 后自动置 OFF），与清零 / 原点 / 告警类一样只写 ON、读回确认，无需客户端回写 OFF。

> `ResetAlarm(M112)` 当前地址表标注「不可用 / 已注释」（PLC 未实现复位逻辑）。领域接口**保留**，映射层按 PLC 能力返回 `UnsupportedByPlcVersion`，与设计稿 §6.5 / 验收 13 一致。

---

## 8. TDD 实施阶段（仿 plc_vnext Step 递进）

| Phase | 交付物 | 验证 |
|---|---|---|
| **P1 model** | `AxisKey / AxisFunction / AxisParameterSet / AxisCommand / GantryStatus / GantryParam / SafetyState` | 值对象默认值、13 项字段、14 触发枚举、State→领域状态映射 |
| **P2 state** | `AxisStateMachine` + `CommandOutbox` + `SafetyStateMachine` + `GantryCouplingStateMachine` | 意图校验、Outbox 覆盖/保序、急停五态、联动状态机流转 |
| **P3 system** | `AxisRegistry / GroupModel / AxisSystem / SystemBoot` | 从 `TopologySnapshot` 动态建轴、A/B 分组、`HmiVisible` 展示判定、重复/非法配置降级锁定 |
| **P4 link** | `FeedbackDispatcher` + `CommandMapper` | RuntimeSnapshot→实体注入；domain command→`PlcAxisCommand` / `GantryRequest` 映射（用 `FakePlcRuntimeGateway`） |
| **P5 app** | `SystemManager` / UseCase / ViewModel 迁移到 `domain_vnext`，龙门编排可选 | 应用层编译 + 集成测试 |
| **P6 清理** | 删旧 `domain/*`、`infrastructure/ISystemDriver` 相关 | 全量回归 |

---

## 9. 旧 domain → domain_vnext 迁移对照

| 旧 | 新 |
|---|---|
| `AxisId`（6 轴枚举） | `AxisFunction` + `PlcGroupIndex` + `PlcAxisSlot` |
| `SystemContext`（固定 6 轴 + 控制器） | `AxisSystem` + `GroupModel` |
| `Axis`（单 pending intent） | `Axis` 基类 + `SingleAxis` / `CoupledAxis` + `CommandOutbox` |
| `GantryCouplingController`（五态） | `GantryCouplingStateMachine`（从 `GantryStatusSnapshot` 映射） |
| `GantryPowerController` | 撤销 → A 组统一走逻辑轴使能入口 `EnableMotor(M141)`（PLC 已做 `使能电机[0]:=[13]、[1]:=[13]` 同步，不并行写 M128/M129） |
| `EmergencyStopController` | `SafetyController`（M224 / M225） |
| `SystemCommand` variant | 新 `SystemCommand` + `CommandMapper` → `PlcAxisCommand` / `GantryRequest` |
| `ISystemDriver.send / pollFeedback` | `IPlcRuntimeGateway` + `FeedbackDispatcher` / `CommandMapper` |
| `AxisSyncService` | `FeedbackDispatcher` |

---

## 10. 风险与待确认项

1. **Y/Z/R 的绑定来源（已确认）**：与 PLC 约定 `Role[2]=Y、Role[3]=Z、Role[4]=R`。完整功能绑定为 `Role[0]=X1、Role[1]=X2、Role[2]=Y、Role[3]=Z、Role[4]=R、Role[5]=X（逻辑轴）`。`RoleBindingResolver` 据此数据驱动建轴；`职责 D1244` 不消费，仍不参与实时控制。
2. **报警解除 M112** 当前 PLC 未实现，领域接口保留但需能力位判断。
3. **GantryParam 的「启动联动 / 解除联动」**：`GantryParam`(D1600) 是**配置区**（方向 / 齿轮比 / 阈值 / 超时），真正的建立 / 解除动作走 `GantryCommand`(D180) 事务。设计时需区分「参数注入」与「动作命令」，避免把配置当命令。
4. **参数区 8~13 读回** 依赖 plc_vnext 补 `AxisParameterSnapshot`（P4 前落地）。
5. **B 组** 当前 `Group[1].Valid=FALSE`，domain 仍需能构建但标记 `not-ready`，不开放控制；B 组龙门 state machine 适配未完成，禁止启用。

---

## 11. 依据《PLC变量协议_Modbus地址表_2026-08-12》的核对与补充

> 本节以 `docs/refactor/PLC变量协议_Modbus地址表_2026-08-12.md`（2026-08-12 编译产物）为权威源，逐节核对 domain 层设计并列出遗漏与修订。正文相关小节已同步修正。

### 11.1 发现的遗漏点

| # | 遗漏 | 严重度 | 处理 |
|---|---|---|---|
| 1 | **A 组使能入口路由**：`MAIN.LD` 已做 `使能轴控/使能电机 [0]:=[13]、[1]:=[13]` 同步，统一控制必须走逻辑轴入口 `M13/M141`，不得并行写 `M0/M1、M128/M129` | 高 | 新增 §4.6a，`CommandMapper`/`AxisSystem` 增加使能入口路由 |
| 2 | **触发命令写策略**：定位触发/终止（M48/M64/M144/M160）原按「ON 后必须回写 OFF」设计；**已确认 PLC 对触发/终止实现自复位（写 ON 后自动置 OFF）**，统一归入 PLC 自复位类 | 高 | §4.6b 命令写策略表已更新；§7 补 `CommandWritePolicy` |
| 3 | **GantryStatus 缺 `ReadyToDecouple`**（+6.bit1），解除准入缺失 | 中 | §4.5 `GantryStatusModel` 补充字段 |
| 4 | **龙门使能入口**：撤销 GantryPowerController 后应统一 `EnableMotor(M141)`，非 `EnableMotor(X1)+EnableMotor(X2)` | 高 | 修订 §9 迁移对照 |
| 5 | **龙门事务闭环条件不完整**：建立/解除需多条件（State/InGear/许可位） | 中 | §4.5 替换单条件闭环为多条件判定 |
| 6 | **运动状态 / 运动限制编码**未在领域枚举给出 | 低 | 新增 §4.7 `MotionState` / `LimitState` |

### 11.2 逐节核对结果

| 地址表节 | 内容 | domain 覆盖 | 结论 |
|---|---|---|---|
| §3 标准 16 轴 D 区 | 13 项设置 + 运动/限制编码 | `AxisParameterSet` 已覆盖字段 | 补 §4.7 编码 |
| §4 标准 16 轴 M 区 | 14 触发 + 使能同步 + 写策略 | `AxisCommandKind` 覆盖 14 项 | 补 §4.6 入口路由 + 写策略（触发/终止为 PLC 自复位） |
| §5 AxisTopology | group/role/HmiVisible、Role[0]/[1]/[5] | `SystemBoot` 已覆盖 | 补充 Role[2]/[3]/[4]=Y/Z/R 绑定 |
| §6 GantryParam | 方向/齿轮比/阈值/超时 | `GantryParamModel` 已覆盖 | 无遗漏 |
| §7 GantryCommand | Command/RequestSeq 事务 | `GantryRequest` 已覆盖 | 补完整闭环条件 |
| §8 GantryStatus | 状态/许可/InGear/ReadyToDecouple | `GantryStatusModel` | 补 `readyToDecouple` |
| §9 SYN0 逻辑轴接口 | 联动后仅逻辑轴运动，入口走 slot13 | `GroupModel` 绑定逻辑轴槽位 | 确认使能入口路由 |
| §10 配置与运行流程 | 建/解/复位准入与闭环 | `GantryCouplingController` | 补多条件判定 |
| §11 兼容区 | 旧联动区/报警解除禁用 | domain 不消费 | 无遗漏（`ResetAlarm` 保留能力位） |

### 11.3 说明

- `ResetAlarm(M112)` 在 2026-08-12 表仍标注「未实现 / 禁止正式 UI」，domain 接口保留、按 PLC 能力返回 `UnsupportedByPlcVersion`（与 §7 一致）。
- 运动状态 / 限制直接解码 `D128 / D144`，确认无需再走旧 Coil 组合推导（与《PLC16槽位动态轴领域模型与职责配置协议.md》§12.4 一致）。
- **触发命令写策略（用户确认）**：`TriggerAbsMove / TriggerRelMove / StopRelMove / StopAbsMove` 已由 PLC 实现自复位（写 ON 后自动置 OFF），统一按「只写 ON、读回确认」处理，无需客户端回写 OFF（见 §4.6b）。
- **Y/Z/R 功能绑定（用户确认）**：与 PLC 约定 `Role[2]=Y、Role[3]=Z、Role[4]=R`，完整绑定见 §2 / §10（见 §11.2 §5 行）。
- 本次核对仅修订领域语义与命令路径，不涉及 `AxisTopology` / `GantryParam` 布局本身（由 plc_vnext layout 负责）。

---

## 附录：参考文件索引

| 层 | 文件 | 说明 |
|---|---|---|
| Infrastructure | `infrastructure/plc_vnext/contracts/TopologySnapshot.h` | AxisTopology 原始配置快照（header/group/role/hmiVisible） |
| Infrastructure | `infrastructure/plc_vnext/contracts/AxisRuntimeSnapshot.h` | 单槽位运行反馈（前 7 项设置） |
| Infrastructure | `infrastructure/plc_vnext/contracts/RuntimeSnapshot.h` | 16 槽位 + 龙门状态集合 |
| Infrastructure | `infrastructure/plc_vnext/contracts/PlcAxisCommand.h` | 单轴命令（14 触发中的 12 项已覆盖） |
| Infrastructure | `infrastructure/plc_vnext/contracts/GantryRequest.h` / `GantryStatusSnapshot.h` | 龙门命令 / 状态 |
| Infrastructure | `infrastructure/plc_vnext/IPlcRuntimeGateway.h` | 上层门面接口 |
| Domain（旧） | `domain/entity/SystemContext.h`、`Axis.h` | 被替换对象 |
| Domain（旧） | `domain/gantry/GantryCouplingController.h`、`EmergencyStopController.h` | 语义保留，实现替换 |
| 文档 | `docs/architecture/PLC16槽位动态轴领域模型与职责配置协议.md` | 领域模型 / 职责协议权威设计 |
| 文档 | `docs/refactor/PLC变量协议_Modbus地址表_2026-08-12.md` | **权威源**：2026-08-12 编译产物地址表（§11 核对依据） |
| 文档 | `docs/refactor/PLC变量协议_Modbus最终地址表.md` | 全部地址与字段权威表 |
| 文档 | `docs/refactor/PLC龙门联动配置与上位机操作指南.md` | 龙门状态机 / 操作边界 |

---
