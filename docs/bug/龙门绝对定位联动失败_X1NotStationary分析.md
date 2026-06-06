# 龙门绝对定位联动失败：X1NotStationary 分析

## 问题摘要

龙门 X 轴绝对定位运动（`GantryAbsMovePolicy`）在使能龙门电机后尝试联动（Coupling）时，PLC 返回错误码 4（`X1NotStationary`），导致联动失败进入错误清理流程，运动无法执行。

## 日志证据

```
[12:27:23.529][INFO][UI][AxisVM]        Machine_A/X gantry triggerAbsMove requested
[12:27:23.537][INFO][APP][GantryMotion]  [Machine_A][X] START GantryAbsMove
[12:27:23.537][HAL][ModbusSystemDriver]  send: GantryPower enable=true -> reg[0] 使能轴X电机 (via X axis enable) -> OK
[12:27:23.596][DOM][Axis]               X  applyFeedback: state Disabled(1) -> Idle(2)
[12:27:23.596][DOM][Axis]               X1 applyFeedback: state Disabled(1) -> Idle(2)
[12:27:23.596][DOM][Axis]               X2 applyFeedback: state Disabled(1) -> Idle(2)
[12:27:23.598][APP][GantryMotion]        WaitingEnabled -> PostEnableDelay
[12:27:23.627][UI][Telemetry]           X: pos=+10.2 Standstill  X1: pos=+10.2 Standstill  X2: pos=+10.2 Standstill
[12:27:24.007][APP][GantryMotion]        PostEnableDelay -> Coupling (stabilized after 409.196100ms)
[12:27:24.018][HAL][ModbusSystemDriver]  send: GantryCoupling enableCoupling=true -> reg[4] 轴X联动使能 -> OK
[12:27:24.036][DOM][GantryCoupling]     applyFeedback: CouplingRequested -> Decoupled (isCoupled=false, errorCode=4, lastError=set)
[12:27:24.037][APP][GantryMotion]        WaitingCoupled -> Decoupling (cleanup after error: X1NotStationary)
[12:27:24.048]                          Decoupling -> WaitingDecoupled (send decoupling command)
[12:27:24.056][DOM][GantryCoupling]     applyFeedback: DecouplingRequested -> Decoupled (isCoupled=false, errorCode=0)
[12:27:24.057]                          WaitingDecoupled -> Disabling
[12:27:24.068]                          Disabling -> WaitingDisabled (send power disable)
[12:27:24.076][DOM][Axis]               X  applyFeedback: state Idle(2) -> Disabled(1)
[12:27:24.076][DOM][Axis]               X1 applyFeedback: state Idle(2) -> Disabled(1)
[12:27:24.076][DOM][Axis]               X2 applyFeedback: state Idle(2) -> Disabled(1)
[12:27:24.077][ERROR][APP][GantryMotion] WaitingDisabled -> Error (cleanup completed)
[12:27:24.077][ERROR][APP][GantryMotion] Flow ended with error
[12:27:24.077][WARN][UI][AxisVM]         error from GantryAbsMove: GANTRY_X1_NOT_STATIONARY
```

## 完整时序分析

```
时间轴 (tick):
──────────────────────────────────────────────────────────────────────────
23.529  UI 触发 triggerAbsMove → GantryAbsMove 启动
23.537  Step=EnsuringEnabled: 下发使能命令 → 写 reg[0] 使能轴X电机
23.596  PLC 反馈: X/X1/X2 状态均从 Disabled(1) → Idle(2)
23.598  Step=WaitingEnabled→PostEnableDelay (检测到 power.isEnabled()==true)
          │
          ├── 400ms 延迟等待伺服稳定 ──┐
          │                            │
23.627    Telemetry: 全部 Standstill   │ (延迟期间约 29ms 处)
23.757    tryGetAxis(X) REJECTED      │ (延迟期间约 159ms 处)
          │  (LogicalAxisUnavailable   │
          │   WhenDecoupled)           │
          │                            │
24.007  Step=PostEnableDelay→Coupling ◄┘ (延迟结束, 实际 409ms)
24.018  Step=Coupling: 下发联动命令 → 写 reg[4] 轴X联动使能=true
          │
          ├── PLC 内部校验 X1 静止条件 ──┐
          │                              │
24.036  PLC 反馈: errorCode=4           ◄┘ X1NotStationary 拒绝联动
        (isCoupled=false, CouplingRequested→Decoupled)
24.037  WaitingCoupled→Decoupling (进入错误清理)
24.048  Decoupling→WaitingDecoupled (下发解耦命令)
24.056  PLC 反馈解耦完成 (DecouplingRequested→Decoupled)
24.057  WaitingDecoupled→Disabling
24.068  Disabling→WaitingDisabled (下发掉电命令)
24.076  X/X1/X2: Idle→Disabled (掉电确认)
24.077  WaitingDisabled→Error (错误清理完成, 流程终止)
```

