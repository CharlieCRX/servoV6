# 龙门联动运动策略 —— application_vnext 实施文档

> 状态：**方案已定稿；P0/P1 评审问题已全部修复；单测全绿
> （application_vnext_tests 42/42、domain_vnext_tests 91/91）；尚未进入真机阶段。
> 按下方"分阶段实施计划"逐阶段推进与验收。**
> 依据：《application_vnext架构与接口说明》、
> 《PLC龙门联动控制逻辑》、《PLC龙门联动配置与上位机操作指南》。

## 1. 背景与需求

龙门联动把虚轴 13（X 逻辑轴 / SYN0，A 组实际绑定）**作为一个单轴看待**，但其
启动与关闭必须与普通单轴区分。用户确认的 PLC 行为：

### 1.1 建立联动（把 13 轴当单轴控制的前置）
```text
1. 使能轴控[13] = ON
2. 使能电机[13] = ON
3. GantryCommand[0].Command = 1（建立）
   若 GantryStatus[0].CommandErrorCode != 0，则先：
     3.1 Command = 3（复位）
     3.2 RequestSeq = RequestSeq + 1
   再执行 3
4. GantryCommand[0].RequestSeq = RequestSeq + 1（只要不等于当前值即可）
```
联动成功提示：
```text
State = 3 | InternalStep = 80 | LogicalControlAllowed = ON | MemberControlAllowed = OFF
```
此后才能正常控制 13 轴号的所有运动（速度/目标/点动/定位/停止/心跳）。

### 1.2 解除联动（恢复 X1/X2 成员控制前）
```text
1. GantryCommand[0].Command = 2（解除）
2. GantryCommand[0].RequestSeq = RequestSeq + 1
```
解除成功提示：
```text
CommandErrorCode = 0 | InternalStep = 10
LogicalControlAllowed = OFF | MemberControlAllowed = ON
```
**之后**才允许 `使能电机[13] = OFF`。

### 1.3 关键约束
- 联动后只能控制 SYN0；解除后才开放 X1/X2 实体轴控制（PLC 已按权限位约束）。
- 解除流程由 PLC 执行 Halt、等稳定、依次 GearOut，上位机**不得跳过**直接写 GearOut。
- 上位机不直接暴露"写 Command=1/2/3"，只暴露业务意图。

## 2. 总体架构

```text
GantryMotionApi（业务意图入口，按 组 -> AxisFunction::X -> 拓扑解析逻辑轴 slot）
 ├─ GantryLifecyclePolicy  ：拥有电源 —— 建立/复位/解除/掉电（单一策略，防并发）
 ├─ GantryMotionGuard      ：判断 slot13 是否具备运动许可（严格准入 + 运行中失联检测）
 └─ Abs/Rel/Jog            ：PowerOwnership::LifecycleManaged —— 仅负责运动，
                             不使能、不掉电，运行前/中由 guard 把关
```

核心原则：
- **生命周期拥有电源，逻辑轴运动策略只拥有运动** —— 把 13 轴对调用者表现得像单轴，
  同时不破坏 PLC 对建立/解除/掉电顺序的约束。
- **复用现有运动内核**（Abs/Rel/Jog 的"触发→运行→停止"骨架），差异集中在使能/掉电两个
  边界点，用 `PowerOwnership` 表达，而非复制一套龙门运动策略。
- **不硬编码槽位 13**：运行时从拓扑解析（`AxisFunction::X` → slot），A 组=13，未来 B 组免改。
- **UI 或普通 `AxisMotionApi` 不直接控制 X1/X2 或 slot 13**，统一经 `GantryMotionApi`。


## 3. 设计决策

