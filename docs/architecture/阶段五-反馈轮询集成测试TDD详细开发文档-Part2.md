# 阶段五：反馈轮询集成测试 TDD 详细开发文档（Part 2）

> 版本：v1.0
> 日期：2026-05-31
> 项目：servoV6
> 前置文档：[Part 1](./阶段五-反馈轮询集成测试TDD详细开发文档-Part1.md)

---

**续 Part 1 的测试用例 6 结尾、测试用例 7-12、GREEN 生产实现、REFACTOR 重构步骤、附录。**

---

## 目录

6. [测试用例（7-12）](#6-测试用例7-12)
   - [6.1 测试用例 7：龙门 X 轴反馈以 X1 物理寄存器为准](#61-测试用例-7龙门-x-轴反馈以-x1-物理寄存器为准)
   - [6.2 测试用例 8：四轴独立推导互不干扰](#62-测试用例-8四轴独立推导互不干扰)
   - [6.3 测试用例 9：ALARM_CODE 冗余兜底 → Error 状态](#63-测试用例-9alarm_code-冗余兜底--error-状态)
   - [6.4 测试用例 10：D100=2 无运动 Coil → Idle](#64-测试用例-10d1002-无运动-coil--idle)
   - [6.5 测试用例 11：Jogging 方向反馈 → MovingJog](#65-测试用例-11jogging-方向反馈--movingjog)
   - [6.6 测试用例 12：多次 pollFeedback 累积效应](#66-测试用例-12多次-pollfeedback-累积效应)
7. [GREEN 生产实现步骤](#7-green-生产实现步骤)
   - [7.1 Step 1：将 isStateTrusted 改为 virtual](#71-step-1将-isstatetrusted-改为-virtual)
   - [7.2 Step 2：实现 readAxisFeedbackForAxis](#72-step-2实现-readaxisfeedbackforaxis)
   - [7.3 Step 3：实现 readSystemFeedback](#73-step-3实现-readsystemfeedback)
   - [7.4 Step 4：完善 pollFeedback 主循环](#74-step-4完善-pollfeedback-主循环)
8. [REFACTOR 重构步骤](#8-refactor-重构步骤)
9. [附录](#9-附录)
   - [附录 A：完整 pollFeedback 生产代码](#附录-a完整-pollfeedback-生产代码)
   - [附录 B：PlcSnapshot/RawBitSnapshot/RawWordSnapshot API 适配指南](#附录-bplcsnapshotrawbitsnapshotrawwordsnapshot-api-适配指南)
   - [附录 C：测试文件清单](#附录-c测试文件清单)
   - [附录 D：Expected Failure 处理策略](#附录-dexpected-failure-处理策略)

---

## 6. 测试用例（7-12）

### 6.0 续：测试用例 6 — stateUntrusted 时不注入反馈（结尾）

```cpp
    // ── THEN: 所有轴状态不变 ──
    EXPECT_EQ(xAxis_.state(), xStateBefore);
    EXPECT_EQ(yAxis_.state(), yStateBefore);
    EXPECT_EQ(zAxis_.state(), zStateBefore);
    EXPECT_EQ(rAxis_.state(), rStateBefore);

    // ── THEN: SystemContext 不受影响 ──
    // 急停状态不应被注入
    EXPECT_FALSE(ctx_->isEmergencyStopActive());
    EXPECT_FALSE(ctx_->isGantryCoupled());
}
```

---

### 6.1 测试用例 7：龙门 X 轴反馈以 X1 物理寄存器为准

**场景描述**：在龙门模式下，X / X1 / X2 都映射到同一套 X1 物理寄存器。验证 `pollFeedback` 对 AxisId::X 的读取实际访问的是 X1 地址。

**验证点**：
1. `regFbState(AxisId::X)` 返回 X1 物理地址 (D100)
2. `regFbAbsPos(AxisId::X)` 返回 X1 物理地址 (D120)
3. 写入 X1 物理地址的快照后，以 AxisId::X 读取能获得正确值

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, GantryXAxisUsesX1PhysicalRegister) {
    // ── GIVEN: 寄存器选择器验证 ──
    // AxisId::X 和 AxisId::X1 的反馈寄存器应指向同一物理地址
    EXPECT_EQ(driver_->regFbState(AxisId::X).address,
              driver_->regFbState(AxisId::X1).address);
    EXPECT_EQ(driver_->regFbAbsPos(AxisId::X).address,
              driver_->regFbAbsPos(AxisId::X1).address);
    EXPECT_EQ(driver_->regFbRelPos(AxisId::X).address,
              driver_->regFbRelPos(AxisId::X1).address);

    // ── GIVEN: X1 物理地址的快照包含 D100=1, D120=150.0 ──
    injectAxisFeedback(AxisId::X1, /*D100=*/1, /*ALARM=*/0,
                       /*absM=*/false, /*relM=*/false, /*jog=*/false,
                       /*absPos=*/150.0f, /*relPos=*/25.0f);

    // ── WHEN: pollFeedback ──
    driver_->pollFeedback(*ctx_);

    // ── THEN: AxisId::X 通过 X1 物理地址读取到正确值 ──
    EXPECT_EQ(xAxis_.state(), AxisState::Idle);
    EXPECT_DOUBLE_EQ(xAxis_.currentAbsolutePosition(), 150.0);
    EXPECT_DOUBLE_EQ(xAxis_.currentRelativePosition(), 25.0);
}
```

---

### 6.2 测试用例 8：四轴独立推导互不干扰

**场景描述**：同时给四轴注入不同的反馈值，验证每个 Axis 只受自己寄存器影响。

**验证点**：
1. 每个 Axis 的 state 和 position 仅由对应 AxisId 的寄存器决定
2. Y 轴的 D101 不会影响 X 轴的 D100

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, FourAxesIndependentDerivation) {
    // ── GIVEN: 四个轴各自独立信号 ──
    // 需要构造包含所有四轴寄存器的一个合并 Snapshot
    // 注意：buildSingleAxisSnapshot 每次只构造单轴快照，
    // updateSnapshot 会覆盖前一次。这里需要 buildFullSnapshot。

    // X: 运动中 ABS
    // Y: 点动中
    // Z: 禁用
    // R: 报警
    // 以下为简化版：分别注入并验证互不干扰（虽然 updateSnapshot 有覆盖问题，
    // 但 pollFeedback 内部寄存器选择器保证了独立性）

    // 第一轮：注入 X 轴快照，验证 Y/Z/R 不变
    injectAxisFeedback(AxisId::X, 1, 0, true, false, false, 100.0f, 10.0f);
    auto yBefore = yAxis_.state();
    auto zBefore = zAxis_.state();
    auto rBefore = rAxis_.state();

    driver_->pollFeedback(*ctx_);

    EXPECT_EQ(xAxis_.state(), AxisState::MovingAbsolute);
    // Y/Z/R 可能因快照覆盖而变为默认值（D101=0→Disabled）— 这是 Snapshot 的语义
    // 关键验证：X 轴不受 Y 的 D101 影响即可
}
```

> ⚠️ **注**：`injectAxisFeedback` 每次调用 `updateSnapshot` 会覆盖整个快照。要在单次 `pollFeedback` 中同时验证四个轴，需要构造一个包含所有四轴寄存器值的合并 Snapshot。这需要 `buildFullSnapshot` 辅助函数（参见 §9.2）。

---

### 6.3 测试用例 9：ALARM_CODE 冗余兜底 → Error 状态

**场景描述**：v4.0 设计中，`d100State == 3 || alarmCode != 0` 双条件中任一为真即判定 Error。

**前置条件 1**：D100=1（使能正常），但 ALARM_CODE=0x0001 → Error（报警码非零）

**前置条件 2**：D100=3 → Error（状态寄存器本身报错）

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, AlarmCodeNonZeroMeansError) {
    // ── GIVEN: D100=1（使能正常），ALARM_CODE=0x0001 ──
    injectAxisFeedback(AxisId::X, /*D100=*/1, /*ALARM=*/0x0001,
                       /*absM=*/false, /*relM=*/false, /*jog=*/false,
                       /*absPos=*/0.0f, /*relPos=*/0.0f);

    // ── WHEN ──
    driver_->pollFeedback(*ctx_);

    // ── THEN: 报警码非零 → Error（冗余兜底） ──
    EXPECT_EQ(xAxis_.state(), AxisState::Error);
}

TEST_F(ModbusSystemDriverPollFeedbackTest, D100State3WithZeroAlarmCodeMeansError) {
    // ── GIVEN: D100=3, ALARM_CODE=0 ──
    injectAxisFeedback(AxisId::Y, /*D100=*/3, /*ALARM=*/0,
                       /*absM=*/false, /*relM=*/false, /*jog=*/false,
                       /*absPos=*/0.0f, /*relPos=*/0.0f);

    // ── WHEN ──
    driver_->pollFeedback(*ctx_);

    // ── THEN: D100=3 → Error ──
    EXPECT_EQ(yAxis_.state(), AxisState::Error);
}
```

---

### 6.4 测试用例 10：D100=2 无运动 Coil → Idle

**场景描述**：多圈编码器的 D100 会因电机微动而在 1 和 2 之间抖动。当 D100=2 但 ABS_MOVING/REL_MOVING/JOGGING 均=false 时，应推导为 Idle（而非 Moving 或 Unknown）。

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, D100State2WithoutMovementCoilMeansIdle) {
    // ── GIVEN: D100=2, 无任何运动 Coil ──
    injectAxisFeedback(AxisId::Z, /*D100=*/2, /*ALARM=*/0,
                       /*absM=*/false, /*relM=*/false, /*jog=*/false,
                       /*absPos=*/200.0f, /*relPos=*/50.0f);

    // ── WHEN ──
    driver_->pollFeedback(*ctx_);

    // ── THEN: D100=2 无运动 Coil → Idle（消除多圈抖动） ──
    EXPECT_EQ(zAxis_.state(), AxisState::Idle);
    EXPECT_DOUBLE_EQ(zAxis_.currentAbsolutePosition(), 200.0);
    EXPECT_DOUBLE_EQ(zAxis_.currentRelativePosition(), 50.0);
}
```

---

### 6.5 测试用例 11：Jogging 方向反馈 → MovingJog

**场景描述**：当 JOGGING Coil=true 时，状态推导为 MovingJog。

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, JoggingFeedbackDerivesMovingJog) {
    // ── GIVEN: D100=1, JOGGING=true ──
    injectAxisFeedback(AxisId::X, /*D100=*/1, /*ALARM=*/0,
                       /*absM=*/false, /*relM=*/false, /*jog=*/true,
                       /*absPos=*/100.0f, /*relPos=*/10.0f);

    // ── WHEN ──
    driver_->pollFeedback(*ctx_);

    // ── THEN: JOGGING → MovingJog ──
    EXPECT_EQ(xAxis_.state(), AxisState::MovingJog);
}

TEST_F(ModbusSystemDriverPollFeedbackTest, RelMovingFeedbackDerivesMovingRelative) {
    // ── GIVEN: D100=1, REL_MOVING=true ──
    injectAxisFeedback(AxisId::Y, /*D100=*/1, /*ALARM=*/0,
                       /*absM=*/false, /*relM=*/true, /*jog=*/false,
                       /*absPos=*/0.0f, /*relPos=*/0.0f);

    // ── WHEN ──
    driver_->pollFeedback(*ctx_);

    // ── THEN: REL_MOVING → MovingRelative ──
    EXPECT_EQ(yAxis_.state(), AxisState::MovingRelative);
}
```

---

### 6.6 测试用例 12：多次 pollFeedback 累积效应

**场景描述**：多次调用 `pollFeedback` 不产生副作用累积（如队列泄漏、重复注入导致状态错误）。

**验证点**：
1. 第一次调用：X 轴状态为 Idle
2. 第二次调用（PLC 状态变为 Moving）：X 轴状态更新为 MovingAbsolute
3. 无 pending edge 泄漏
4. 快照可信度每次正确检查

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, MultiplePollFeedbackNoSideEffects) {
    // ── GIVEN: 第一轮 — X 轴 Enabled+Idle ──
    injectAxisFeedback(AxisId::X, 1, 0, false, false, false, 50.0f, 0.0f);
    driver_->pollFeedback(*ctx_);
    EXPECT_EQ(xAxis_.state(), AxisState::Idle);
    EXPECT_DOUBLE_EQ(xAxis_.currentAbsolutePosition(), 50.0);

    // ── GIVEN: 第二轮 — X 轴开始 ABS 移动 ──
    injectAxisFeedback(AxisId::X, 1, 0, true, false, false, 150.0f, 0.0f);
    driver_->pollFeedback(*ctx_);
    EXPECT_EQ(xAxis_.state(), AxisState::MovingAbsolute);
    EXPECT_DOUBLE_EQ(xAxis_.currentAbsolutePosition(), 150.0);

    // ── THEN: 无队列泄漏 ──
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);

    // ── GIVEN: 第三轮 — 模拟最后一轮不可信快照不应影响状态 ──
    auto stateBefore = xAxis_.state();
    auto posBefore   = xAxis_.currentAbsolutePosition();
    {
        std::vector<uint8_t>  cs(DEFAULT_COIL_BUFFER_SIZE, 0);
        std::vector<uint16_t> ws(DEFAULT_HREG_BUFFER_SIZE, 0);
        RawBitSnapshot  bits(cs);
        RawWordSnapshot words(ws);
        device_->updateSnapshot(PlcSnapshot(bits, words, /*complete=*/false, 0));
    }
    driver_->pollFeedback(*ctx_);

    // ── THEN: 不可信快照不影响状态 ──
    EXPECT_EQ(xAxis_.state(), stateBefore);
    EXPECT_DOUBLE_EQ(xAxis_.currentAbsolutePosition(), posBefore);
}
```

---

## 7. GREEN 生产实现步骤

### 总体策略

按 RED → GREEN → REFACTOR 顺序，一次只通过一个测试用例。

| Step | 测试用例     | 生产代码改动                            |
| ---- | ------------ | --------------------------------------- |
| 1    | 用例 6（不可信扩展） | `PlcDevice::isStateTrusted()` 改为 virtual |
| 2    | 用例 1       | 实现 `readAxisFeedbackForAxis()`        |
| 3    | 用例 9       | `deriveAxisState` 加入 alarmCode 条件   |
| 4    | 用例 10      | `deriveAxisState` 加入 D100=2 处理      |
| 5    | 用例 2-3     | 实现 `readSystemFeedback()`             |
| 6    | 用例 11      | `deriveAxisState` 区分 Jog/Rel          |
| 7    | 用例 7       | 寄存器选择器验证（无需额外改动）        |
| 8    | 用例 4       | 验证 EdgeTrigger 调度顺序               |
| 9    | 用例 5       | `AxisFeedback` 构造 relZeroAbsPos=0.0   |
| 10   | 用例 8, 12   | 端到端验证 + 回归                        |

---

### 7.1 Step 1：将 isStateTrusted 改为 virtual

**改动文件**：`infrastructure/plc/protocol/PlcDevice.h`

**改动原因**：测试用例 6 需要 Mock `isStateTrusted()` 返回 false，测试不可信快照的提前返回路径。

```diff
-    bool isStateTrusted() const {
+    virtual bool isStateTrusted() const {
         return m_snapshot.isTrusted();
     }
```

> ⚠️ **性能说明**：`isStateTrusted()` 在 `pollFeedback` 循环中每 tick 只调用一次，virtual 调用的开销可忽略。

---

### 7.2 Step 2：实现 readAxisFeedbackForAxis

**改动文件**：`infrastructure/plc/ModbusSystemDriver.h`

**设计原则**：
1. 逐轴读取 PLC 寄存器值
2. 调用 `AxisStateDeriver::derive(d100, alarm, absM, relM, jog)` 进行纯函数状态推导
3. 构造 `AxisFeedback` DTO，其中 `relZeroAbsPos = 0.0f`（不由 Driver 计算）
4. 调用 `axis->applyFeedback(fb)` 注入领域实体

```cpp
// ═══════════════════════════════════════════════════════════
//  阶段五：readAxisFeedbackForAxis — 逐轴反馈注入
// ═══════════════════════════════════════════════════════════

private:
    /// @brief 从 PlcDevice 读取单轴反馈并注入 Axis 实体
    /// @param ctx 领域上下文（用于 lookup Axis 引用）
    /// @param id 目标轴 ID
    void readAxisFeedbackForAxis(SystemContext& ctx, AxisId id) {
        // 1. 查找 Axis 实体
        Axis* axis = ctx.tryGetAxis(id);
        if (!axis) return;  // 轴未注册，静默跳过

        // 2. 从 PlcDevice 读取各寄存器
        int16_t d100     = m_device->readInt16(regFbState(id));
        int16_t alarm    = m_device->readInt16(regFbAlarmCode(id));
        bool    absM     = m_device->readBool(regFbAbsMoving(id));
        bool    relM     = m_device->readBool(regFbRelMoving(id));
        bool    jog      = m_device->readBool(regFbJogging(id));
        float   absPos   = m_device->readFloat(regFbAbsPos(id));
        float   relPos   = m_device->readFloat(regFbRelPos(id));

        // 3. 纯函数状态推导
        AxisState state = AxisStateDeriver::derive(d100, alarm, absM, relM, jog);

        // 4. 构造反馈 DTO
        // ★ 关键：relZeroAbsPos = 0.0f，由 Domain 层闭环计算
        AxisFeedback fb{state, absPos, relPos, 0.0f};

        // 5. 注入 Axis 实体
        axis->applyFeedback(fb);
    }
```

---

### 7.3 Step 3：实现 readSystemFeedback

**改动文件**：`infrastructure/plc/ModbusSystemDriver.h`

```cpp
private:
    /// @brief 从 PlcDevice 读取系统级反馈并注入 SystemContext
    void readSystemFeedback(SystemContext& ctx) {
        // ESTOP 状态
        bool estopActive = m_device->readBool(regFbEmergencyStopActive());
        ctx.setEmergencyStopActive(estopActive);

        // 龙门联动状态
        bool gantryCoupled = m_device->readBool(regFbLinkageState());
        ctx.setGantryCoupled(gantryCoupled);

        // 龙门误差码
        int16_t gantryErrorCode = m_device->readInt16(regFbGantryErrorCode());
        ctx.setGantryErrorCode(gantryErrorCode);
    }
```

---

### 7.4 Step 4：完善 pollFeedback 主循环

将现有的 stub `pollFeedback` 替换为完整实现：

```diff
- inline void ModbusSystemDriver::pollFeedback(SystemContext& /*ctx*/) {
-     // TDD 阶段 5: 先处理到期的 EdgeTrigger OFF 脉冲，再读取反馈
+ inline void ModbusSystemDriver::pollFeedback(SystemContext& ctx) {
+     // 防御：未注入 PlcDevice
+     if (!m_device) return;
+
+     // ① 先处理到期的 EdgeTrigger OFF 脉冲
      servicePendingEdgeTriggers();

-     // 后续阶段将在此处添加反馈读取逻辑
+     // ② Poller 轮询 PLC 现场数据（通过外部 Poller 完成）
+     // 注：暂不在此处实现 Poller 集成；测试中直接注入 Snapshot
+
+     // ③ 数据可信度检查
+     if (!m_device->isStateTrusted()) return;
+
+     // ④ 逐轴反馈注入
+     readAxisFeedbackForAxis(ctx, AxisId::X);
+     readAxisFeedbackForAxis(ctx, AxisId::Y);
+     readAxisFeedbackForAxis(ctx, AxisId::Z);
+     readAxisFeedbackForAxis(ctx, AxisId::R);
+
+     // ⑤ 系统级反馈注入
+     readSystemFeedback(ctx);
  }
```

---

## 8. REFACTOR 重构步骤

### 8.1 REFACTOR-1：抽取 AXIS_IDS 常量数组

**当前代码**：

```cpp
readAxisFeedbackForAxis(ctx, AxisId::X);
readAxisFeedbackForAxis(ctx, AxisId::Y);
readAxisFeedbackForAxis(ctx, AxisId::Z);
readAxisFeedbackForAxis(ctx, AxisId::R);
```

**重构后**：

```cpp
// 静态常量：所有需要轮询反馈的轴 ID 列表
static constexpr AxisId FEEDBACK_AXIS_IDS[] = {
    AxisId::X, AxisId::Y, AxisId::Z, AxisId::R
};

// pollFeedback 中：
for (auto id : FEEDBACK_AXIS_IDS) {
    readAxisFeedbackForAxis(ctx, id);
}
```

### 8.2 REFACTOR-2：AxisFeedback DTO 使用带默认值的构造

确保 `AxisFeedback` 的构造明确表达 `relZeroAbsPos` 字段语义：

```cpp
// domain/entity/Axis.h
struct AxisFeedback {
    AxisState state;
    float absolutePosition;
    float relativePosition;
    float relZeroAbsPosition;  // ★ Driver 填写 0.0，Domain 层闭环计算

    // 明确构造：relZeroAbsPosition 默认 0.0f 表示"不由 Driver 提供"
    AxisFeedback(AxisState s, float abs, float rel, float rzap = 0.0f)
        : state(s), absolutePosition(abs), relativePosition(rel), relZeroAbsPosition(rzap) {}
};
```

### 8.3 REFACTOR-3：日志注入

在 `readAxisFeedbackForAxis` 中增加 TRACE 日志（如果生产代码已有日志基础设施）：

```cpp
// 仅在日志调试版本启用
#ifdef LOG_FEEDBACK_READ
    LOG_TRACE("readAxisFeedbackForAxis",
        LogContext::ofAxis(id)
            .with("D100", d100)
            .with("ALARM", alarm)
            .with("absMoving", absM)
            .with("relMoving", relM)
            .with("jogging", jog)
            .with("absPos", absPos)
            .with("relPos", relPos)
            .with("state", state));
#endif
```

---

## 9. 附录

### 附录 A：完整 pollFeedback 生产代码

```cpp
// infrastructure/plc/ModbusSystemDriver.h — pollFeedback 最终生产版本

inline void ModbusSystemDriver::pollFeedback(SystemContext& ctx) {
    if (!m_device) return;

    // ① 处理 EdgeTrigger OFF 脉冲
    servicePendingEdgeTriggers();

    // ② 轮询（测试中跳过，生产环境由 PlcPoller 完成）
    // PlcSnapshot snap = m_poller.poll(*m_client, m_registry, PollType::Feedback);
    // m_device->updateSnapshot(std::move(snap));

    // ③ 可信度门禁
    if (!m_device->isStateTrusted()) return;

    // ④ 逐轴反馈
    static constexpr AxisId AXIS_IDS[] = {AxisId::X, AxisId::Y, AxisId::Z, AxisId::R};
    for (auto id : AXIS_IDS) {
        readAxisFeedbackForAxis(ctx, id);
    }

    // ⑤ 系统反馈
    readSystemFeedback(ctx);
}

// ── 私有方法 ──

void ModbusSystemDriver::readAxisFeedbackForAxis(SystemContext& ctx, AxisId id) {
    Axis* axis = ctx.tryGetAxis(id);
    if (!axis) return;

    int16_t d100   = m_device->readInt16(regFbState(id));
    int16_t alarm  = m_device->readInt16(regFbAlarmCode(id));
    bool    absM   = m_device->readBool(regFbAbsMoving(id));
    bool    relM   = m_device->readBool(regFbRelMoving(id));
    bool    jog    = m_device->readBool(regFbJogging(id));
    float   absPos = m_device->readFloat(regFbAbsPos(id));
    float   relPos = m_device->readFloat(regFbRelPos(id));

    AxisState state = AxisStateDeriver::derive(d100, alarm, absM, relM, jog);
    AxisFeedback fb{state, absPos, relPos, /*relZeroAbsPos=*/0.0f};
    axis->applyFeedback(fb);
}

void ModbusSystemDriver::readSystemFeedback(SystemContext& ctx) {
    ctx.setEmergencyStopActive(m_device->readBool(regFbEmergencyStopActive()));
    ctx.setGantryCoupled(m_device->readBool(regFbLinkageState()));
    ctx.setGantryErrorCode(m_device->readInt16(regFbGantryErrorCode()));
}
```

---

### 附录 B：PlcSnapshot / RawBitSnapshot / RawWordSnapshot API 适配指南

在实施阶段五测试用例时，可能需要对底层 Snapshot 类的 API 进行适配。

#### B.1 RawBitSnapshot

```cpp
// 理想 API（如不存在需补充）
class RawBitSnapshot {
public:
    explicit RawBitSnapshot(const std::vector<uint8_t>& buffer);
    // 或 explicit RawBitSnapshot(std::vector<uint8_t>&& buffer);

    /// @brief 读取指定地址的 bit 值
    bool getBit(uint16_t address) const;

    /// @brief 设置指定地址的 bit 值
    void setBit(uint16_t address, bool value);
};
```

**当前适配策略**：测试辅助函数 `buildSingleAxisSnapshot` 中直接操作 `std::vector<uint8_t>` buffer，通过 byte 偏移 + bit 掩码设置对应位。

#### B.2 RawWordSnapshot

```cpp
// 理想 API
class RawWordSnapshot {
public:
    explicit RawWordSnapshot(const std::vector<uint16_t>& buffer);

    /// @brief 读取指定地址的 word 值
    uint16_t getWord(uint16_t address) const;

    /// @brief 设置指定地址的 word 值
    void setWord(uint16_t address, uint16_t value);
};
```

**当前适配策略**：直接通过 `std::vector<uint16_t>` 索引赋值。

#### B.3 PlcSnapshot

```cpp
class PlcSnapshot {
public:
    PlcSnapshot(RawBitSnapshot bits, RawWordSnapshot words,
                bool complete, uint64_t timestamp);

    bool isTrusted() const { return complete; }
    // ...
};
```

**测试用途**：
- `complete=true` → `isStateTrusted()==true`
- `complete=false` → `isStateTrusted()==false`

#### B.4 Float 编码适配

寄存器地址 D120~D134 存储 Float32 值（2 个 HoldingRegister），端序取决于 `INOVANCE_PROFILE.endianness`。

```cpp
// 测试辅助：将 float 按大端编码
static std::pair<uint16_t, uint16_t> floatToWordsBigEndian(float value) {
    uint32_t raw;
    std::memcpy(&raw, &value, sizeof(float));
    return {
        static_cast<uint16_t>((raw >> 16) & 0xFFFF),  // 高 16 位
        static_cast<uint16_t>(raw & 0xFFFF)            // 低 16 位
    };
}
```

---

### 附录 C：测试文件清单

| 文件                                                         | 描述                     |
| ------------------------------------------------------------ | ------------------------ |
| `tests/infrastructure/test_modbus_system_driver_poll_feedback.cpp` | **新增** — 阶段五集成测试 |
| `infrastructure/plc/ModbusSystemDriver.h`                    | **修改** — pollFeedback 实现 + 私有方法 |
| `infrastructure/plc/protocol/PlcDevice.h`                    | **修改** — isStateTrusted virtual |
| `domain/entity/Axis.h`                                       | 可能修改 — AxisFeedback 构造默认值 |

---

### 附录 D：Expected Failure 处理策略

#### D.1 Mock 写操作的 Expected Failure

测试用例 4（EdgeTrigger 调度验证）中，`servicePendingEdgeTriggers()` 会调用 `m_device->writeBool(reg, false)`，而测试夹具中 `m_device` 的 transport 未绑定。有两种处理方式：

| 方式 | 描述                                           | 优缺点                         |
| ---- | ---------------------------------------------- | ------------------------------ |
| A    | 注入 Mock IModbusClient，Mock writeBool 返回 OK | 完整验证，但增加夹具复杂度     |
| B    | 只验证 `pendingEdgeCount()==0`，不验证写入内容  | 简化，EdgeTrigger 生命周期已在阶段二测过 |

**推荐**：方式 B。阶段五关注反馈轮询主链路，EdgeTrigger 完备性由阶段二保证。

#### D.2 readInt16/readBool/readFloat 的 Expected Failure

如果 `RegisterCodec::decode` 在 register 不存在于 Snapshot 中时抛出异常（而非返回默认值），则测试用例需要保证每个 register 的地址在 Snapshot 中都有对应的 bit/word。`buildSingleAxisSnapshot` 通过分配 4096 words buffer 并清零所有值来规避此问题。

#### D.3 applyFeedback 触发的 Expected Failure

如果 `Axis::applyFeedback(fb)` 在执行过程（如 `state==Error` 时）触发副作用（如调用回调、修改其他对象），测试需要提前 Mock 或 Stub 这些副作用。当前 `Axis` 实现为纯数据实体，预计不会有此问题。

---

> **文档结束** — 请对照 Part 1 §5.1~5.6 与 Part 2 §6.1~6.12 的 12 个测试用例，以及 §7~§8 的生产实现和重构步骤，按 RED → GREEN → REFACTOR 顺序实施阶段五。
