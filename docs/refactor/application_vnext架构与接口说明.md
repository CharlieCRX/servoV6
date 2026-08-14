# application_vnext 作用与接口说明

> 状态：P5 app 已完成（SystemManager/UseCase 迁移到 domain_vnext）；阶段 3 单轴受控写入、
> 运动策略层（Abs/Rel/Jog + AxisMotionApi）与联机探针已落地。
> 依据：《docs/refactor/servoV6剩余迁移工作实施方案.md》、《docs/refactor/domain_vnext/Domain层重构设计——domain_vnext.md》。

## 1. 模块定位

`application_vnext` 是**基于 `domain_vnext` 的新应用层 STATIC 库**，与旧 `application/` 并行迁移
（P6 清理前旧层保留，互不侵入）。它承接三层职责：

1. **组合根门面**：`SystemManagerVnext` 封装 `boot / poll / 单轴用例 / 急停 / 龙门`，是 UI、UDP、
   摇杆等生产控制入口的统一门面。
2. **驱动适配**：`PlcRuntimeDriverAdapter` 把领域侧 `gateway::IPlcDriver` 委托给
   `plc_vnext::IPlcRuntimeGateway`（真实客户端 / Fake 可注入）。
3. **调试与策略**：
   - `commissioning/`：阶段 3 单轴物理调试（写入闸门 + 点动心跳会话），供联机探针使用。
   - `policy/`：非阻塞 tick 运动策略（AbsMovePolicy / RelMovePolicy / JogPolicy）+ `AxisMotionApi`，
     统一"使能→下发/触发→运行→停止/等空闲→掉电"时序。

### 依赖方向与约束
- 依赖方向：`application_vnext -> domain_vnext -> plc_vnext::contracts`。
- **不 include** 旧 `domain/*`、`infrastructure/ISystemDriver`、Qt、Modbus。
- 写通道经 `domain_vnext::gateway::IPlcDriver`（面向抽象）；测试注入 `plc_vnext::fake::FakePlcRuntimeGateway`。

## 2. 目录与文件清单

| 文件 | 内容 |
| --- | --- |
| `SystemManagerVnext.h` | 组合根门面：boot/poll/单轴用例/急停/龙门；完整链路 find→submit→drain→map→路由→writeAxis |
| `PlcRuntimeDriverAdapter.h` | 实现 `gateway::IPlcDriver`，委托 `plc_vnext::IPlcRuntimeGateway` |
| `AppVnextError.h` | 应用层错误聚合（`std::variant`，monostate=成功） |
| `ShadowRunAssessor.h` | 阶段 2 只读影子运行锁定判定（纯函数） |
| `commissioning/CommissioningTypes.h` | 调试操作类型 + 写入闸门拒绝原因 |
| `commissioning/PhysicalAxisCommissioningService.{h,cpp}` | 阶段 3 单轴物理调试服务（闸门 + 参数往返 + 使能 + 点动 + 定位 + 停止 + 急停） |
| `commissioning/JogSession.{h,cpp}` | 点动心跳会话（周期写心跳，断连自动退出） |
| `policy/AxisMotionCommon.h` | motionState 语义常量 + 时序参数 + 结果结构 |
| `policy/AbsMovePolicy.h` | 绝对定位触发策略（仅触发，不写目标） |
| `policy/RelMovePolicy.h` | 相对定位触发策略（仅触发，不写目标） |
| `policy/JogPolicy.h` | 点动策略（显式停止 + tick 驱动心跳） |
| `policy/AxisMotionApi.h` | 运动"可调用接口"（slot 维度 → AxisFunction） |
| `tests/` | `test_system_manager_vnext.cpp` / `test_shadow_run_assessor.cpp` / `test_axis_motion_policy.cpp` |

## 3. 核心接口

### 3.1 SystemManagerVnext（组合根门面）

构造：`SystemManagerVnext(domain_vnext::gateway::IPlcDriver& driver)`。

**生命周期**
| 接口 | 说明 |
| --- | --- |
| `bool boot()` | 读拓扑→动态建轴/分组→首次 poll 注入反馈；返回是否可控制 |
| `bool poll()` | 读运行快照→FeedbackDispatcher 注入轴反馈/龙门状态/急停 |
| `void applyParameters(...)` | 注入参数区快照（RW） |

