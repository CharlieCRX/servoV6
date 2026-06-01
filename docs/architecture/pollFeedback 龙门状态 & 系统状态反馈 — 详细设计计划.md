## pollFeedback 龙门状态 & 系统状态反馈 — 详细设计计划

---

### 一、整体数据流：pollFeedback 中从寄存器到领域控制器的完整链路

```
┌─────────────────────────────────────────────────────────────────┐
│                    pollFeedback(SystemContext& ctx)               │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  [已实现] Phase 1-5:  PLC 读取管线                              │
│    prepare() → FC01/FC03 → assemble() → updateSnapshot()        │
│                                                                  │
│  [已实现] Phase 6: Per-Axis Feedback Injection                   │
│    for each AxisId:                                              │
│      device.readInt16(regFbState)                                │
│      device.readInt16(regFbAlarmCode)                            │
│      device.readBool(regFbAbsMoving/RelMoving/Jogging)           │
│      device.readFloat(regFbAbsPos/RelPos)                        │
│      → deriveAxisState() → axis->applyPlcFeedback()              │
│                                                                  │
│  ── ✦ 新增 ✦ ─────────────────────────────────────────────── │
│                                                                  │
│  Phase 7: 急停状态注入                                           │
│    device.readBool(M130 ESTOP_ACTIVE)                             │
│    → ctx.emergencyStopController().applyFeedback(bool)           │
│    ├─ NotSynchronized → Running / EmergencyStopped (首次同步)    │
│    ├─ EmergencyStopping → EmergencyStopped                       │
│    ├─ ReleasingEmergencyStop → Running                           │
│    └─ Running → EmergencyStopped (物理急停按钮)                  │
│                                                                  │
│  Phase 8: 龙门状态注入                                           │
│    enable  ← device.readInt16(D100 STATE) ≠ 0                    │
│    coupled ← device.readBool(M125 LINKAGE_STATE)                  │
│    errCode ← device.readInt16(D180 GANTRY_ERROR_CODE)            │
│    → GantryFeedback {enable, coupled, errCode}                   │
│    → ctx.gantryCouplingController().applyFeedback(fb)            │
│    │   ├─ CouplingRequested + errCode≠0 → Decoupled (PLC拒绝)    │
│    │   ├─ CouplingRequested + coupled   → Coupled                │
│    │   ├─ DecouplingRequested + !coupled → Decoupled             │
│    │   └─ 稳态 → 直接反映物理真相                                │
│    → ctx.gantryPowerController().applyFeedback(fb)               │
│        └─ enable ? Enabled : Disabled                            │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

### 二、三个 Controller 的状态机交互

```
         pollFeedback() 注入
              │
     ┌────────┼──────────┐
     ▼        ▼          ▼
 Emergency  Gantry    Gantry
 Stop       Coupling  Power
            │
            │ isNotSynchronized() ──影响──▶ SystemContext::tryReadAxis()
            │                              对龙门轴的拦截
            │
            │ isCoupled() / isDecoupled()
            └──影响──▶ SystemContext::tryGetAxisInternal()
                        X1/X2 vs X 的可见性裁决
```

**关键时序**：pollFeedback 每帧按 **急停 → 龙门耦合 → 龙门电机** 顺序注入，这确保了：
- 当 `GantryCouplingController` 从 `NotSynchronized` 迁出后，下一帧的 `tryReadAxis()` 调用即可正确路由龙门轴访问
- 急停状态是最先更新的，保证安全域最早生效

---

### 三、设计关键点深入分析

#### 3.1 龙门使能信号 `enable` 为什么不从 Axis 实体获取？

```
解耦态下的 SystemContext::tryReadAxis() 行为：
  - AxisId::X    → 拦截 (LogicalAxisUnavailableWhenDecoupled)
  - AxisId::X1   → 通过 (物理轴)
  - AxisId::X2   → 通过 (物理轴)
```

而 `GantryPowerController` 需要的是"**X1/X2 电机实际的使能状态**"（即 D100 STATE ≠ 0），无论龙门是联动还是解耦。这个信息**无法**从 X 逻辑轴获取（解耦时被拦截），必须直接从 `PlcDevice` 快照中读取 `D100` 原值。

因此设计为：
```cpp
const bool gantryEnabled = m_device->readInt16(regFbState(AxisId::X)) != 0;
```

注意 `regFbState(AxisId::X)` 返回的是 `reg::x_axis::feedback::STATE`（D100），X1/X2 也映射到同一个寄存器。用 `AxisId::X` 只是选择器表驱动查询，实际读取的是 D100。

#### 3.2 急停反馈与安全锁定的时序保证

```
帧 N:
  pollFeedback:
    → EmergencyStopController::applyFeedback(true)
    → m_state 变为 EmergencyStopped
    → isSystemLocked() 从 false 变为 true