| # | 决策 | 理由 |
| --- | --- | --- |
| D1 | `PowerOwnership{SelfManaged, LifecycleManaged}` 枚举代替布尔开关 | 不可变配置，避免 `setExternalEnable(bool)` 误用；独立轴行为完全不受影响 |
| D2 | `LifecycleManaged` 下策略跳过 `EnsuringEnabled` / `Disabling`，`disableMotor()` 为 no-op | 统一拦截所有掉电路径，最小侵入三个策略 |
| D3 | `GantryLifecyclePolicy` 为**单一**生命周期策略（Couple/Decouple 共用） | 每组同时只能一个活动生命周期操作，避免 Couple/Reset/Decouple 并发互相覆盖 `RequestSeq` |
| D4 | 启动顺序按用户确认：使能轴控→使能电机→等 `motionState==2`→(Reset)→Couple | 与 PLC 实际接收顺序一致，不采用"Couple 后才 Enable" |
| D5 | `GantryMotionGuard` 承担严格运动准入（State=3/InternalStep=80/InGear/LogicalAllowed 等） | 领域 `AxisStateMachine` 只在 `Unconfigured` 时拒绝，不足为龙门保护 |
| D6 | `SystemManagerVnext` 缓存最近 `GantryStatusModel` 完整快照 | `GantryCouplingStateMachine` 只保留 state/fault/readyTo*，无 `InternalStep`/`InGear`/许可位，guard 与生命周期需读取完整快照 |
| D7 | `submitGantryRequestDetailed`（保留 `CommitUncertain`），提交不确定时**不重发**，进入等待靠 poll 观察 AckSeq | 按迁移方案约定，绝不因写结果未知而重发新序号 |
| D8 | 运动完成/失联**不直接掉电** | 需先解除、确认 `MemberControlAllowed`，再由生命周期 `EnableMotor=OFF` |
| D9 | 解除后只关 `EnableMotor[13]`；`EnableAxis[13]` 按当前约定不擅自关闭 | 遵循 PLC 约定 |
| D10 | 单轴入口 group-aware（新增带 `PlcGroupIndex` 重载，旧接口=group0 包装） | 最终 A/B 组架构；保留现有调用方与测试兼容 |

## 4. 已落地实现（对应 9 步开发顺序）

| 步骤 | 内容 | 文件 | 状态 |
| --- | --- | --- | --- |
| 1 | `GantryStatusModel` 补 `internalStep` 字段 + 映射 | `domain_vnext/model/GantryStatus.h`、`tests/model/test_gantry_status.cpp` | ✅ 已编译 + 测试通过 |
| 2 | 单轴入口 group-aware（新增 `PlcGroupIndex` 重载，旧接口=group0 包装） | `application_vnext/SystemManagerVnext.h` | ✅ 已编译 + 32 测试通过 |
| 3 | 新增 `PowerOwnership` 枚举 | `application_vnext/policy/AxisMotionCommon.h` | ✅ 已编译 |
| 4 | Abs/Rel/Jog 支持 `PowerOwnership`（跳使能/掉电 + 失联停） | `AbsMovePolicy.h`/`RelMovePolicy.h`/`JogPolicy.h` | ✅ 已编译 |
| 5 | `GantryMotionGuard`（运动准入 + 失联检测） | `application_vnext/policy/GantryMotionGuard.h`（新增） | ✅ 已编译 |
| 6 | `GantryLifecyclePolicy`（Reset→Couple→Ready；Stop→Decouple→DisableMotor） | `application_vnext/policy/GantryLifecyclePolicy.h`（新增） | ✅ 已编译 |
| 7 | `GantryMotionApi`（业务意图入口，组→X→拓扑解析 slot） | `application_vnext/policy/GantryMotionApi.h`（新增） | ✅ 已编译 |
| 8 | 龙门策略测试 | `application_vnext/tests/test_gantry_policy.cpp`（新增，已加 CMakeLists） | ✅ 已编译 + 测试通过 |
| 9 | 真机闭环 | — | ⏳ 现场 |

> 验证状态说明（已更新）：
> - 步骤 1：`domain_vnext_tests` 全绿（**91/91**，含 `InternalStepMappedFromSnapshot`）。
> - 步骤 2：单轴入口 group-aware 无回归（`application_vnext_tests`）。
> - 步骤 3~8：`application_vnext_tests` **42/42 全绿**（含新增龙门策略测试：guard 准入、
>   lifecycle 建立/复位/解除、LifecycleManaged 不使能不掉电、运行中失联即停）。
> - P0/P1 评审问题已全部修复（见 §7「修复记录」）。

## 5. 分阶段实施计划

> 原则：**每阶段都有可独立验证的完成判据**，验收通过后再进入下一阶段。
> 当前状态：**阶段 A 已完成**（P0/P1 已修复，单测全绿）；下一步进入阶段 B/C（真机前）。

### 阶段 A —— 单测闭环（本地，无真机）✅
**内容**：完成第 1~8 步代码，编译并运行 `application_vnext_tests`，确保新增龙门策略测试全绿、旧测试无回归。

**完成判据**：
- [x] `domain_vnext_tests` 全绿（91/91，含 `GantryStatusModelTest.InternalStepMappedFromSnapshot`）
- [x] `application_vnext_tests` 全绿（42/42，含新增 `test_gantry_policy.cpp`）：
  - `GantryMotionGuard`：耦合放行 / 各失效条件拒绝
  - `GantryLifecyclePolicy`：建立→Ready、错误码前置 Reset→Couple、解除→掉电→Done
  - `PowerOwnership::LifecycleManaged`：不使能、不掉电、运行中失联即停（发 Stop 不直接掉电）
  - `GantryMotionApi::beginAbs`：解析到逻辑轴 slot13，LifecycleManaged 不使能