## 根因分析

### 1. 直接原因：PLC 判定 X1 轴未静止

龙门联动（Coupling）操作由 PLC 执行最终安全裁决。当上位机写 `reg[4] LINKAGE_ENABLE=true` 时，PLC 内部会校验以下条件：

| PLC 错误码 | 含义 |
|------------|------|
| 0 | 无错误 |
| 1 | PositionToleranceExceeded（X1/X2 位置偏差超限） |
| 2 | X1NotEnabled（X1 未使能） |
| 3 | X2NotEnabled（X2 未使能） |
| **4** | **X1NotStationary（X1 未静止）** |
| 5 | X2NotStationary（X2 未静止） |

本案例中 PLC 返回 **errorCode=4**，即 PLC 判定 X1 轴在联动命令到达时尚未进入稳定静止状态。

### 2. 上位机与 PLC 的"静止"定义不一致

- **上位机**（`AxisState::Idle` / Telemetry "Standstill"）：仅表示轴状态寄存器非 Disabled/Error，且不在 Jogging/MovingAbsolute/MovingRelative 状态。不测量实际速度。
- **PLC**（stationary 判定）：可能有更严格的判据，如要求轴速度在阈值以下并持续 N 个采样周期。

上位机在 `23.596` 就认为全部轴已 Idle，Telemetry 在 `23.627` 也显示"Standstill"。但 PLC 在 `24.018`（约 391ms 后）仍判定 X1 未静止。

### 3. GantryPowerController 反馈判据过于宽松

`GantryPowerController::applyFeedback()` 仅依据 **X 轴** 的状态寄存器判断使能是否完成：

```cpp
// infrastructure/plc/ModbusSystemDriver.h 第 776 行
const bool gantryEnabled = m_device->readInt16(regFbState(AxisId::X)) != 0;
const GantryFeedback fb{gantryEnabled, gantryCoupled, gantryErrCode};
ctx.gantryPowerController().applyFeedback(fb);  // → m_status = Enabled
```

而 `PostEnableDelay` 的 400ms 计时起点是 `GantryPowerController::isEnabled() == true`，这仅表示 **X 轴逻辑状态寄存器非零**，并不代表 X1/X2 物理伺服已完全稳定。

### 4. GantryCouplingController 未做前置静止校验

```cpp
// domain/gantry/GantryCouplingController.h 第 67 行注释
// 注意：不再检查 Axis Error 状态，X1/X2 是否使能/静止/超差
//       由 PLC 通过 Gantry_Error_Code 反馈，PLC 是最终安全裁决者
```

联动控制器的 `requestCouple(true)` 不做任何 X1/X2 静止合法性预检，直接下发联动命令到 PLC。这导致：
- 只要 GantryPowerController 认为"已使能"，就立即进入 Coupling 步骤
- 不等待 X1/X2 物理静止确认
- 依赖 PLC 的错误反馈来发现条件不满足

### 5. 400ms PostEnableDelay 可能不足

当前 `kPostEnableDelaySeconds = 0.4`（400ms）从 X 轴状态变为 Idle 时开始计时。对于某些伺服系统，电机使能后的位置锁定（servo-lock settling）可能需要更长时间：
- 伺服 PID 参数整定较慢的系统
- 电机/负载惯量较大的系统
- 使能瞬间有微小过冲的编码器系统

### 6. 联动失败后的错误清理逻辑问题

当联动失败（`WaitingCoupled → Decoupling`）后，`GantryMotionOrchestrator` 进入清理流程：解耦 → 掉电 → Error。最终 `WaitingDisabled → Error` 而非重试，导致：
- 用户需要手动重新触发运动
- 如果问题是瞬时的（如 X1 正在做最后的微调），重试即可成功，但当前设计不支持

## 影响范围

- **龙门绝对定位**（`GantryAbsMovePolicy`）：使能→联动阶段失败，无法执行运动
- **龙门相对定位**（`GantryRelMovePolicy`）：同样继承 `GantryMotionOrchestrator`，相同流程，同样受影响
- **龙门点动**（`GantryJogPolicy`）：同样继承 `GantryMotionOrchestrator`，同样受影响
- **非龙门单轴运动**（Y/Z/R 单轴 `JogOrchestrator`）：不受影响

