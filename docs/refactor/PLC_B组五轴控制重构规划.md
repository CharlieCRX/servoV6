# PLC B组五轴控制 —— 系统重构规划文档

> **文档日期**: 2026-06-05  
> **适用版本**: servoV6 (commit fccc4fe)  
> **前提**: PLC 端 B组寄存器地址已确定并预留（排除 PLC 编程时间，仅软件侧重构）

---

## 1. 现状分析

### 1.1 当前架构概览

```
┌──────────────────────────────────────────────────────────────────┐
│                         main.cpp                                 │
│  ┌─────────────────────┐          ┌─────────────────────┐        │
│  │   SystemContext     │          │   SystemContext     │        │
│  │   "Machine_A"       │          │   "Machine_B"       │        │
│  │ X, X1, X2, Y, Z, R  │          │ X, X1, X2, Y, Z, R  │        │
│  └────────┬────────────┘          └────────┬────────────┘        │
│           │                                │                     │
│  ┌────────▼────────────┐          ┌────────▼────────────┐        │
│  │  ModbusSystemDriver │          │  ModbusSystemDriver │        │
│  │  driverA            │          │  driverB            │        │
│  │  ┌──────────────┐   │          │  ┌──────────────┐   │        │
│  │  │ PlcPoller    │   │          │  │ PlcPoller    │   │        │
│  │  │ PlcDevice    │   │          │  │ PlcDevice    │   │        │
│  │  └──────┬───────┘   │          │  └──────┬───────┘   │        │
│  └─────────┼───────────┘          └─────────┼───────────┘        │
│            │                                │                    │
│  ┌─────────▼────────────────────────────────▼───────────┐        │
│  │          共享 RegisterRegistry (同一份寄存器地址表)      │        │
│  │  x_axis::command, y_axis::command, z_axis::command,  │        │
│  │  r_axis::command, system_global::command/feedback    │        │
│  └──────────────────────────────────────────────────────┘        │
│            │                                │                    │
│  ┌─────────▼────────┐              ┌────────▼─────────┐          │
│  │ AsioModbusTcp    │              │ AsioModbusTcp    │          │
│  │ 192.168.1.88:502 │              │ 127.0.0.1:502    │          │
│  └──────────────────┘              └──────────────────┘          │
└──────────────────────────────────────────────────────────────────┘
```

### 1.2 核心问题

| 问题 | 描述 |
|------|------|
| **共享寄存器表** | `Machine_A` 和 `Machine_B` 共用一个 `RegisterRegistry`，访问相同的 PLC 寄存器地址 |
| **硬编码选择器** | `ModbusSystemDriver` 的寄存器选择器（如 `regCmdEnable(id)`）硬编码返回固定命名空间下的 `RegisterInfo` 常量 |
| **无分组感知** | Driver 不知道自己在为哪个"组"服务——它只看到 AxisId，不感知 Bank A/B |

### 1.3 目标架构

```
┌──────────────────────────────────────────────────────────────────┐
│  Machine_A (Group A)              Machine_B (Group B)             │
│  轴: X, X1, X2, Y, Z, R          轴: X, X1, X2, Y, Z, R         │
│  ┌──────────────────┐            ┌──────────────────┐            │
│  │ ModbusSystemDriver│           │ ModbusSystemDriver│           │
│  │ RegisterMapA     │            │ RegisterMapB     │            │
│  │ (Bank A 地址)    │             │ (Bank B 地址)    │            │
│  └──────┬───────────┘            └──────┬───────────┘            │
│         │                               │                         │
│  ┌──────▼───────────┐            ┌──────▼───────────┐            │
│  │ RegisterRegistryA│            │ RegisterRegistryB│            │
│  │ (A组寄存器地址表) │            │ (B组寄存器地址表) │            │
│  └──────┬───────────┘            └──────┬───────────┘            │
│         │                               │                         │
│  ┌──────▼───────────┐            ┌──────▼───────────┐            │
│  │ AsioModbusTcp    │            │ AsioModbusTcp    │            │
│  │ 192.168.1.88:502│            │ 192.168.1.88:502│            │
│  └──────────────────┘            └──────────────────┘            │
│                                                                   │
│  注意: 同一 PLC IP, 不同寄存器地址区间 (Bank A / Bank B 区分)     │
└──────────────────────────────────────────────────────────────────┘
```