- [x] 旧 `test_axis_motion_policy.cpp` 与 `test_system_manager_vnext.cpp` 无回归

### 阶段 B —— 联机探针接入 ✅（代码完成，真机验证并入阶段 C）
**内容**：扩展 `tools/plc_vnext_motion_probe`，经 `GantryMotionApi` 暴露龙门 action，现场可逐帧打印
`step / ms / pos / gantryState / internalStep`。

**探针用法**（A 组 g=0）：
```bash
# 建立联动并使能逻辑轴（->Ready）
plc_vnext_motion_probe.exe --host IP --group 0 --action gantry-couple --confirm-write
# 龙门下绝对/相对定位（前提已 couple 到 Ready）
plc_vnext_motion_probe.exe --host IP --group 0 --action gantry-move-abs --value 100 --confirm-write --confirm-motion
plc_vnext_motion_probe.exe --host IP --group 0 --action gantry-move-rel --value -30 --confirm-write --confirm-motion
# 龙门下点动（显式停止）
plc_vnext_motion_probe.exe --host IP --group 0 --action gantry-jog-forward --duration-ms 2000 --confirm-write --confirm-motion
# 解除联动并掉电逻辑轴
plc_vnext_motion_probe.exe --host IP --group 0 --action gantry-decouple --confirm-write
```

**完成判据**：
- [x] 探针代码接入 `GantryMotionApi`：`gantry-couple / gantry-decouple / gantry-move-abs / gantry-move-rel / gantry-jog-*`
- [x] 探针在龙门分支注入 `applyGantryConfig(valid=true)`（`requestCouple` 的 configValid 准入来源）
- [x] 单测覆盖 `GantryMotionApi`（未绑定组返回 Error 策略、目标/停止落到逻辑轴），`application_vnext_tests` 44/44 全绿
- [ ] **真机验证**（并入阶段 C）：对真实 PLC 发起"建立 → 使能 → 低速小位移 → 停止 → 解除 → 掉电"并打印每步状态；`CommitUncertain` 路径可观察（提交不确定时探针不重发，继续 poll 等 Ack）

### 阶段 C —— 真机闭环验证（现场）
**内容**：用探针或生产入口执行完整龙门闭环，验证建立/解除顺序与 PLC 权限位一致。

**完成判据**（对齐《PLC龙门联动配置与上位机操作指南》§14 验收）：
- [ ] 建立后：`State=3 / InternalStep=80 / LogicalControlAllowed=ON / MemberControlAllowed=OFF`，
  L0/L1 普通运动被禁、SYN0 逻辑运动被允许
- [ ] SYN0 正反向小距离运动完成，X1/X2 跟随无指令错误、偏差未超阈值
- [ ] 运动中实体轴独立命令被清除/拒绝
- [ ] 解除后：`InternalStep=10 / MemberControlAllowed=ON`，恢复 X1/X2 控制，最后 `EnableMotor[13]=OFF`
- [ ] 异常：偏差超时/逻辑轴 Halt 超时进入 Fault，`Command=3` 能安全解除恢复
- [ ] 拓扑失效时按安全清理，配置恢复后不自动重新建立联动

### 阶段 D —— 生产接入（替换旧编排器）
**内容**：把 `GantryMotionApi::beginEnableAndCouple / beginAbs / beginDecoupleAndDisable`（非阻塞）
挂到主 poll 循环，替换旧 `application/policy/GantryOrchestrator`；UI 只调用业务意图。

**完成判据**：
- [ ] 生产入口切到 vnext 龙门链路，旧的龙门编排器退役
- [ ] UI 经 `GantryMotionApi` 展示联动/成员/逻辑运动控制，权限以 PLC 许可位为最终依据

## 6. 应用层接口形态（业务意图，不暴露 Command=1/2/3）

```cpp
// 生命周期（拥有电源）
GantryLifecyclePolicy beginEnableAndCouple(PlcGroupIndex g);
GantryLifecyclePolicy beginDecoupleAndDisable(PlcGroupIndex g);

// 目标 / 速度 / 停止（写逻辑轴 X）
AppVnextResult setPositioningSpeed(PlcGroupIndex g, float v);
AppVnextResult setAbsTarget(PlcGroupIndex g, float v);
AppVnextResult setRelTarget(PlcGroupIndex g, float v);
AppVnextResult stop(PlcGroupIndex g);

// 运动（LifecycleManaged + guard）
AbsMovePolicy beginAbs(PlcGroupIndex g);
RelMovePolicy beginRel(PlcGroupIndex g);
JogPolicy   beginJog(PlcGroupIndex g, bool forward, int durationMs, int heartbeatPeriodMs = 500);

// 阻塞便利（联机调试）
MoveOutcome runAbs(PlcGroupIndex g, float target);
MoveOutcome runRel(PlcGroupIndex g, float delta);
JogOutcome  runJog(PlcGroupIndex g, bool forward, int durationMs);
```