**单轴原子用例（默认 A 组 g=0，按 `AxisFunction` 路由）**
| 接口 | 说明 |
| --- | --- |
| `enableAxis(fn, on)` / `enableMotor(fn, on)` | 轴控 / 电机使能（保持电平） |
| `jog(fn, forward, on)` / `stopJog(fn, forward)` | 点动方向 ON/OFF |
| `jogHeartbeat(fn, on)` | 点动心跳（保持电平，周期写 ON） |
| `setManualSpeed / setPositioningSpeed / setAbsTarget / setRelTarget` | 参数写（D 区） |
| `triggerAbsMove(fn)` / `triggerRelMove(fn)` | 定位触发（自复位线圈，只写 ON） |
| `stop(fn)` | 绝对/相对定位终止（自复位，只写 ON） |
| `clearRelZero / setRelZero / clearAbsPosition` | 清零/设零 |

**安全与龙门**
| 接口 | 说明 |
| --- | --- |
| `requestEmergencyStop() / requestReleaseEmergencyStop()` | M224/M225 |
| `applyEmergencyStopFeedback(bool)` | 注入急停反馈 |
| `isSystemLocked()` / `hasPendingEStop()` / `popPendingEStop()` | 急停状态 |
| `gantryCouple / gantryDecouple / gantryReset(g)` | 龙门建立/解除/复位（Command→RequestSeq→AckSeq） |
| `applyGantryConfig(g, cfg)` | 注入龙门参数 |

**单轴用例链路（§5.2）**
```text
SystemManagerVnext.<用例>
  -> AxisSystem.find({group, fn})
  -> AxisStateMachine.submit(意图)      // 系统锁定/龙门同步/轴忙校验
  -> CommandOutbox.drain()              // 参数去重 + 运动保序 + 脉冲
  -> CommandMapper.mapAxis              // kind/value/level 透传
  -> effectiveEnableSlot                // §4.6a 龙门成员使能改写为逻辑轴槽位
  -> gateway::IPlcDriver.writeAxis(slot, cmd)
```

### 3.2 AppVnextError（错误聚合）
`using AppVnextResult = std::variant<std::monostate, AppVnextError>`；`appResultOk(r)` / `appErrorOf<E>(r)`。
包含：`AppNotBooted / AxisNotFound / AxisNotReady / SubmitRejectedState / CommandUnsupported /
CommFailed / SafetyRejected / GantryRequestRejected / GantryCommFailed`。

### 3.3 commissioning/（阶段 3 单轴物理调试，阻塞式、供探针）
- `PhysicalAxisCommissioningService(IPlcRuntimeGateway&)`：
  - 写入闸门 `evaluateGate(slot, op)`：连接/拓扑/Revision/运行可信/急停/报警/slot 白名单(2,3)/无其它点动会话。
  - `verifyParameter`（读原值→写→读回→恢复）、`enableAxis/enableMotor`、`jog`（含心跳）、
    `moveRelative/moveAbsolute`、`stopMove`、`triggerEmergencyStop/requestReleaseEmergencyStop`。
- `JogSession`：点动心跳会话（独立线程，断连回调退出；commissioning 专用）。
- 说明：本服务**绕过** SystemManagerVnext（直接对物理 slot 写），用于未绑定拓扑功能的调试轴联机验证。

### 3.4 policy/（运动策略层，非阻塞 tick，本次新增重点）

**共同形态**：构造持 `SystemManagerVnext&` + `PlcAxisSlot`，`start()` 启动，调用方每反馈周期 `tick()`；
`isDone() / hasError() / diag()` 查询。写走 SystemManagerVnext 原子用例，反馈读 `Axis::feedback().motionState`。