---

## 2. 重构策略设计

### 2.1 核心设计决策：Register Map 注入模式（Strategy Pattern）

**为什么选择这个方案：**

| 方案 | 优点 | 缺点 |
|------|------|------|
| RegisterMap 注入 ✅ | 接口清晰、易测试、易扩展、不影响 Domain 层 | 需新增一个抽象层 |
| 地址偏移量 | 改动最小 | 无法处理非连续地址映射 |
| 模板参数化 | 零运行时开销 | 编译复杂、main.cpp 需大量模板代码 |
| 双命名空间 + switch | 直观 | 每个选择器函数都要加 switch，代码膨胀严重 |

**结论**：采用 **Register Map 注入模式**。`ModbusSystemDriver` 持有一个 `IRegisterMap*` 指针，运行时注入。所有寄存器选择器委托给 Map。

### 2.2 新增/修改文件清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `infrastructure/plc/protocol/IRegisterMap.h` | **新增** | 抽象接口：给定 AxisId → 返回 RegisterInfo |
| `infrastructure/plc/protocol/RegisterMapA.h` | **新增** | A组注册映射（使用现有 RegisterAddressAll.h） |
| `infrastructure/plc/protocol/RegisterMapB.h` | **新增** | B组注册映射（使用新寄存器地址） |
| `infrastructure/plc/protocol/RegisterAddressAllB.h` | **新增** | B组 5轴寄存器地址定义 |
| `infrastructure/plc/ModbusSystemDriver.h` | **修改** | Driver 注入 IRegisterMap*, 重构所有选择器 |
| `main.cpp` | **修改** | 创建双 Registry + 双 RegisterMap, 分别注入 |
| `tests/infrastructure/` | **新增** | B组寄存器映射单元测试 |
| `docs/architecture.md` | **修改** | 更新架构文档 |

### 2.3 不影响的部分 ✅

以下模块 **无需任何修改**：

| 层级 | 模块 | 原因 |
|------|------|------|
| Domain | `Axis`, `AxisId`, `SystemContext` | 轴实体不感知寄存器地址 |
| Domain | `GantryCouplingController`, `EmergencyStopController` | 仅处理领域逻辑 |
| Domain | `SystemCommand`, `AxisCommand` | 命令体系不变 |
| Application | 所有 UseCase, Orchestrator | 用例不感知物理地址 |
| Infrastructure | `PlcPoller`, `PlcDevice`, `PlcSnapshot` | 仅操作 Registry 中的地址，不关心 Bank |
| Infrastructure | `AsioModbusTcpClient` | Modbus 传输层不变 |
| Infrastructure | `ISystemDriver` 接口 | send/pollFeedback 签名不变 |
| Presentation | QML, ViewModel | 表现层完全隔离 |

---

## 3. 详细每日工作计划

### ── Day 1: B组寄存器地址定义（约 5.5 小时）──

**目标**：为 B组 创建完整的 5轴 + 系统全局寄存器地址表。

**任务明细**：

| 时间段 | 任务 | 产出 |
|--------|------|------|
| 09:00-10:00 | 阅读 PLC 点位表文档，确认 B组各寄存器地址范围 | 地址映射表格 |
| 10:00-12:00 | 创建 `RegisterAddressAllB.h`，定义 B组所有寄存器常量 | B组寄存器头文件 |
| 13:00-14:00 | 为 B组 x_axis 命名空间编写寄存器定义（coil + holding reg） | X轴 B组寄存器 |
| 14:00-15:00 | 为 B组 y_axis/z_axis/r_axis 编写寄存器定义 | Y/Z/R 轴 B组寄存器 |
| 15:00-15:30 | 为 B组 system_global 编写寄存器定义（ESTOP, Gantry Error） | 系统全局 B组寄存器 |
| 15:30-16:30 | 编写 B组寄存器单元测试（验证地址无冲突、无重叠） | 测试代码 |

