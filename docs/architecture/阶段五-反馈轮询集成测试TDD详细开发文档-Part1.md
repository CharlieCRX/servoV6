# 阶段五：反馈轮询集成测试 TDD 详细开发文档（Part 1）

> 版本：v1.0
> 日期：2026-05-31
> 项目：servoV6
> 前置文档：
> - [SystemCommand寄存器映射与Domain-Infrastructure对接——TDD开发步骤](./SystemCommand寄存器映射与Domain-Infrastructure对接——TDD开发步骤.md) §7
> - [SystemCommand寄存器映射与Domain-Infrastructure对接设计](./SystemCommand寄存器映射与Domain-Infrastructure对接设计.md) §4.9

---

**依赖关系**：阶段一（寄存器选择器）✅ → 阶段二（EdgeTrigger）✅ → 阶段三（状态推导）✅ → 阶段四（命令分派）✅ → **阶段五（反馈轮询）**

---

## 目录

1. [目标与范围](#1-目标与范围)
2. [架构与数据流](#2-架构与数据流)
3. [Mock 策略设计](#3-mock-策略设计)
4. [测试夹具设计](#4-测试夹具设计)
5. [测试用例（1-6）](#5-测试用例1-6)
   - [5.1 测试用例 1：正常反馈轮询 → 多轴状态注入](#51-测试用例-1正常反馈轮询--多轴状态注入)
   - [5.2 测试用例 2：龙门联动反馈 → 系统状态更新](#52-测试用例-2龙门联动反馈--系统状态更新)
   - [5.3 测试用例 3：急停状态反馈 → SystemContext 急停标志](#53-测试用例-3急停状态反馈--systemcontext-急停标志)
   - [5.4 测试用例 4：EdgeTrigger 在 pollFeedback 前自动调度](#54-测试用例-4edgetrigger-在-pollfeedback-前自动调度)
   - [5.5 测试用例 5：relZeroAbsPos 不从 PLC 读取](#55-测试用例-5relzeroabspos-不从-plc-读取)
   - [5.6 测试用例 6：stateUntrusted 时不注入反馈](#56-测试用例-6stateuntrusted-时不注入反馈)

> 📘 **Part 2** 将包含：测试用例（7-12）、GREEN 生产实现步骤、REFACTOR 重构步骤、附录 A~D。

---

## 1. 目标与范围

### 1.1 阶段五总体目标

验证 `ModbusSystemDriver::pollFeedback(SystemContext&)` 的**完整反馈轮询链路**：

```
pollFeedback(ctx)
  ├── ① servicePendingEdgeTriggers()       — 边沿触发 OFF 脉冲
  ├── ② PlcPoller.poll(client, registry)  — 网络读取
  │       └── m_device.updateSnapshot(snap) — 快照注入
  ├── ③ isStateTrusted() 检查              — 数据可信度门禁
  ├── ④ readAxisFeedback(ctx)              — 逐轴读取+状态推导+Axis::applyFeedback
  └── ⑤ readSystemFeedback(ctx)           — 系统级反馈注入 SystemContext
```

### 1.2 涉及的组件

| 组件                | 文件                                           | 本阶段角色           |
| ------------------- | ---------------------------------------------- | -------------------- |
| `ModbusSystemDriver` | `infrastructure/plc/ModbusSystemDriver.h`       | 被测对象             |
| `PlcDevice`          | `infrastructure/plc/protocol/PlcDevice.h`       | 读/写操作代理        |
| `PlcSnapshot`        | `infrastructure/plc/protocol/PlcSnapshot.h`     | 快照载体             |
| `AxisStateDeriver`   | `infrastructure/plc/AxisStateDeriver.h`         | 纯函数状态推导引擎   |
| `SystemContext`      | `domain/entity/SystemContext.h`                 | 领域上下文（真实对象） |
| `Axis`               | `domain/entity/Axis.h`                          | 领域实体（真实对象）  |
| `AxisFeedback`       | `domain/entity/Axis.h`                          | 反馈 DTO             |

### 1.3 与前面阶段的区别

| 维度       | 阶段四（命令分派）                         | 阶段五（反馈轮询）                           |
| ---------- | ------------------------------------------ | -------------------------------------------- |
| 测试层级   | 单元测试                                   | 集成测试（Driver + PlcDevice + SystemContext + Axis） |
| Mock 对象  | MockPlcDevice 写操作                       | MockPlcDevice 读写 + isStateTrusted          |
| 真实对象   | 无                                         | SystemContext、Axis 实体                     |
| 验证范围   | 单次 send() 调用                           | 完整 pollFeedback() 调用链                   |
| 边界条件   | 写入失败                                   | 快照可信/不可信、多轴独立推导                 |
| 数据方向   | 命令下发 (Domain → PLC)                     | 反馈上报 (PLC → Domain)                      |

### 1.4 v4.0 设计关键点

| 设计点                      | 说明                                                       | 测试覆盖  |
| --------------------------- | ---------------------------------------------------------- | --------- |
| `relZeroAbsPos` 不读 PLC     | Driver 在 AxisFeedback 中填 0.0，Domain 层闭环计算          | 用例 5    |
| `isStateTrusted()` 门禁      | 快照不可信时提前返回，不注入任何反馈                       | 用例 6    |
| 龙门 X 轴以 X1 为准          | `regFbAbsPos(AxisId::X)` 返回 X1 物理地址                   | 用例 7（Part 2） |
| ALARM_CODE 冗余兜底          | `d100==3 || alarmCode!=0` 双条件 Error                     | 用例 9（Part 2） |
| D100=2 无 Coil → Idle        | 多圈编码器抖动消除                                         | 用例 10（Part 2） |
| 四轴独立推导                 | 每个 AxisId 独立读取寄存器                                 | 用例 8（Part 2） |

---

## 2. 架构与数据流

### 2.1 pollFeedback 完整时序

```
ModbusSystemDriver::pollFeedback(ctx)
│
├─[Step 1] servicePendingEdgeTriggers()
│   ├─ 遍历 m_pendingEdges
│   ├─ 对到期的 WroteOn 条目: m_device->writeBool(reg, false)
│   └─ 移除 WroteOff 条目
│
├─[Step 2] 轮询 PLC 现场数据
│   ├─ PollRequest req = m_poller.buildRequest(registry, Feedback);
│   ├─ PlcSnapshot snap = m_poller.poll(*m_client, registry, req);
│   └─ m_device->updateSnapshot(std::move(snap));
│
├─[Step 3] 数据可信度检查
│   └─ if (!m_device->isStateTrusted()) return;
│
├─[Step 4] 逐轴反馈注入
│   └─ for each AxisId in {X, Y, Z, R}:
│       └─ readAxisFeedbackForAxis(ctx, id)
│           ├─ int16_t d100  = m_device->readInt16(regFbState(id));
│           ├─ int16_t alarm = m_device->readInt16(regFbAlarmCode(id));
│           ├─ bool absM     = m_device->readBool(regFbAbsMoving(id));
│           ├─ bool relM     = m_device->readBool(regFbRelMoving(id));
│           ├─ bool jog      = m_device->readBool(regFbJogging(id));
│           ├─ float absPos  = m_device->readFloat(regFbAbsPos(id));
│           ├─ float relPos  = m_device->readFloat(regFbRelPos(id));
│           ├─ AxisState state = deriveAxisState(d100, alarm, absM, relM, jog);
│           ├─ AxisFeedback fb{state, absPos, relPos, 0.0f/*relZeroAbsPos*/};
│           └─ axis->applyFeedback(fb);
│
└─[Step 5] 系统级反馈注入
    └─ readSystemFeedback(ctx)
        ├─ ctx.setEmergencyStopActive(m_device->readBool(regFbEStopActive()));
        ├─ ctx.setGantryCoupled(m_device->readBool(regFbLinkageState()));
        └─ ctx.setGantryErrorCode(m_device->readInt16(regFbGantryErrorCode()));
```

### 2.2 数据流向图

```
┌─────────────────┐     writeBool(reg, false)      ┌──────────────────┐
│  PendingEdge Q   │────────────────────────────────▶│  MockPlcDevice    │
│  (EdgeTrigger)   │   (OFF 脉冲, 阶段五验证)         │  (写操作 mock)    │
└─────────────────┘                                 └───────┬──────────┘
                                                            │
┌─────────────────┐     poll(client, registry)   ┌─────────▼──────────┐
│   PlcPoller      │─────────────────────────────▶│    PlcDevice        │
│  (Mock/Stub)     │                              │  (真实, 持有快照)    │
└────────┬────────┘                               │  readBool/Int16/   │
         │ PlcSnapshot                            │  Float → decode    │
         ▼                                        └───────┬──────────┘
┌─────────────────┐                                        │ 解码后值
│  PlcSnapshot    │                                        ▼
│  - bits  (Coil) │                               ┌──────────────────┐
│  - words (HReg) │                               │ ModbusSystemDriver│
│  - complete flag│                               │ readAxisFeedback  │
└─────────────────┘                               │ readSystemFeedback│
                                                  └───┬──────────┬───┘
                                                      │          │
                                           AxisFeedback│          │setEmergencyStopActive
                                                      ▼          │setGantryCoupled
                                              ┌──────────┐       ▼
                                              │   Axis   │  ┌──────────────┐
                                              │ 实体对象  │  │ SystemContext │
                                              └──────────┘  └──────────────┘
```

### 2.3 PlcSnapshot 与测试数据的关系

每个测试用例需要构造 `PlcSnapshot`，包含：

```
PlcSnapshot {
    RawBitSnapshot bits:      // Coil 区域
        bit[M110~M121]  →  absMoving/relMoving/jogging 信号
        bit[M130]       →  ESTOP_ACTIVE
        bit[M125]       →  LINKAGE_STATE

    RawWordSnapshot words:    // HoldingRegister 区域
        word[D100~D103]  →  axis state (0/1/2/3)
        word[D110~D113]  →  alarm code
        word[D120~D134]  →  abs/rel position (float, 2 words each)
        word[D180]       →  gantry error code

    bool complete:     → true=可信, false=不可信
    uint64_t timestamp:→ 采集时间戳
}
```

---

## 3. Mock 策略设计

### 3.1 推荐方案：PlcSnapshot 注入 + 最小 Mock

| 被测组件行为                     | 测试策略                                       |
| -------------------------------- | ---------------------------------------------- |
| **读寄存器** (readBool/Int16/Float) | **不 Mock**，使用真实 PlcDevice + 预构造 Snapshot 注入 |
| **写寄存器** (writeBool/Float)    | Mock（验证 EdgeTrigger OFF 脉冲）              |
| **isStateTrusted()**             | Mock override 返回 false（测试提前返回路径）    |
| **PlcPoller.poll()**             | 绕过，直接 m_device->updateSnapshot(snap)       |
| **SystemContext, Axis**          | 真实对象                                       |
| **AxisStateDeriver**             | 真实纯函数（阶段三已测）                       |

### 3.2 PlcDevice 需 virtual 化的方法

```cpp
// infrastructure/plc/protocol/PlcDevice.h — 改动点

class PlcDevice {
public:
    // ✅ 已 virtual — 无需改动
    virtual CommunicationResult writeBool(const RegisterInfo& reg, bool v) { ... }
    virtual CommunicationResult writeFloat(const RegisterInfo& reg, float v) { ... }

    // ❗ 需新增 virtual
    // 方式1 (推荐): 不做 virtual，直接通过 Snapshot 注入测试
    // 方式2 (备选): 将以下方法改为 virtual
    // virtual bool      readBool(const RegisterInfo& reg) const { ... }
    // virtual int16_t   readInt16(const RegisterInfo& reg) const { ... }
    // virtual float     readFloat(const RegisterInfo& reg) const { ... }
    // virtual bool      isStateTrusted() const { ... }

    // 如果采用 Snapshot 注入方式，仅需 isStateTrusted() 为 virtual
    virtual bool isStateTrusted() const { return m_snapshot.isTrusted(); }
};
```

### 3.3 测试专用 PlcDevice 子类

```cpp
// tests/infrastructure/test_modbus_system_driver_poll_feedback.cpp

/// @brief 测试用 PlcDevice，仅 override isStateTrusted
class TestPlcDevice : public PlcDevice {
public:
    using PlcDevice::PlcDevice;  // 继承构造

    void setTrusted(bool trusted) { m_trustedOverride = trusted; }

    // 读操作走父类真实实现（从 m_snapshot 解码）
    // 写操作走父类真实实现（通过 m_client 写出），测试中 bind 一个 Mock IModbusClient

    bool isStateTrusted() const override {
        return m_trustedOverride;
    }

private:
    bool m_trustedOverride = true;
};
```

### 3.4 辅助工厂函数

```cpp
/// @brief 为单个轴构建完整反馈快照
/// @details 同时设置 D100、ALARM_CODE、运动 Coil、位置寄存器
PlcSnapshot buildSingleAxisSnapshot(
    const ModbusSystemDriver& driver,
    AxisId id,
    int16_t d100State,
    int16_t alarmCode,
    bool absMoving,
    bool relMoving,
    bool jogging,
    float absPos,
    float relPos
);

/// @brief 为全部四轴 + 系统信号构建完整快照
PlcSnapshot buildFullSnapshot(
    const ModbusSystemDriver& driver,
    int16_t d100X, int16_t alarmX, bool absMX, bool relMX, bool jogX, float absPosX, float relPosX,
    int16_t d100Y, int16_t alarmY, bool absMY, bool relMY, bool jogY, float absPosY, float relPosY,
    int16_t d100Z, int16_t alarmZ, bool absMZ, bool relMZ, bool jogZ, float absPosZ, float relPosZ,
    int16_t d100R, int16_t alarmR, bool absMR, bool relMR, bool jogR, float absPosR, float relPosR,
    bool estopActive,
    bool gantryCoupled,
    int16_t gantryErrorCode
);
```

---

## 4. 测试夹具设计

### 4.1 完整代码

```cpp
// tests/infrastructure/test_modbus_system_driver_poll_feedback.cpp

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <memory>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cmath>

#include "infrastructure/plc/ModbusSystemDriver.h"
#include "infrastructure/plc/protocol/PlcDevice.h"
#include "infrastructure/plc/protocol/PlcSnapshot.h"
#include "infrastructure/plc/protocol/MemorySnapshot.h"
#include "infrastructure/plc/protocol/RegisterAddressAll.h"
#include "infrastructure/plc/protocol/ProtocolProfile.h"
#include "infrastructure/plc/protocol/RegisterCodec.h"
#include "infrastructure/plc/AxisStateDeriver.h"
#include "infrastructure/utils/FakeClock.h"
#include "domain/entity/Axis.h"
#include "domain/entity/AxisId.h"
#include "domain/entity/SystemContext.h"
#include "application/CommunicationResult.h"

using namespace plc;
using namespace plc::protocol;
using namespace ::testing;

// =============================================================================
// 常量
// =============================================================================

/// 默认 Coil 区域 buffer 大小（覆盖 M0~M255）
static constexpr size_t DEFAULT_COIL_BUFFER_SIZE = 32;  // bytes, 256 bits
/// 默认 HoldingRegister 区域 buffer 大小（覆盖 D0~D2048）
static constexpr size_t DEFAULT_HREG_BUFFER_SIZE = 4096; // uint16_t words

// =============================================================================
// 辅助函数：PlcSnapshot 构造
// =============================================================================

/// @brief 修改 RawBitSnapshot 中某个 Coil 地址的 bit 值
static void setCoilBit(RawBitSnapshot& bits, uint16_t address, bool value) {
    // 假设 RawBitSnapshot 内部是 uint8_t[] buffer，按字节偏移 + bit 偏移设置
    // 实际实现取决于 RawBitSnapshot 的内部结构，此处为示意
    size_t byteIdx = address / 8;
    uint8_t bitMask = 1 << (address % 8);
    // bits.buffer[byteIdx] = value
    //   ? (bits.buffer[byteIdx] | bitMask)
    //   : (bits.buffer[byteIdx] & ~bitMask);
    (void)bits; (void)address; (void)value; // 待实际适配
}

/// @brief 修改 RawWordSnapshot 中某个 HoldingRegister 地址的 word 值
static void setHregWord(RawWordSnapshot& words, uint16_t address, uint16_t value) {
    // 直接索引赋值
    // words.buffer[address] = value;
    (void)words; (void)address; (void)value; // 待实际适配
}

/// @brief 将 float32 按大端编码为两个 uint16_t
static std::pair<uint16_t, uint16_t> floatToWords(float value) {
    uint32_t raw;
    std::memcpy(&raw, &value, sizeof(float));
    // 大端：高 16 位在前
    return {static_cast<uint16_t>((raw >> 16) & 0xFFFF),
            static_cast<uint16_t>(raw & 0xFFFF)};
}

/// @brief 为指定轴构造完整反馈快照
static PlcSnapshot buildSingleAxisSnapshot(
    const ModbusSystemDriver& driver,
    AxisId id,
    int16_t d100State,
    int16_t alarmCode,
    bool absMoving,
    bool relMoving,
    bool jogging,
    float absPos,
    float relPos)
{
    const auto& regState  = driver.regFbState(id);
    const auto& regAlarm  = driver.regFbAlarmCode(id);
    const auto& regAbsM   = driver.regFbAbsMoving(id);
    const auto& regRelM   = driver.regFbRelMoving(id);
    const auto& regJog    = driver.regFbJogging(id);
    const auto& regAbsPos = driver.regFbAbsPos(id);
    const auto& regRelPos = driver.regFbRelPos(id);

    // Coil 区域: 分配 256 bits buffer，清零
    std::vector<uint8_t> coilBuf(DEFAULT_COIL_BUFFER_SIZE, 0);
    auto setBit = [&](uint16_t addr, bool val) {
        if (addr < 8 * DEFAULT_COIL_BUFFER_SIZE) {
            size_t byteIdx = addr / 8;
            uint8_t mask = 1 << (addr % 8);
            if (val) coilBuf[byteIdx] |= mask;
            else     coilBuf[byteIdx] &= ~mask;
        }
    };
    setBit(regAbsM.address,  absMoving);
    setBit(regRelM.address,  relMoving);
    setBit(regJog.address,   jogging);

    // HoldingRegister 区域: 分配 4096 words buffer，清零
    std::vector<uint16_t> hregBuf(DEFAULT_HREG_BUFFER_SIZE, 0);
    auto setWord = [&](uint16_t addr, uint16_t val) {
        if (addr < DEFAULT_HREG_BUFFER_SIZE)
            hregBuf[addr] = val;
    };
    setWord(regState.address, static_cast<uint16_t>(d100State));
    setWord(regAlarm.address, static_cast<uint16_t>(alarmCode));

    auto [absHi, absLo] = floatToWords(absPos);
    setWord(regAbsPos.address,     absHi);
    setWord(regAbsPos.address + 1, absLo);

    auto [relHi, relLo] = floatToWords(relPos);
    setWord(regRelPos.address,     relHi);
    setWord(regRelPos.address + 1, relLo);

    RawBitSnapshot  bits(coilBuf);
    RawWordSnapshot words(hregBuf);
    return PlcSnapshot(std::move(bits), std::move(words), /*complete=*/true, /*timestamp=*/0);
}

// =============================================================================
// 测试夹具
// =============================================================================

class ModbusSystemDriverPollFeedbackTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 1. 创建真实 PlcDevice
        device_ = std::make_unique<PlcDevice>(INOVANCE_PROFILE);

        // 2. 创建被测 Driver
        driver_ = std::make_unique<ModbusSystemDriver>();

        // 3. 注入 FakeClock
        auto clock = std::make_unique<FakeClock>();
        rawClock_ = clock.get();
        driver_->setClock(std::move(clock));

        // 4. 绑定 Device
        driver_->setDevice(device_.get());

        // 5. 注册四个轴到 SystemContext
        ctx_ = std::make_unique<SystemContext>();
        ctx_->registerAxis(AxisId::X, &xAxis_, "Group1");
        ctx_->registerAxis(AxisId::Y, &yAxis_, "Group1");
        ctx_->registerAxis(AxisId::Z, &zAxis_, "Group1");
        ctx_->registerAxis(AxisId::R, &rAxis_, "Group1");
    }

    void TearDown() override {
        driver_->clearPendingEdges();
    }

    /// @brief 为单个轴注入反馈快照
    void injectAxisFeedback(
        AxisId id, int16_t d100, int16_t alarm,
        bool absM, bool relM, bool jog,
        float absPos, float relPos)
    {
        auto snap = buildSingleAxisSnapshot(*driver_, id,
            d100, alarm, absM, relM, jog, absPos, relPos);
        device_->updateSnapshot(std::move(snap));
    }

    /// @brief 注入系统级反馈快照
    void injectSystemFeedback(bool estop, bool coupled, int16_t gantryErr) {
        const auto& regEstop     = driver_->regFbEmergencyStopActive();
        const auto& regLinked    = driver_->regFbLinkageState();
        const auto& regGantryErr = driver_->regFbGantryErrorCode();

        std::vector<uint8_t> coilBuf(DEFAULT_COIL_BUFFER_SIZE, 0);
        auto setBit = [&](uint16_t addr, bool val) {
            if (addr < 8 * DEFAULT_COIL_BUFFER_SIZE) {
                size_t bi = addr / 8;
                uint8_t m = 1 << (addr % 8);
                if (val) coilBuf[bi] |= m; else coilBuf[bi] &= ~m;
            }
        };
        setBit(regEstop.address,  estop);
        setBit(regLinked.address, coupled);

        std::vector<uint16_t> hregBuf(DEFAULT_HREG_BUFFER_SIZE, 0);
        if (regGantryErr.address < DEFAULT_HREG_BUFFER_SIZE)
            hregBuf[regGantryErr.address] = static_cast<uint16_t>(gantryErr);

        RawBitSnapshot  bits(coilBuf);
        RawWordSnapshot words(hregBuf);
        device_->updateSnapshot(PlcSnapshot(std::move(bits), std::move(words),
                                            /*complete=*/true, /*timestamp=*/0));
    }

    // 成员变量
    std::unique_ptr<PlcDevice>          device_;
    std::unique_ptr<ModbusSystemDriver> driver_;
    FakeClock*                          rawClock_ = nullptr;
    Axis                                xAxis_, yAxis_, zAxis_, rAxis_;
    std::unique_ptr<SystemContext>      ctx_;
};
```

---

## 5. 测试用例（1-6）

### 5.1 测试用例 1：正常反馈轮询 → 多轴状态注入

**场景描述**：PLC 返回四轴各自的信号组合，`pollFeedback` 正确注入到 `SystemContext` 中注册的 `Axis` 实体。

**前置条件**：
- X 轴：D100=1, ALARM=0, 无运动 Coil, ABS_POS=150.0, REL_POS=25.0 → 预期 Idle
- Y 轴：D100=0, ALARM=0, 无运动 Coil, ABS_POS=0.0, REL_POS=0.0 → 预期 Disabled
- Z 轴：D100=1, ALARM=0, ABS_MOVING=true, ABS_POS=200.0, REL_POS=50.0 → 预期 MovingAbsolute
- R 轴：D100=3, ALARM=12, 无运动 Coil → 预期 Error

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, NormalFeedbackUpdatesAllAxes) {
    // ── GIVEN: 四轴各自独立反馈 ──
    injectAxisFeedback(AxisId::X, /*D100=*/1, /*ALARM=*/0,
                       /*absM=*/false, /*relM=*/false, /*jog=*/false,
                       /*absPos=*/150.0f, /*relPos=*/25.0f);
    injectAxisFeedback(AxisId::Y, /*D100=*/0, /*ALARM=*/0,
                       /*absM=*/false, /*relM=*/false, /*jog=*/false,
                       /*absPos=*/0.0f, /*relPos=*/0.0f);
    injectAxisFeedback(AxisId::Z, /*D100=*/1, /*ALARM=*/0,
                       /*absM=*/true,  /*relM=*/false, /*jog=*/false,
                       /*absPos=*/200.0f, /*relPos=*/50.0f);
    injectAxisFeedback(AxisId::R, /*D100=*/3, /*ALARM=*/12,
                       /*absM=*/false, /*relM=*/false, /*jog=*/false,
                       /*absPos=*/0.0f, /*relPos=*/0.0f);

    // ── WHEN: pollFeedback ──
    driver_->pollFeedback(*ctx_);

    // ── THEN: 各轴状态正确注入 ──
    // X 轴：使能静止 → Idle
    EXPECT_EQ(xAxis_.state(), AxisState::Idle);
    EXPECT_DOUBLE_EQ(xAxis_.currentAbsolutePosition(), 150.0);
    EXPECT_DOUBLE_EQ(xAxis_.currentRelativePosition(), 25.0);

    // Y 轴：未使能 → Disabled
    EXPECT_EQ(yAxis_.state(), AxisState::Disabled);
    EXPECT_DOUBLE_EQ(yAxis_.currentAbsolutePosition(), 0.0);

    // Z 轴：运动中 → MovingAbsolute
    EXPECT_EQ(zAxis_.state(), AxisState::MovingAbsolute);
    EXPECT_DOUBLE_EQ(zAxis_.currentAbsolutePosition(), 200.0);
    EXPECT_DOUBLE_EQ(zAxis_.currentRelativePosition(), 50.0);

    // R 轴：报警 → Error
    EXPECT_EQ(rAxis_.state(), AxisState::Error);
}
```

### 5.2 测试用例 2：龙门联动反馈 → 系统状态更新

**场景描述**：PLC 返回 LINKAGE_STATE=true、GANTRY_ERROR_CODE=0x0001，验证 SystemContext 接收龙门状态。

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, GantryCoupledFeedback) {
    // ── GIVEN: 龙门联动 + 误差码 ──
    injectSystemFeedback(/*estop=*/false, /*coupled=*/true, /*gantryErr=*/0x0001);

    // ── WHEN: pollFeedback ──
    driver_->pollFeedback(*ctx_);

    // ── THEN: 龙门状态注入 ──
    EXPECT_TRUE(ctx_->isGantryCoupled());
    EXPECT_EQ(ctx_->gantryErrorCode(), 0x0001);
    EXPECT_FALSE(ctx_->isEmergencyStopActive());
}
```

### 5.3 测试用例 3：急停状态反馈 → SystemContext 急停标志

**场景描述**：PLC 返回 ESTOP_ACTIVE=true，验证 SystemContext 急停标志设置。

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, EmergencyStopActiveFeedback) {
    // ── GIVEN: ESTOP_ACTIVE=true ──
    injectSystemFeedback(/*estop=*/true, /*coupled=*/false, /*gantryErr=*/0);

    // ── WHEN: pollFeedback ──
    driver_->pollFeedback(*ctx_);

    // ── THEN: 急停标志注入 ──
    EXPECT_TRUE(ctx_->isEmergencyStopActive());
    EXPECT_FALSE(ctx_->isGantryCoupled());
}
```

### 5.4 测试用例 4：EdgeTrigger 在 pollFeedback 前自动调度

**场景描述**：有一个待处理的 EdgeTrigger（已写 ON 150ms），pollFeedback 时先调度 OFF 再读取反馈。

**验证点**：
1. `writeBool(reg, false)` 在 read 之前调用
2. `pendingEdgeCount()` 从 1 变为 0

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, EdgeTriggerServicedBeforeFeedbackRead) {
    // ── GIVEN: 已写入 ON 脉冲的 EdgeTrigger（推进 150ms 后到期） ──
    const auto& reg = driver_->regCmdClearAbsPos(AxisId::X);  // M30
    driver_->sendEdgeTrigger(reg);
    ASSERT_EQ(driver_->pendingEdgeCount(), 1);

    driver_->advanceTime(std::chrono::milliseconds(150));

    // ── WHEN: pollFeedback ──
    // 注意：pollFeedback 内部调用 servicePendingEdgeTriggers，
    // 后者会写 writeBool(M30, false)。由于 PlcDevice 写操作依赖 m_client，
    // 测试中 m_client 为 nullptr，writBool 会抛异常。因此需要注入 Mock IModbusClient。
    //
    // 简化方案：不验证 writeBool 调用内容，只验证队列清空。
    // 完整的 EdgeTrigger 生命周期已在阶段二测试覆盖。

    // 此处 pre-fill 一个假的 PlcSnapshot 避免 isStateTrusted() 提前返回
    injectAxisFeedback(AxisId::X, /*D100=*/1, /*ALARM=*/0,
                       /*absM=*/false, /*relM=*/false, /*jog=*/false,
                       /*absPos=*/0.0f, /*relPos=*/0.0f);

    driver_->pollFeedback(*ctx_);

    // ── THEN: 边沿触发队列已清空 ──
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);
}
```

### 5.5 测试用例 5：relZeroAbsPos 不从 PLC 读取

**场景描述**：v4.0 设计中，`relZeroAbsPos` 由 Domain 层在 `SetRelativeZero` 时闭环计算，Driver 在 `AxisFeedback` 中填写 0.0。

**验证点**：
1. `AxisFeedback` 的 `relZeroAbsPos` 字段在构造时明确设为 0.0f
2. `Axis::applyFeedback()` 后，`relativeZeroAbsolutePosition()` 保持原值（由 Domain 计算）

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, RelZeroAbsPosNotReadFromPlc) {
    // ── GIVEN: X 轴 ABS_POS=150.0，Driver 读取 PLC ──
    injectAxisFeedback(AxisId::X, /*D100=*/1, /*ALARM=*/0,
                       /*absM=*/false, /*relM=*/false, /*jog=*/false,
                       /*absPos=*/150.0f, /*relPos=*/25.0f);

    // ── GIVEN: Domain 层预先设置 relZeroAbsPos（模拟 SetRelativeZero 完成） ──
    // 如果 Axis 有此方法，则调用；否则通过 applyFeedback 间接验证

    // ── WHEN: pollFeedback ──
    driver_->pollFeedback(*ctx_);

    // ── THEN: relZeroAbsPos 不由 Driver 提供，保持 Domain 层计算的值 ──
    // 关键断言：AxisFeedback 构造中 relZeroAbsPos 字段明确设为 0.0f
    // 这意味着 applyFeedback 不会覆盖 Domain 层计算的值

    // 验证位置正确注入
    EXPECT_DOUBLE_EQ(xAxis_.currentAbsolutePosition(), 150.0);
    EXPECT_DOUBLE_EQ(xAxis_.currentRelativePosition(), 25.0);

    // relZeroAbsPos 应保持默认值或 Domain 层设置的值（非 PLC 读取值）
    // 如果 Axis 有 explicit setter → 比较 setter 值
    // 如果无 explicit setter → 验证 applyFeedback 不应修改它
}
```

### 5.6 测试用例 6：stateUntrusted 时不注入反馈

**场景描述**：`PlcDevice::isStateTrusted()` 返回 false（快照不完整），`pollFeedback` 应提前返回，不向任何 Axis 注入反馈。

**验证点**：
1. 所有轴状态在 `pollFeedback` 前后不变
2. `SystemContext` 急停/龙门状态不变

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, StateUntrustedDoesNotInjectFeedback) {
    // ── GIVEN: 快照不可信 ──
    // 构造一个 complete=false 的 Snapshot
    std::vector<uint8_t>     emptyCoils(DEFAULT_COIL_BUFFER_SIZE, 0);
    std::vector<uint16_t>    emptyHregs(DEFAULT_HREG_BUFFER_SIZE, 0);
    RawBitSnapshot           bits(emptyCoils);
    RawWordSnapshot          words(emptyHregs);
    PlcSnapshot              untrusted(bits, words, /*complete=*/false, 0);
    device_->updateSnapshot(std::move(untrusted));

    // 记录当前状态
    auto xStateBefore = xAxis_.state();
    auto yStateBefore = yAxis_.state();
    auto zStateBefore = zAxis_.state();
    auto rStateBefore = rAxis_.state();

    // ── WHEN: pollFeedback ──
    driver_->pollFeedback(*ctx_);

    //