**AbsMovePolicy / RelMovePolicy（触发-only）**
```text
Initial -> EnsuringEnabled -> PostEnableDelay(0.4s) -> TriggeringMove(仅触发)
        -> WaitingMotionStart -> WaitingMotionFinish -> PostStopDelay(0.5s)
        -> Disabling(掉电=电机OFF) -> Done / Error
```
- **不写目标**：目标经独立的 `setAbsTarget / setRelTarget` 写入 PLC。
- **完成判定**：`state==2`（PLC 空闲）无条件成功；否则位置到位 ±0.1 持续 3s 兜底成功。
- 可调：`setVerifyTarget(t)`（到位校验目标，仅判定不写）、`setVerifyTolerance(t)`、`setSendStopAfterIdle(bool)`。

**JogPolicy（点动，显式停止）**
```text
Idle -> EnsuringEnabled -> PostEnableDelay -> IssuingJog(心跳ON+方向ON) -> Jogging(周期刷心跳)
     -> IssuingStop(方向OFF+心跳OFF) -> WaitingForIdle -> PostStopDelay -> EnsuringDisabled -> Done / Error
```
- 心跳为 **tick 驱动**（在主 poll 循环内按周期写 ON，无后台线程，避免与 poll 竞争 domain）。
- `durationMs>0` 到时自动停；`requestStop()` 外部停止；心跳写失败自动停。

**AxisMotionApi（slot 维度可调用接口）**
```cpp
AxisMotionApi api(mgr);                      // mgr 已 boot
api.setAbsTarget(slot2, 100.f);              // 独立写目标（不触发）
auto o  = api.runAbs(slot2, 100.f);          // 阻塞便利：写目标+触发策略跑完
auto o  = api.runRel(slot2, 50.f);
auto o  = api.runJog(slot2, /*fwd*/true, 3000);
auto p  = api.beginAbs(slot2);               // 非阻塞：返回策略，主循环 poll()+tick()
api.targetFor(slot2, target);                // slot -> (group, AxisFunction)
```
- 依赖真实拓扑把 slot 绑定到某 AxisFunction（如 `Role[2]=Y→slot2`），否则返回 `AxisNotFound`。

## 4. motionState 语义（D128，真实 PLC_re）

| 值 | 含义 | 说明 |
| --- | --- | --- |
| 0 | 轴控未使能 | — |
| 1 | 轴控 ON、电机 OFF | 掉电态 |
| 2 | 电机 ON、空闲 | **使能完成 / 运动完毕**（完成判定） |
| 3 / 4 | 点动正向 / 反向 | Jog 运行中 |
| 5 | 轴MoveDo | **绝对定位执行中** |
| 6 | 轴MoveXDDo | **相对定位执行中**（MoveXDPosition=目标相对距离） |

使能阶段：`ms==0` → 下发轴控+电机；`ms==1` → 只补电机；`ms==2` → 就绪。

## 5. 联机验证工具

`tools/plc_vnext_motion_probe`（见 `tools/`），经 SystemManagerVnext + AxisMotionApi 联机驱动：
```bash
plc_vnext_motion_probe.exe --host IP --slot 2 --action read
plc_vnext_motion_probe.exe --host IP --action topo          # 查看 slot 绑定
plc_vnext_motion_probe.exe --host IP --slot 2 --action jog-forward --duration-ms 2000 --confirm-write --confirm-motion
plc_vnext_motion_probe.exe --host IP --slot 2 --action move-absolute --value 100 --confirm-write --confirm-motion
plc_vnext_motion_probe.exe --host IP --slot 2 --action move-relative --value -30 --confirm-write --confirm-motion
```
- 逐帧打印 `step / ms / pos`；`--move-timeout-ms 0`=不限（由策略 Done/Error 结束）。
- 另有一阶段 3 直写探针 `plc_vnext_physical_axis_probe`（commissioning 阻塞式、不要求拓扑绑定）。

## 6. 构建与测试

```bash
cmake --build build --target application_vnext_tests
ctest --test-dir build -R "^application_vnext\\."
```
- 当前 `application_vnext_tests` 含系统管理器、影子运行、运动策略三组测试（运动策略 8/8 通过）。

## 7. 迁移状态提示
- 生产控制入口尚未切换到 vnext（`main.cpp` 仍用旧链路），切换是方案 §4.2/阶段 5 的后续工作。
- 运动策略层的生产接入：把 `AxisMotionApi::beginXxx`（非阻塞）挂到主 poll 循环即可替换旧 `application/policy/*Orchestrator`。