帧 N+1:
  SystemManager::tick():
    → 尝试下发运动命令
    → ctx.tryGetAxis() → Layer 0 拦截 (SystemSafetyLocked)
    → 命令被拒绝
```

急停锁定在当前帧注入后立即生效（因为 `isSystemLocked()` 是实时查询状态机的），下一帧所有运动命令都将被拦截。

#### 3.3 龙门同步状态与轴可见性的时序保证

```
帧 N (启动后首次 pollFeedback):
  GantryCouplingController::applyFeedback({enable=true, coupled=true, errCode=0})
  → m_state 从 NotSynchronized → Coupled

帧 N+1:
  ctx.tryReadAxis(AxisId::X1)  → 被拦截 (PhysicalAxisLockedByGantry)
  ctx.tryReadAxis(AxisId::X)   → 通过 (逻辑轴)
  → per-axis 反馈循环只为 X 注入数据
```

此设计保证联动态下 X1/X2 的 Axis 实体不接收反馈（它们被龙门语义锁定），只有 X 逻辑轴接收。

---

### 四、边缘情况分析

| 场景                                | 预期行为                                                     |
| ----------------------------------- | ------------------------------------------------------------ |
| **快照不可信**                      | `isStateTrusted()==false` → 提前 return，急停和龙门都不注入  |
| **m_device 为空**                   | Phase 5 之后 `if (m_device)` 保护已存在                      |
| **M125/M130/D180 寄存器读取**       | 都已在 PlcPoller 的全量快照中，不需要额外 FC 请求            |
| **GantryCouplingController 未同步** | applyFeedback() 会将其从 NotSynchronized 迁出                |
| **急停与龙门同时变化**              | 顺序注入，急停优先（安全域最高优先级）                       |
| **X 轴 D100=0（掉电）时龙门反馈**   | GantryFeedback.enable=false → GantryPowerController 正确迁移到 Disabled |

---

### 五、实施计划（分步骤）

| 步骤       | 内容                                                | 涉及文件                                      | 风险                                                         |
| ---------- | --------------------------------------------------- | --------------------------------------------- | ------------------------------------------------------------ |
| **Step 1** | 在 `pollFeedback()` 中添加急停反馈注入（3 行代码）  | `ModbusSystemDriver.h`                        | 低 — `EmergencyStopController::applyFeedback()` 已是纯函数   |
| **Step 2** | 在 `pollFeedback()` 中添加龙门反馈注入（~8 行代码） | `ModbusSystemDriver.h`                        | 低 — `GantryCouplingController` / `GantryPowerController::applyFeedback()` 已存在 |
| **Step 3** | 添加单元测试：急停反馈注入验证                      | `test_modbus_system_driver_poll_feedback.cpp` | 中 — 需要构造可信快照                                        |
| **Step 4** | 添加单元测试：龙门耦合反馈注入验证                  | 同上                                          | 中 — 需验证状态机迁出 NotSynchronized                        |
| **Step 5** | 添加单元测试：龙门电机使能反馈验证                  | 同上                                          | 低                                                           |
| **Step 6** | 集成测试：急停+龙门组合场景                         | `test_system_integration.cpp`                 | 高 — 需完整环境                                              |

---

### 六、修改前后 pollFeedback() 结构对比

**修改前（当前）：**
```
pollFeedback:
  1. servicePendingEdgeTriggers()
  2. 防御检查 (m_modbusClient, m_poller)
  3. prepare → FC01 → FC03 → assemble → updateSnapshot
  4. 可信度门禁
  5. for each axis: decode → deriveAxisState → applyPlcFeedback
  END
```

**修改后：**
```
pollFeedback:
  1. servicePendingEdgeTriggers()
  2. 防御检查 (m_modbusClient, m_poller)
  3. prepare → FC01 → FC03 → assemble → updateSnapshot
  4. 可信度门禁
  5. for each axis: decode → deriveAxisState → applyPlcFeedback
  6. ✦ EmergencyStopController::applyFeedback(bool)
  7. ✦ 构造 GantryFeedback → 
       GantryCouplingController::applyFeedback()
       GantryPowerController::applyFeedback()
  END
```

仅新增 **~15 行核心代码**，不触及现有任何逻辑。

---

是否需要我进入 ACT MODE 开始实施 Step 1 和 Step 2？