**关键产出文件**：
```
infrastructure/plc/protocol/RegisterAddressAllB.h
```
内容结构示例：
```cpp
#pragma once
#include "RegisterMetadata.h"

namespace plc::reg::b {

namespace x_axis::command {
  constexpr RegisterInfo ENABLE_REQUEST = {
    RegisterArea::Coil, 500, RegisterType::Bool, ...  // B组X轴使能地址
  };
  // ... 其他 X 轴 B组寄存器
}

namespace y_axis::command {
  constexpr RegisterInfo ENABLE_REQUEST = {
    RegisterArea::Coil, 501, RegisterType::Bool, ...  // B组Y轴使能地址
  };
  // ...
}
// ... Z, R, system_global 同理
}
```

**风险点**：
- ⚠️ 需要 PLC 人员确认 B组寄存器地址表准确无误
- ⚠️ B组地址可能与 A组地址区间重叠（需确认 PLC 内部已做隔离）

---

### ── Day 2: IRegisterMap 抽象层设计与实现（约 5 小时）──

**目标**：创建寄存器映射策略接口及 A/B 两组实现。

**任务明细**：

| 时间段 | 任务 | 产出 |
|--------|------|------|
| 09:00-10:00 | 设计 `IRegisterMap` 接口：定义所有需要的寄存器查询方法 | 接口头文件 |
| 10:00-11:30 | 实现 `RegisterMapA`：包装现有 `RegisterAddressAll.h` | A组映射实现 |
| 11:30-12:00 | 实现 `RegisterMapB`：包装 `RegisterAddressAllB.h` | B组映射实现 |
| 13:00-14:30 | 编写 IRegisterMap 单元测试（A组/B组独立测试） | 测试代码 |
| 14:30-16:00 | 编写地址隔离验证测试（确保 A/B 返回不同地址） | 隔离测试 |

**IRegisterMap 接口设计**：

```cpp
// infrastructure/plc/protocol/IRegisterMap.h
#pragma once
#include "RegisterMetadata.h"
#include "domain/entity/AxisId.h"

namespace plc::protocol {

class IRegisterMap {
public:
    virtual ~IRegisterMap() = default;

    // ── 命令寄存器 ──
    virtual const RegisterInfo& cmdEnable(AxisId id) const = 0;
    virtual const RegisterInfo& cmdJogFwd(AxisId id) const = 0;
    virtual const RegisterInfo& cmdJogBwd(AxisId id) const = 0;
    virtual const RegisterInfo& cmdAbsTarget(AxisId id) const = 0;
    virtual const RegisterInfo& cmdRelTarget(AxisId id) const = 0;
    virtual const RegisterInfo& cmdAbsTrigger(AxisId id) const = 0;
    virtual const RegisterInfo& cmdRelTrigger(AxisId id) const = 0;
    virtual const RegisterInfo& cmdSetRelZero(AxisId id) const = 0;
    virtual const RegisterInfo& cmdClearRelZero(AxisId id) const = 0;
    virtual const RegisterInfo& cmdClearAbsPos(AxisId id) const = 0;
    virtual const RegisterInfo& cmdJogSpeed(AxisId id) const = 0;
    virtual const RegisterInfo& cmdMoveSpeed(AxisId id) const = 0;

    // ── 反馈寄存器 ──
    virtual const RegisterInfo& fbAbsPos(AxisId id) const = 0;
    virtual const RegisterInfo& fbRelPos(AxisId id) const = 0;
    virtual const RegisterInfo& fbState(AxisId id) const = 0;
    virtual const RegisterInfo& fbAlarmCode(AxisId id) const = 0;
    virtual const RegisterInfo& fbAbsMoving(AxisId id) const = 0;
    virtual const RegisterInfo& fbRelMoving(AxisId id) const = 0;
    virtual const RegisterInfo& fbJogging(AxisId id) const = 0;
    virtual const RegisterInfo& fbRelZeroRecord(AxisId id) const = 0;
    virtual const RegisterInfo& fbSoftLimitPos(AxisId id) const = 0;
    virtual const RegisterInfo& fbSoftLimitNeg(AxisId id) const = 0;

    // ── 组级寄存器 ──
    virtual const RegisterInfo& gantryCoupling() const = 0;
    virtual const RegisterInfo& emergencyStopTrigger() const = 0;
    virtual const RegisterInfo& fbEmergencyStopActive() const = 0;
    virtual const RegisterInfo& fbGantryErrorCode() const = 0;
    virtual const RegisterInfo& fbLinkageState() const = 0;
};

} // namespace plc::protocol
```