`GantryLifecyclePolicy` 状态机（每组一个活动操作）：

```text
建立：ValidatePreconditions -> EnsureAxisControl -> WaitAxisControlReady
      -> EnsureMotor -> WaitMotorReady(motionState==2) -> CheckGantryError
      -> [CommandErrorCode!=0] SubmitReset -> WaitResetFinal
      -> SubmitCouple -> WaitCoupleFinal(State=3/Step=80/LogicalAllowed/InGear) -> Ready
解除：EnsureLogicalAxisStopped -> SubmitDecouple -> WaitDecoupleFinal(State=1/Step=10/MemberAllowed)
      -> DisableMotor(EnableMotor[13]=OFF) -> Done
```

## 7. 修复记录（评审 P0/P1 → 已修复，单测全绿）

### 7.1 P0（阻断，已修复）

| 问题 | 修复 |
| --- | --- |
| 解除流程无法推进（`beginDecouple` 未解析逻辑轴） | `beginDecouple()` 立即 `findLogicalAxis()`，找不到 X 直接 Error |
| 无效逻辑轴可能误操作 slot15 | `GantryMotionApi::beginXxx` 解析失败返回 `setUnavailable()` 的 Error 策略，不再用有效 PLC slot 当哨兵 |
| 未校验 AckSeq | 新增 `GantryCouplingStateMachine::lastRequestSeq()` + `SystemManagerVnext::lastGantryRequestSeq()`；Reset/Couple/Decouple 均以 `AckSeq==本次 RequestSeq` 闭环 |
| Reset 判定过宽 | `WaitResetFinal` 用 `resetFinalSatisfied()`：`AckSeq==resetSeq && CommandResult==2 && State==1 && InternalStep==10 && MemberControlAllowed && CommandErrorCode==0` |
| 解除停止无超时/失败处理 | `EnsureLogicalAxisStopped` 检查 `stop()` 结果 + 停止超时（`kDecoupleTimeoutSeconds`），超时 Error |
| 掉电写失败仍报 Done | `DisableMotor` 检查 `enableMotor(...,false)` 结果，失败 Error |
| 建立前置检查不足 | `ValidatePreconditions` 增加：急停 / 龙门状态可信 / 组就绪 / 已解除(State==1) / 非故障 |

### 7.2 P1（已修复）

1. `WaitAxisControlReady` 真正等待 `motionState==1`（轴控 ON）再开电机（带超时）。
2. `runRel()` 到位校验目标改为 `startPosition + delta`（不再把 delta 当绝对位置）。
3. 参数预置（`setAbsTarget/setRelTarget/setPositioningSpeed`）保持"允许预写"（PLC 参数写不触发运动），运动触发严格由 `GantryMotionGuard` 把关；`stop()` 始终允许。
4. `JogPolicy::LifecycleManaged::start()` 补 `m_idleReachedTime` 初始化，统一三策略 0.4s 后延时。
5. `setPowerOwnership()` 仅 `start()` 前可设（Abs/Rel 在 `Step::Initial`、Jog 在 `Step::Idle` 才生效），运行中不可切换。

## 8. 待办与风险

- **阶段 A 完成**：P0/P1 已修复，单测全绿（application_vnext_tests 42/42、domain_vnext_tests 91/91）。
- **超时参数**：`GantryLifecyclePolicy` 内 Reset/Couple/Decouple 超时固定 5s，`kMotorReadyTimeoutSeconds` 复用 `kEnableTimeoutSeconds`（2s）。是否从 `GantryParam.coupleTimeoutMs/decoupleTimeoutMs` 取值待确认（当前未接入参数区）。
- **`EnableAxis[13]` 关闭策略**：当前解除只关 `EnableMotor[13]`，`EnableAxis[13]` 保持 ON（遵循现有约定）。若现场要求解除后同时关轴控，需再确认。
- **B 组（SYN1）**：`GantryMotionApi` 按 `组->X->拓扑解析` 已天然支持多组，但 PLC 侧 B 组未开放，UI 不得开放控制（沿用 `GroupModel::isReady()` 门禁）。
- **`submitGantryRequestDetailed`**：底层已委托 detailed 入口、保留 `GantrySubmitState`；`GantryLifecyclePolicy` 已处理 `CommitUncertain`（不重发、进入等待）。真机需验证 ack 闭环路径。
- **阶段 B/C（探针 + 真机闭环）**：见 §5 分阶段计划，进入真机前需联机探针接入。