## 修复建议

### 方案 A（推荐）：增加 PostEnableDelay + 添加重试机制

**修改文件**：`application/policy/GantryMotionOrchestrator.h`

**1. 增大 PostEnableDelay 延迟时间**

```cpp
// 修改前
static constexpr double kPostEnableDelaySeconds = 0.4;

// 修改后：增加到 800ms，给伺服更充裕的锁定时间
static constexpr double kPostEnableDelaySeconds = 0.8;
```

**2. 在 WaitingCoupled 阶段添加重试逻辑**

当前 `WaitingCoupled` 遇到错误直接进入 Decoupling 清理。改为：当错误为 `X1NotStationary` 或 `X2NotStationary` 时，退回到 PostEnableDelay 重试（最多 N 次）：

```cpp
case Step::WaitingCoupled:
    if (coupling.isCoupled()) {
        m_step = Step::IssuingCommand;
    } else if (coupling.hasError()) {
        auto err = coupling.getLastError();
        // X1/X2 未静止 → 可能是伺服尚在锁定中，退回延迟等待重试
        if ((err == GantryRejection::X1NotStationary 
             || err == GantryRejection::X2NotStationary)
            && m_couplingRetryCount < kMaxCouplingRetries) {
            m_couplingRetryCount++;
            LOG_WARN(LogLayer::APP, "GantryMotion",
                logPrefix() + " WaitingCoupled -> PostEnableDelay (retry "
                    + std::to_string(m_couplingRetryCount) + "/"
                    + std::to_string(kMaxCouplingRetries)
                    + " after: " + rejectionToString(err) + ")");
            // 先解耦当前失败的联动请求
            coupling.requestCouple(false);
            if (coupling.hasPendingCommand() && drv) {
                drv->send(coupling.popPendingCommand());
            }
            m_postEnableDoneTime = std::chrono::steady_clock::now();
            m_step = Step::PostEnableDelay;
        } else {
            m_lastError = err;
            m_cleanupAfterError = true;
            m_step = Step::Decoupling;
        }
    }
    break;
```

**新增成员变量**：
```cpp
int m_couplingRetryCount = 0;
static constexpr int kMaxCouplingRetries = 3;
```

**在 `startMotion()` 中重置**：
```cpp
m_couplingRetryCount = 0;
```

### 方案 B（更保守）：增加 PLC 静止状态确认

在 `Coupling` 步骤之前，增加一个 `WaitingStationary` 步骤，通过读取 X1/X2 的 PLC 反馈寄存器（如速度或专用静止标志位）确认物理静止后再下发联动命令。

**优点**：彻底消除上位机/PLC 状态不一致  
**缺点**：需要 PLC 侧提供明确的静止状态寄存器，改动面更大

### 方案 C（最简单，治标）：仅增大 PostEnableDelay

将 `kPostEnableDelaySeconds` 从 0.4 增加到 1.0~1.5 秒。

**优点**：一行改动  
**缺点**：
- 不解决根本的状态不一致问题
- 增加了所有龙门运动的启动延迟
- 对于特别慢的伺服系统可能仍不够

## 建议采用方案

**推荐方案 A**（增大延迟 + 重试），理由：
1. 改动集中在 `GantryMotionOrchestrator.h` 一个文件
2. 兼容现有 PLC 接口，不需要 PLC 侧修改
3. 重试机制能覆盖偶发性的伺服锁定延迟
4. 增大延迟提供更充裕的物理稳定时间
5. 最大重试次数限制防止无限循环

## 相关代码位置

| 文件 | 行号 | 说明 |
|------|------|------|
| `application/policy/GantryMotionOrchestrator.h` | 642 | `kPostEnableDelaySeconds = 0.4` — 延迟常量 |
| `application/policy/GantryMotionOrchestrator.h` | 197-207 | `PostEnableDelay` 步骤实现 |
| `application/policy/GantryMotionOrchestrator.h` | 244-264 | `WaitingCoupled` 步骤（错误处理入口） |
| `domain/gantry/GantryCouplingController.h` | 48-85 | `requestCouple()` — 无静止预检 |
| `domain/gantry/GantryCouplingController.h` | 158-168 | `translatePlcError()` — PLC 错误码映射 |
| `domain/gantry/GantryPowerController.h` | 84-86 | `applyFeedback()` — 仅依据 X 轴状态判断使能完成 |
| `infrastructure/plc/ModbusSystemDriver.h` | 775-784 | `pollFeedback` Phase 8 — GantryFeedback 构造 |

## 日期

2026-06-06