**风险点**：
- ⚠️ 接口方法数量较多（~26个），需确保覆盖所有现有选择器
- ⚠️ `RegisterMapB` 中某些寄存器可能在 PLC 端暂无对应（需用占位寄存器填充或抛出异常）

---

### ── Day 3: ModbusSystemDriver 重构（约 6 小时）──

**目标**：将 Driver 的硬编码寄存器选择器改为通过 IRegisterMap 注入。

**任务明细**：

| 时间段 | 任务 | 产出 |
|--------|------|------|
| 09:00-10:00 | 在 `ModbusSystemDriver` 中添加 `setRegisterMap()` 注入方法 | Driver 接口扩展 |
| 10:00-12:00 | 重构所有命令选择器函数（~12个函数，替换为 Map 委托调用） | 选择器重构 Part 1 |
| 13:00-14:30 | 重构所有反馈选择器函数（~10个函数）+ 组级选择器（~5个） | 选择器重构 Part 2 |
| 14:30-15:30 | 重构 `pollFeedback()`：确保使用注入的 Map | pollFeedback 重构 |
| 15:30-16:30 | 重构 `send()`：确保所有写操作使用注入的 Map | send 重构 |
| 16:30-17:00 | 编译验证 + 修复编译错误 | 编译通过 |

**修改后的 Driver 关键代码变化**：

```cpp
// 修改前 (硬编码):
inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdEnable(AxisId id) const {
    switch (id) {
        case AxisId::X: return reg::x_axis::command::ENABLE_REQUEST;  // 固定 A 组
        // ...
    }
}

// 修改后 (注入 Map):
inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdEnable(AxisId id) const {
    return m_registerMap->cmdEnable(id);  // 委托给 Map, 支持 A/B 组
}
```

**新增成员和方法**：
```cpp
class ModbusSystemDriver : public ISystemDriver {
public:
    void setRegisterMap(const protocol::IRegisterMap* map) { m_registerMap = map; }
    // ...
private:
    const protocol::IRegisterMap* m_registerMap = nullptr;  // 必须注入
};
```

**风险点**：
- ⚠️ 所有选择器函数签名不变，需确保编译期引用正确
- ⚠️ `regSoftLimitPos/R` 中 R 轴使用了 X 轴的 fallback——需在 MapB 中明确 R 轴限位逻辑
- ⚠️ 现有单元测试可能因接口变更而需微调（主要是 Fake 测试）

---

### ── Day 4: main.cpp 集成与端到端配置（约 5 小时）──

**目标**：在 main.cpp 中完成完整的双 Bank 装配和端到端验证。

**任务明细**：

| 时间段 | 任务 | 产出 |
|--------|------|------|
| 09:00-10:30 | 创建独立的 `RegisterRegistry` registryB, 注册 B组全部寄存器 | 双 Registry 配置 |
| 10:30-12:00 | 配置 Driver A 使用 `RegisterMapA` + `RegistryA`, Driver B 使用 `RegisterMapB` + `RegistryB` | 双 Driver 配置 |
| 13:00-14:00 | 配置 Driver B 的 Modbus TCP 连接到 PLC B组地址（同一 IP 或独立 IP） | 网络配置 |
| 14:00-15:00 | 端到端编译 + 静态验证（检查所有寄存器地址在各自 Registry 中） | 编译通过 |
| 15:00-16:30 | 运行时验证：启动应用，验证两组 pollFeedback 正常轮询各自寄存器 | 运行验证 |

**main.cpp 关键变更示意**：

```cpp
// ── 新增：创建两个独立的 RegisterRegistry ──
plc::protocol::RegisterRegistry registryA;
{
    using namespace plc::reg;
    registryA.addAll({ system_global::command::ESTOP_TRIGGER, ... });
    // ... A组所有寄存器（与现有代码相同）
}

plc::protocol::RegisterRegistry registryB;
{
    using namespace plc::reg::b;   // ← B组命名空间
    registryB.addAll({ b::system_global::command::ESTOP_TRIGGER, ... });
    // ... B组所有寄存器
}

// ── 新增：创建两个独立的 RegisterMap ──
auto mapA = std::make_unique<plc::protocol::RegisterMapA>();
auto mapB = std::make_unique<plc::protocol::RegisterMapB>();

// ── 修改：Driver 注入 RegisterMap ──
plc::ModbusSystemDriver driverA, driverB;
driverA.setRegisterMap(mapA.get());
driverB.setRegisterMap(mapB.get());
// ... Poller 各自使用独立的 Registry
auto pollerA = std::make_unique<plc::protocol::PlcPoller>(registryA);  // ← registryA
auto pollerB = std::make_unique<plc::protocol::PlcPoller>(registryB);  // ← registryB

// ── 修改：Machine_A 和 Machine_B 现在都包含全部 6 轴 ──
manager.createGroup("Machine_A", reason);  // Y, Z, R, X, X1, X2
manager.createGroup("Machine_B", reason);  // Y, Z, R, X, X1, X2

// ── 其余代码：ViewModel 创建逻辑不变，但 Machine_A 现在也创建 X/X1/X2 VM ──
```

**风险点**：
- ⚠️ Machine_A 之前只有 Y/Z/R 轴，现在增加了 X/X1/X2 的 ViewModel
- ⚠️ 确认 `cfgB.host` 地址（同一台 PLC 的不同寄存器区间，还是不同 PLC）
- ⚠️ GantryViewModel 和 EmergencyStopViewModel 需要确认 B组是否有对应的龙门/急停逻辑

---

### ── Day 5: 测试完善、文档更新与边界情况处理（约 5 小时）──

**目标**：全面验证重构正确性，更新文档，处理边界情况。

**任务明细**：

| 时间段 | 任务 | 产出 |
|--------|------|------|
| 09:00-10:30 | 编写 B组寄存器独立功能测试（使能/点动/定位/急停） | 功能测试 |
| 10:30-11:30 | 编写双组并行测试（A/B 同时操作，验证寄存器隔离） | 隔离测试 |
| 11:30-12:00 | 验证 PlcPoller 正确打包 B组地址区间 | Poller 测试 |
| 13:00-14:00 | 验证边缘触发（EdgeTrigger）在 B组正常工作 | 协议测试 |
| 14:00-15:00 | 更新 `docs/architecture.md` 架构文档 | 文档更新 |
| 15:00-16:00 | 处理边界情况：单组离线时另一组正常工作、断线重连逻辑 | 鲁棒性验证 |
| 16:00-16:30 | 代码 Review + 提交 PR | 代码提交 |

**边界情况清单**：
- [ ] Machine_A 连接断开时，Machine_B 仍正常工作
- [ ] B组寄存器初始值为 0 时的行为
- [ ] 两组同时向同一 PLC IP 发送不同寄存器地址的写操作
- [ ] B组报警码处理逻辑
- [ ] 龙门联动/解耦在 B组的行为
- [ ] UDP 远程控制在双组下的路由

---

## 4. 时间安排总览

| 天数 | 主题 | 核心产出 | 预估工时 |
|------|------|---------|----------|
| **Day 1** | B组寄存器地址定义 | `RegisterAddressAllB.h` | 5.5h |
| **Day 2** | RegisterMap 抽象层 | `IRegisterMap.h`, `RegisterMapA/B.h` | 5h |
| **Day 3** | Driver 重构 | 修改后的 `ModbusSystemDriver.h` | 6h |
| **Day 4** | main.cpp 集成 | 端到端可运行的双 Bank 系统 | 5h |
| **Day 5** | 测试 + 文档 + 边界 | 完整测试覆盖 + 更新文档 | 5h |
| **总计** | | | **26.5 小时** |

> **注**：以上时间基于对代码库充分熟悉的前提。如有不熟悉的模块，建议预留 20% buffer（约 32 小时）。

---

## 5. 架构示意图（重构后）

```
                        ┌──────────────┐
                        │  main.cpp    │
                        │  (装配层)     │
                        └──┬───────┬───┘
                           │       │
              ┌────────────▼─┐   ┌─▼─────────────┐
              │ driverA      │   │ driverB       │
              │ (Bank A)     │   │ (Bank B)      │
              │              │   │               │
              │ RegisterMapA │   │ RegisterMapB  │
              │ RegistryA    │   │ RegistryB     │
              │ PollerA      │   │ PollerB       │
              │ DeviceA      │   │ DeviceB       │
              └──────┬───────┘   └──────┬────────┘
                     │                  │
              ┌──────▼───────┐   ┌──────▼────────┐
              │ TCP Client A │   │ TCP Client B  │
              │ 192.168.1.88 │   │ 192.168.1.88  │
              │ Bank A 寄存器│   │ Bank B 寄存器 │
              └──────────────┘   └───────────────┘
                     │                  │
                     └────────┬─────────┘
                              │
                     ┌────────▼────────┐
                     │  同一台 PLC      │
                     │  Bank A 地址区间  │
                     │  Bank B 地址区间  │
                     └─────────────────┘
```

---

## 6. 风险评估

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|----------|
| B组寄存器地址表不完整或有误 | 中 | 高 | Day 1 与 PLC 人员确认地址表，编写地址冲突检测 |
| IRegisterMap 接口遗漏方法 | 低 | 中 | Day 2 对照 Driver 选择器函数逐项核对 |
| 编译期 register 常量引用失效 | 低 | 低 | constexpr 常量生命周期静态，取地址安全 |
| Machine_A 新增 X/X1/X2 轴导致 Gantry 冲突 | 中 | 中 | Day 4 验证：Machine_A Gantry 状态是否正确初始化 |
| 双 Registry 导致 Poller 打包地址区间重叠 | 低 | 高 | Day 4 验证：打印 Poller 生成的 AddressRange |
| 现有测试因接口变更失败 | 中 | 中 | Day 3-5 逐步运行现有测试套件，修复不兼容的 Fake |

---

## 7. 前置依赖

| 依赖项 | 负责人 | 截止日期 | 状态 |
|--------|--------|----------|------|
| B组 PLC 寄存器地址点位表 | PLC 工程师 | Day 1 前 | ⬜ 待确认 |
| PLC 固件支持 B组寄存器 | PLC 工程师 | Day 4 前 | ⬜ 待确认 |
| 确认 cfgB.host IP 地址 | 项目负责人 | Day 4 前 | ⬜ 待确认 |

---

## 8. 附录：关键代码变更对比

### 8.1 RegisterAddressAll.h（A组，不变）

保持现有代码，地址从 Coil 0 开始：
```
X 轴: Coil 0~71, HoldingReg 0~172
Y 轴: Coil 1~61, HoldingReg 2~156
Z 轴: Coil 2~62, HoldingReg 4~160
R 轴: Coil 3~63, HoldingReg 6~142
全局: Coil 80, Coil 130, HoldingReg 180
```

### 8.2 RegisterAddressAllB.h（B组，新增）

预期 B组地址偏移（示例，以 PLC 实际点位表为准）：
```
B组 X 轴: Coil 500~571, HoldingReg 500~672
B组 Y 轴: Coil 501~561, HoldingReg 502~656
B组 Z 轴: Coil 502~562, HoldingReg 504~660
B组 R 轴: Coil 503~563, HoldingReg 506~642
B组 全局: Coil 580, Coil 630, HoldingReg 680
```

### 8.3 接口稳定性保证

以下接口 **不会发生变化**，上层代码无需修改：

- ✅ `ISystemDriver::send(const SystemCommand&)` 
- ✅ `ISystemDriver::pollFeedback(SystemContext&)`
- ✅ `Axis::applyFeedback(const AxisFeedback&)`
- ✅ 所有 UseCase 类
- ✅ 所有 Orchestrator 类
- ✅ 所有 ViewModel 类
- ✅ `PlcDevice::readBool/readFloat/writeBool/writeFloat`
- ✅ `PlcPoller::prepare/assemble`
- ✅ `RegisterRegistry::add/findByAddress`

---

> **文档版本**: v1.0  
> **最后更新**: 2026-06-05  
> **审核状态**: 待审核