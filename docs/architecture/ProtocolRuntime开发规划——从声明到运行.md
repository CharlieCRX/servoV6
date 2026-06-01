# Protocol Runtime 开发规划 —— 从声明到运行

## 文档版本

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0 | 2026-05-25 | 初版：基于当前实现状态，制定 L2c~L2e 完整 TDD 开发路线图 |
| v2.0 | 2026-05-25 | 修正：PlcRegisterMap 接口类型安全化（使用 SystemCommand variant + std::visit）、RegisterBlock 命名精确化、五维度架构评审 |

---

## 一、当前状态回顾（已完成的基石）

### ✅ L2a Metadata 层
| 文件 | 状态 |
|------|------|
| `RegisterMetadata.h` | ✅ 完成 — 全部枚举 + `RegisterInfo` 结构体 |
| `EndianPolicy.h` | ✅ 完成 — ByteOrder × WordOrder 正交组合 |
| `ProtocolProfile.h` | ✅ 完成 — 汇川 H5U 预设 |
| `RegisterAddressY.h` | ✅ 完成 — Y 轴完整寄存器定义 |

### ✅ L2b Registry 层
| 文件 | 状态 |
|------|------|
| `RegisterRegistry.h` | ✅ 完成 — 集中注册 + 多维查询（byGroup/byArea/byAddress） |

### ✅ L2d Codec 层
| 文件 | 状态 |
|------|------|
| `RegisterCodec.h` | ✅ 完成 — 三层编解码引擎（L1原生/L2端序/L3元数据） |

### ✅ 基础 Driver 抽象
| 文件 | 状态 |
|------|------|
| `ISystemDriver.h` | ✅ 完成 — 统一接口，`send(const SystemCommand&)` |
| `FakeAxisDriver.h` | ✅ 完成 — 测试替身 |
| `FakePLC.h` | ✅ 完成 — 物理引擎仿真 |

### ✅ Domain 命令层（已完成，不在本次规划范围内，但与 P3 强相关）
| 文件 | 状态 |
|------|------|
| `domain/command/SystemCommand.h` | ✅ 完成 — `std::variant<AxisCommandWithId, GantryCouplingCommand, GantryPowerCommand, EmergencyStopCommand>` |

### 当前间隙

> **"协议世界的基础骨架"已经建立完成。但 RegisterInfo 存在后，没人验证它、没人消费它、没人规划它。现在是 Runtime 构建阶段。**

---

## 二、架构哲学：多阶段 Lowering

本项目不是"写个 PLC 控制软件"，而是在**构建工业协议 Runtime Core**。

协议世界的数据流遵循编译器式的**多阶段 lowering**：

```
Stage 1: SystemCommand          (Domain 层 — 类型安全的业务意图)
    │
    │  PlcRegisterMap::translate()   ← std::visit 分发
    ▼
Stage 2: RegisterWrite[]        (Protocol IR — 物理寄存器写操作)
    │
    │  BatchWritePlan::plan()       ← 排序 → 合并 → 分片 → 功能码选择
    ▼
Stage 3: ModbusFrame[]          (Lowered IR — Modbus 帧序列)
    │
    │  ModbusTcpDriver (P4)·
    ▼
Stage 4: TCP Packet[]           (Transport — 字节流)
```

这类似于 LLVM 的 `C++ AST → LLVM IR → Machine IR → Machine Code` 管线，或 Vulkan Driver 的 `VkCmd → IR → GPU Command Buffer` 管线。

每一层都有明确的职责边界，不跨层污染。

---

## 三、总体开发路线图

```
P0（立即执行）──── P1（紧接其后）──── P2（物理优化）──── P3（核心翻译）──── P4（最后）
     │                   │                   │                   │               │
     ▼                   ▼                   ▼                   ▼               ▼
┌─────────┐    ┌──────────────────┐    ┌─────────────┐    ┌──────────────┐   ┌───────────────┐
│Validator│    │Planning 基元      │    │BatchWrite   │    │PlcRegister   │   │Transport +    │
│+ Test   │───▶│RegisterWrite     │───▶│Plan         │───▶│Map           │──▶│Driver         │
│         │    │RegisterRead      │    │+ Test       │    │+ Test        │   │               │
│         │    │RegisterBlock     │    │ModbusFrame  │    │              │   │               │
└─────────┘    └──────────────────┘    └─────────────┘    └──────────────┘   └───────────────┘
```

**核心原则：每一步都先写测试（TDD），再写实现。**

---

## 四、P0：ProtocolConstraintValidator（立即执行）

### 为什么这个文件必须最先实现？

当前 Y 轴寄存器定义中已经存在一个**真实的地址重叠问题**：

```cpp
// feedback/MOVE_DONE   → Coil, 101
// feedback/STATE       → HoldingReg, 101
```

| 寄存器 | Area | Address | Type |
|--------|------|---------|------|
| `feedback::MOVE_DONE` | Coil | 101 | Bool |
| `feedback::STATE` | HoldingReg | 101 | Int16 |

这虽然是不同 Area（Coil vs HoldingReg），因而在 Modbus 协议上地址空间不同，**不构成真正的硬件冲突**。但系统需要明确检测并报告这类潜在问题。

更危险的是**同一 Area 内的重叠**，例如：

```cpp
// ABS_POSITION   → HoldingReg, 124, Float32 (占 124-125)
// 如果后续有人加了：SOME_VALUE → HoldingReg, 125, Int16
```

这就是**运行时灾难**——Float32 的高字被 Int16 覆盖，位置数据损坏。

**这不能在测试时才发现。必须在"协议编译阶段"（Protocol Bootstrap Phase）被拦截。**

### 4.1 ProtocolViolation.h — 违规描述 DTO

**文件路径**：`infrastructure/plc/protocol/validator/ProtocolViolation.h`

**为什么要实现**：这是 Validator 的输出类型。没有它，Validator 无法返回结构化的违规信息，上层无法判断错误的严重程度、定位问题寄存器。

**设计**：

```cpp
#pragma once
#include <string>
#include <cstdint>

namespace plc::protocol {

struct RegisterInfo; // 前向声明

struct ProtocolViolation {
    enum class Severity { Error, Warning };

    Severity severity;
    std::string ruleId;               // 规则 ID，如 "R01"
    std::string description;          // 人类可读描述
    const RegisterInfo* regA;        // 涉及的寄存器 A（主）
    const RegisterInfo* regB;        // 涉及的寄存器 B（冲突场景，可为 nullptr）
};

} // namespace plc::protocol
```

### 4.2 ProtocolConstraintValidator.h — 校验器接口

**文件路径**：`infrastructure/plc/protocol/validator/ProtocolConstraintValidator.h`

**为什么要实现**：这是"协议编译器"的入口。如同 C++ 编译器拒绝语法错误的代码，Validator 拒绝非法协议配置。它是 Runtime 安全的守门人。

**设计**：

```cpp
#pragma once
#include <vector>
#include "ProtocolViolation.h"

namespace plc::protocol {

class RegisterRegistry;

class ProtocolConstraintValidator {
public:
    /// @brief 验证整个寄存器注册表
    /// @return 违规列表（空 = 协议合法，可安全进入 Runtime）
    std::vector<ProtocolViolation> validate(const RegisterRegistry& registry);

private:
    // Rule 1: 地址重叠检查
    void checkAddressOverlap(
        const RegisterRegistry& registry,
        std::vector<ProtocolViolation>& out);

    // Rule 2: Coil 只能 Bool
    void checkCoilType(
        const RegisterRegistry& registry,
        std::vector<ProtocolViolation>& out);

    // Rule 3: Feedback 必须 ReadOnly
    void checkFeedbackAccess(
        const RegisterRegistry& registry,
        std::vector<ProtocolViolation>& out);

    // Rule 4: ManualResetEdgeTrigger 必须 pulseWidth > 0
    void checkPulseWidth(
        const RegisterRegistry& registry,
        std::vector<ProtocolViolation>& out);
};

} // namespace plc::protocol
```

**四条规则的精确定义**：

| 规则 ID | 检查项 | 严重性 | 触发条件 |
|---------|--------|--------|---------|
| R01 | 地址重叠 | Error | 同一 Area 内，regA.address ∈ [regB.address, regB.address + regB.wordCount() - 1]，且 regA ≠ regB |
| R02 | Coil 类型限制 | Error | `area == Coil` 且 `type != Bool` |
| R03 | Feedback 权限 | Error | `group == Feedback` 且 `access != ReadOnly` |
| R04 | 脉冲宽度缺失 | Error | `behavior == ManualResetEdgeTrigger` 且 `pulseWidthMs == 0` |

**Rule 1 为什么关键**：这是 Validator 的核心算法。Float32 占 2 个寄存器，需要检查其占用的全部地址范围。不仅检查精确匹配（两个寄存器都声明 address=124），还要检查范围重叠（一个声明 124 Float32，另一个声明 125 Int16）。

### 4.3 ProtocolConstraintValidator.cpp — 校验器实现

**文件路径**：`infrastructure/plc/protocol/validator/ProtocolConstraintValidator.cpp`

**实现要点**：

```cpp
#include "ProtocolConstraintValidator.h"
#include "infrastructure/plc/protocol/RegisterRegistry.h"
#include "infrastructure/plc/protocol/RegisterMetadata.h"
#include <algorithm>

namespace plc::protocol {

std::vector<ProtocolViolation> ProtocolConstraintValidator::validate(
    const RegisterRegistry& registry)
{
    std::vector<ProtocolViolation> violations;

    checkAddressOverlap(registry, violations);   // R01
    checkCoilType(registry, violations);          // R02
    checkFeedbackAccess(registry, violations);    // R03
    checkPulseWidth(registry, violations);        // R04

    return violations;
}

void ProtocolConstraintValidator::checkAddressOverlap(
    const RegisterRegistry& registry,
    std::vector<ProtocolViolation>& out)
{
    const auto& all = registry.all();
    for (size_t i = 0; i < all.size(); ++i) {
        for (size_t j = i + 1; j < all.size(); ++j) {
            const auto& a = all[i];
            const auto& b = all[j];

            // 只有同一 Area 才可能重叠
            if (a.area != b.area) continue;

            // a 占用的地址范围：[a.address, a.address + a.wordCount() - 1]
            // b 占用的地址范围：[b.address, b.address + b.wordCount() - 1]
            uint16_t aEnd = a.address + a.wordCount() - 1;
            uint16_t bEnd = b.address + b.wordCount() - 1;

            // 区间重叠判断
            bool overlaps = (a.address <= bEnd) && (b.address <= aEnd);

            if (overlaps) {
                out.push_back({
                    ProtocolViolation::Severity::Error,
                    "R01",
                    "Address overlap: [" + std::string(a.description) +
                    "] occupies [" + std::to_string(a.address) + "-" +
                    std::to_string(aEnd) + "], conflicts with [" +
                    std::string(b.description) + "] at [" +
                    std::to_string(b.address) + "-" +
                    std::to_string(bEnd) + "]",
                    &a,
                    &b
                });
            }
        }
    }
}

void ProtocolConstraintValidator::checkCoilType(
    const RegisterRegistry& registry,
    std::vector<ProtocolViolation>& out)
{
    for (const auto& reg : registry.all()) {
        if (reg.area == RegisterArea::Coil && reg.type != RegisterType::Bool) {
            out.push_back({
                ProtocolViolation::Severity::Error,
                "R02",
                "Coil register must be Bool type: [" +
                std::string(reg.description) + "] at address " +
                std::to_string(reg.address),
                &reg,
                nullptr
            });
        }
    }
}

void ProtocolConstraintValidator::checkFeedbackAccess(
    const RegisterRegistry& registry,
    std::vector<ProtocolViolation>& out)
{
    for (const auto& reg : registry.all()) {
        if (reg.group == RegisterGroup::Feedback &&
            reg.access != RegisterAccess::ReadOnly) {
            out.push_back({
                ProtocolViolation::Severity::Error,
                "R03",
                "Feedback group must be ReadOnly: [" +
                std::string(reg.description) + "] at address " +
                std::to_string(reg.address),
                &reg,
                nullptr
            });
        }
    }
}

void ProtocolConstraintValidator::checkPulseWidth(
    const RegisterRegistry& registry,
    std::vector<ProtocolViolation>& out)
{
    for (const auto& reg : registry.all()) {
        if (reg.behavior == RegisterBehavior::ManualResetEdgeTrigger &&
            reg.pulseWidthMs == 0) {
            out.push_back({
                ProtocolViolation::Severity::Error,
                "R04",
                "ManualResetEdgeTrigger requires pulseWidthMs > 0: [" +
                std::string(reg.description) + "] at address " +
                std::to_string(reg.address),
                &reg,
                nullptr
            });
        }
    }
}

} // namespace plc::protocol
```

### 4.4 test_protocol_validator.cpp — TDD 测试

**文件路径**：`tests/infrastructure/protocol/test_protocol_validator.cpp`

**为什么要先写测试**：TDD 的核心原则——先定义期望行为，再实现代码。测试即规格。

**测试用例设计**：

```
Test Suite: ProtocolConstraintValidator

  Test: validate_empty_registry_returns_no_violations
    场景: 空 Registry
    期望: 返回空违规列表

  Test: detect_address_overlap_same_area_float32_int16
    场景: HoldingReg 124 Float32 + HoldingReg 125 Int16   ← 当前 Y 轴存在类似风险
    期望: 1 个 R01 Error

  Test: no_overlap_different_areas
    场景: Coil 101 + HoldingReg 101   ← 当前 Y 轴真实场景
    期望: 0 个违规（不同 Area 地址空间独立）

  Test: detect_coil_with_non_bool_type
    场景: Coil + Float32
    期望: 1 个 R02 Error

  Test: coil_with_bool_is_valid
    场景: Coil + Bool
    期望: 0 个违规

  Test: detect_feedback_not_readonly
    场景: Feedback 组 + ReadWrite 权限
    期望: 1 个 R03 Error

  Test: feedback_readonly_is_valid
    场景: Feedback 组 + ReadOnly 权限
    期望: 0 个违规

  Test: detect_edge_trigger_without_pulse_width
    场景: ManualResetEdgeTrigger + pulseWidthMs=0
    期望: 1 个 R04 Error

  Test: edge_trigger_with_pulse_width_is_valid
    场景: ManualResetEdgeTrigger + pulseWidthMs=50
    期望: 0 个违规

  Test: multiple_violations_reported_together
    场景: 同时触发 R02 + R03 + R04
    期望: 3 个违规全部报告

  Test: no_overlap_same_area_non_overlapping
    场景: HoldingReg 100 Int16 + HoldingReg 102 Int16（中间隔了 101）
    期望: 0 个违规

  Test: detect_overlap_float32_spanning_two_registers
    场景: HoldingReg 124 Float32 (占124-125) + HoldingReg 125 Int16
    期望: 1 个 R01 Error
```

---

## 五、P1：Planning 基元（紧接其后）

P1 阶段建立**协议 IR（中间表示）**。DTO 先于算法——否则 BatchWritePlan 会变成"边写边定义结构"，最终架构崩掉。

### 5.1 RegisterWrite.h — 单次写操作 DTO

**文件路径**：`infrastructure/plc/protocol/RegisterWrite.h`

**为什么要实现**：
这是"业务语义"第一次降维成"物理寄存器写入"。例如 `MoveAbsolute(Y, 10.25)` 最终变成 `D24 → [0x0000, 0x4124]`。
它是 PlcRegisterMap 的**输出类型**（Lowering Stage 1→2），也是 BatchWritePlan 的**输入类型**（Lowering Stage 2→3）。没有它，上下两层无法对接。

**设计**：

```cpp
#pragma once
#include <cstdint>
#include <vector>
#include "RegisterMetadata.h"

namespace plc::protocol {

/// Stage 2 IR: 单次物理寄存器写操作
struct RegisterWrite {
    RegisterArea area;              // Coil 或 HoldingReg
    uint16_t address;               // 起始地址
    std::vector<uint16_t> values;   // 要写入的寄存器值（已编码）
    const RegisterInfo* source;     // 指向元数据（调试/日志/校验用）

    /// 便捷：是否是 Coil 写入
    bool isCoil() const { return area == RegisterArea::Coil; }

    /// 便捷：写入占用的寄存器数量
    uint16_t registerCount() const { return static_cast<uint16_t>(values.size()); }
};

} // namespace plc::protocol
```

**为什么 `source` 是裸指针**：`RegisterInfo` 是 `constexpr` 全局常量，生命周期是永久的，不需要智能指针。

### 5.2 RegisterRead.h — 单次读操作 DTO

**文件路径**：`infrastructure/plc/protocol/RegisterRead.h`

**为什么要实现**：写有 RegisterWrite，读同样需要 RegisterRead。它是构建批量读计划（Feedback Polling）的基础单元。最终落地路径：RegisterRead → RegisterBlock → Modbus 读帧。

**设计**：

```cpp
#pragma once
#include <cstdint>
#include "RegisterMetadata.h"

namespace plc::protocol {

/// Stage 2 IR: 单次物理寄存器读操作
struct RegisterRead {
    RegisterArea area;              // Coil 或 HoldingReg
    uint16_t address;               // 起始地址
    uint16_t count;                 // 要读取的寄存器数量
    const RegisterInfo* source;     // 指向元数据
};

} // namespace plc::protocol
```

### 5.3 RegisterBlock.h — 连续寄存器优化块

**文件路径**：`infrastructure/plc/protocol/RegisterBlock.h`

**为什么要实现**：Feedback Polling 不是逐寄存器读取，而是**连续块批量读**。例如 D100~D140 一次读完。RegisterBlock 将相邻的 RegisterRead 合并成更大的连续区间，减少 Modbus 通讯次数。这是性能优化的关键组件。

**设计**：

```cpp
#pragma once
#include <cstdint>
#include <vector>
#include "RegisterMetadata.h"

namespace plc::protocol {

/// 连续寄存器块 — Feedback Polling 的优化单元
struct RegisterBlock {
    RegisterArea area;                              // Coil 或 HoldingReg
    uint16_t startAddress;                          // 块起始地址
    uint16_t totalRegisterCount;                    // 块占用的寄存器总数
    std::vector<const RegisterInfo*> registers;     // 块内包含的寄存器元数据列表

    /// 是否是有效的块
    bool valid() const { return totalRegisterCount > 0 && !registers.empty(); }
};

} // namespace plc::protocol
```

**命名说明**：使用 `totalRegisterCount` 而非 `registerCount`，避免歧义——明确表达"这是整个 block 的寄存器计数"而非单个寄存器的 count。这是评审中提出的改进点。

### 5.4 ModbusFrame.h — Modbus 帧 DTO

**文件路径**：`infrastructure/plc/protocol/ModbusFrame.h`

**为什么要实现**：BatchWritePlan 的输出必须是一个明确的数据结构，而不是"神秘计划对象"。ModbusFrame 是规划结果的标准载体，直接映射到 Modbus 协议的帧格式。它是 Stage 3 Lowered IR。

**设计**：

```cpp
#pragma once
#include <cstdint>
#include <vector>
#include "RegisterMetadata.h"

namespace plc::protocol {

/// Modbus 功能码（协议规划层职责）
enum class ModbusFunctionCode : uint8_t {
    ReadCoils              = 0x01,
    ReadHoldingRegisters   = 0x03,
    WriteSingleCoil        = 0x05,
    WriteSingleRegister    = 0x06,
    WriteMultipleCoils     = 0x0F,
    WriteMultipleRegisters = 0x10
};

/// Stage 3 IR: Modbus 帧 DTO
struct ModbusFrame {
    ModbusFunctionCode function;       // 功能码
    RegisterArea area;                 // 寄存器区域
    uint16_t startAddress;             // 起始地址
    std::vector<uint16_t> payload;     // 数据载荷（读取时为空，写入时为值列表）

    /// 便捷：是否是读帧
    bool isRead() const {
        return function == ModbusFunctionCode::ReadCoils ||
               function == ModbusFunctionCode::ReadHoldingRegisters;
    }

    /// 便捷：是否是写帧
    bool isWrite() const {
        return function == ModbusFunctionCode::WriteSingleCoil ||
               function == ModbusFunctionCode::WriteSingleRegister ||
               function == ModbusFunctionCode::WriteMultipleCoils ||
               function == ModbusFunctionCode::WriteMultipleRegisters;
    }
};

} // namespace plc::protocol
```

**为什么把 FunctionCode 放在 protocol 层而非 transport 层**：功能码的选择（FC05 vs FC0F，FC06 vs FC10）由 BatchWritePlan 根据寄存器数量和连续性决定，这是**协议规划层的职责**，不是传输层的职责。

**关于 Coil payload 的说明**（评审建议）：当前 `std::vector<uint16_t>` 对线圈 bit-packed 场景不够精确（FC0F 实际需要 bit-packed）。但**当前阶段不做修改**。未来 Stage 4（Transport）需要时，可改为 `std::variant<std::vector<uint16_t>, std::vector<bool>>` 或 raw bytes。当前 HoldingReg 主导场景下 `vector<uint16_t>` 完全够用。

---

## 六、P2：BatchWritePlan（物理优化）

### 6.1 BatchWritePlan.h — 批量写优化引擎

**文件路径**：`infrastructure/plc/protocol/BatchWritePlan.h`

**为什么要实现**：PlcRegisterMap 产出的 RegisterWrite[] 是"逻辑视图"——每个业务命令独立产生写操作，不考虑相邻寄存器可以合并。BatchWritePlan 负责将这些写操作优化为最优的 Modbus 帧序列（Lowering Stage 2→3）：

- **排序**：按 (Area, Address) 排序，保证连续的物理布局
- **合并**：D24、D25、D26 → 一次 FC16 写入 [D24: 3 registers]
- **分片**：超过最大寄存器数的超长写入 → 拆成多帧
- **FC06 vs FC16 选择**：单寄存器用 FC06，多寄存器用 FC16
- **FC05 vs FC0F 选择**：单线圈用 FC05，多线圈用 FC0F

**设计**：

```cpp
#pragma once
#include <vector>
#include "RegisterWrite.h"
#include "ModbusFrame.h"
#include "ProtocolProfile.h"

namespace plc::protocol {

class BatchWritePlan {
public:
    explicit BatchWritePlan(const ProtocolProfile& profile);

    /// @brief 将逻辑写操作列表优化为最优 Modbus 帧序列
    /// @param writes 逻辑写操作列表（来自 PlcRegisterMap）
    /// @return 优化后的 Modbus 帧序列
    std::vector<ModbusFrame> plan(std::vector<RegisterWrite> writes);

private:
    const ProtocolProfile& m_profile;

    // Phase 1: 按 (Area, Address) 排序，保证连续物理布局
    void normalize(std::vector<RegisterWrite>& writes);

    // Phase 2: 合并相邻写入（同一 Area 内地址连续的寄存器）
    std::vector<RegisterWrite> mergeAdjacent(const std::vector<RegisterWrite>& writes);

    // Phase 3: 按最大寄存器数分片为 ModbusFrame
    std::vector<ModbusFrame> splitToFrames(const std::vector<RegisterWrite>& merged);

    // 选择最优功能码
    ModbusFunctionCode selectFunctionCode(
        RegisterArea area, uint16_t registerCount) const;
};

} // namespace plc::protocol
```

**为什么增加了 `normalize()` 排序阶段**（评审建议）：RegisterWrite[] 可能来源于多个命令的混合输出，顺序未必连续。排序能最大化合并收益。当前 P2 阶段可直接实现排序+合并，分片可暂用占位逻辑。

### 6.2 test_batch_write_plan.cpp — TDD 测试

**文件路径**：`tests/infrastructure/protocol/test_batch_write_plan.cpp`

**测试用例设计**：

```
Test Suite: BatchWritePlan

  Test: single_register_write_produces_fc06
    场景: 单个 HoldingReg 写入
    期望: 1 个 ModbusFrame，function=FC06

  Test: multiple_adjacent_writes_merge_to_fc16
    场景: D24 + D25 + D26 三个相邻写入
    期望: 1 个 ModbusFrame，function=FC16，payload 包含 3 个值

  Test: non_adjacent_writes_stay_separate
    场景: D24 + D100（不连续）
    期望: 2 个 ModbusFrame

  Test: out_of_order_writes_sort_before_merge
    场景: D26 + D24 + D25（乱序输入）
    期望: 1 个 ModbusFrame（排序后合并），startAddress=24

  Test: overflow_split_to_multiple_frames
    场景: 200 个连续寄存器写入（>120 限制）
    期望: 2 个 ModbusFrame（120 + 80）

  Test: single_coil_write_produces_fc05
    场景: 单个 Coil 写入
    期望: 1 个 ModbusFrame，function=FC05

  Test: multiple_coils_merge_to_fc0f
    场景: Coil 1 + Coil 2 + Coil 3
    期望: 1 个 ModbusFrame，function=FC0F

  Test: mixed_area_writes_not_merged
    场景: Coil 1 + HoldingReg 24
    期望: 2 个 ModbusFrame（Area 不同不能合并）
```

---

## 七、P3：PlcRegisterMap（核心翻译）

### 7.1 PlcRegisterMap.h — 业务命令翻译器

**文件路径**：`infrastructure/plc/protocol/PlcRegisterMap.h`

**为什么要实现**：这是"业务语义 → 物理寄存器"的翻译层（Lowering Stage 1→2）。例如 `AxisCommandWithId{Y, Enable}` → `RegisterWrite{Coil, 1, [0xFF00]}`。

**为什么 PlcRegisterMap 反而靠后**：它依赖 RegisterWrite（P1）、BatchWritePlan（P2）、RegisterRegistry（已有）、RegisterCodec（已有）。它处于依赖链的最末端。但它的核心地位不容置疑——它是业务世界和协议世界之间的桥梁。

**⚠️ 关键设计决策：类型安全（评审强烈建议）**

~~错误做法~~：

```cpp
// ❌ 架构退化：协议层重新字符串化
std::vector<RegisterWrite> translateWrite(
    const std::string& commandType,
    const std::vector<float>& params);
```

**正确做法**：

```cpp
// ✅ 类型安全：直接消费 Domain 层的 SystemCommand variant
std::vector<RegisterWrite> translate(const SystemCommand& cmd);
```

**为什么不能降级为 string + vector<float>**：

1. Domain 层已经通过 `SystemCommand = std::variant<...>` 建立了类型安全的命令体系
2. `ISystemDriver::send(const SystemCommand&)` 已经使用此类型
3. 降级为字符串意味着重新发明类型系统——这是架构退化
4. 字符串匹配容易出错（拼写错误、版本不同步）

**设计**：

```cpp
#pragma once
#include <vector>
#include "RegisterWrite.h"
#include "RegisterRead.h"
#include "RegisterMetadata.h"
#include "domain/command/SystemCommand.h"

namespace plc::protocol {

class RegisterRegistry;
class RegisterCodec;

class PlcRegisterMap {
public:
    PlcRegisterMap(const RegisterRegistry& registry,
                   const RegisterCodec& codec);

    /// @brief 将类型安全的业务命令翻译为寄存器写操作列表
    ///        Lowering Stage 1 → Stage 2
    /// @param cmd Domain 层 SystemCommand variant
    /// @return 寄存器写操作列表
    std::vector<RegisterWrite> translate(const SystemCommand& cmd);

    /// @brief 按分组获取寄存器读操作列表（用于 Feedback Polling）
    /// @param group 寄存器分组
    /// @return 该分组所有寄存器的读操作列表
    std::vector<RegisterRead> buildReadPlan(RegisterGroup group) const;

private:
    const RegisterRegistry& m_registry;
    const RegisterCodec& m_codec;

    // ——— 内部翻译函数（通过 std::visit 分发）———

    std::vector<RegisterWrite> translateAxisCommand(
        const AxisCommandWithId& cmd);

    std::vector<RegisterWrite> translateGantryCoupling(
        const GantryCouplingCommand& cmd);

    std::vector<RegisterWrite> translateGantryPower(
        const GantryPowerCommand& cmd);

    std::vector<RegisterWrite> translateEmergencyStop(
        const EmergencyStopCommand& cmd);

    // ——— 内部查找辅助 ———

    const RegisterInfo* findCommandRegister(const std::string& name) const;
};

} // namespace plc::protocol
```

**实现核心（std::visit 分发）**：

```cpp
std::vector<RegisterWrite> PlcRegisterMap::translate(const SystemCommand& cmd) {
    return std::visit(
        [this](const auto& concrete) -> std::vector<RegisterWrite> {
            using T = std::decay_t<decltype(concrete)>;

            if constexpr (std::is_same_v<T, AxisCommandWithId>) {
                return translateAxisCommand(concrete);
            } else if constexpr (std::is_same_v<T, GantryCouplingCommand>) {
                return translateGantryCoupling(concrete);
            } else if constexpr (std::is_same_v<T, GantryPowerCommand>) {
                return translateGantryPower(concrete);
            } else if constexpr (std::is_same_v<T, EmergencyStopCommand>) {
                return translateEmergencyStop(concrete);
            }
            // static_assert 在 variant 完备时不需要 else 分支
        },
        cmd);
}
```

### 7.2 test_plc_register_map.cpp — TDD 测试

**文件路径**：`tests/infrastructure/protocol/test_plc_register_map.cpp`

**测试用例设计**：

```
Test Suite: PlcRegisterMap

  Test: translate_enable_command_produces_coil_write
    场景: SystemCommand(AxisCommandWithId{Y, Enable})
    期望: 1 个 RegisterWrite，area=Coil，address=1

  Test: translate_move_absolute_produces_holdingreg_write
    场景: SystemCommand(AxisCommandWithId{Y, MoveAbsolute{10.25}})
    期望: 1 个 RegisterWrite，area=HoldingReg，address=24

  Test: build_read_plan_for_feedback_group
    场景: buildReadPlan(Feedback)
    期望: 返回所有 Feedback 组的 RegisterRead 列表

  Test: build_read_plan_for_empty_group
    场景: 空 Registry，buildReadPlan(Command)
    期望: 返回空列表

  Test: translate_emergency_stop_produces_correct_register
    场景: SystemCommand(EmergencyStopCommand{true})
    期望: 1 个 RegisterWrite 写入正确的急停寄存器
```

---

## 八、P4：Transport + Driver（最后）

P4 阶段的内容——IModbusClient、FakeModbusClient、LibModbusClient、ModbusTcpDriver——**绝对不在当前阶段实现**。原因：

1. 它们依赖 PlcRegisterMap + BatchWritePlan 的完整就绪
2. 它们涉及真实的 Socket 编程，需要运行时环境
3. 在 Protocol Runtime 世界没有完全建立之前，跳过它们直接写 Driver 会导致大量的返工

P4 将在 P0~P3 全部完成后，作为独立的子项目推进。

---

## 九、完整 Lowering Pipeline 总览

当 P0~P3 全部完成，完整的命令下发链路为：

```
SystemManager::execute(cmd)
    │
    │  SystemCommand (variant)
    ▼
ISystemDriver::send(cmd)
    │
    │  (Domain → Protocol 分界)
    ▼
PlcRegisterMap::translate(cmd)        ← P3: Stage 1 → Stage 2
    │
    │  std::vector<RegisterWrite>
    ▼
BatchWritePlan::plan(writes)          ← P2: Stage 2 → Stage 3
    │
    │  std::vector<ModbusFrame>
    ▼
ModbusTcpDriver::executeFrames(...)   ← P4: Stage 3 → Stage 4
    │
    │  TCP Packets
    ▼
PLC Hardware
```

反馈轮询链路：

```
SystemManager::tick()
    │
    ▼
ISystemDriver::pollFeedback(ctx)
    │
    ▼
PlcRegisterMap::buildReadPlan(Feedback)   ← P3: 确定要读哪些寄存器
    │
    │  std::vector<RegisterRead>
    ▼
RegisterBlock::merge(readPlan)            ← P1: 合并相邻读取
    │
    │  std::vector<RegisterBlock>
    ▼
ModbusTcpDriver::readBlocks(blocks)       ← P4: 批量读帧 → 解包 → 分发
    │
    ▼
SystemContext::applyFeedback(...)
```

---

## 十、测试策略总结

| 阶段 | 测试文件 | 测试类型 | 测试数量 |
|------|---------|---------|---------|
| P0 | `test_protocol_validator.cpp` | 单元测试 | ~12 个 case |
| P1 | 无独立测试 | DTO 编译验证 | — |
| P2 | `test_batch_write_plan.cpp` | 单元测试 | ~8 个 case |
| P3 | `test_plc_register_map.cpp` | 单元测试 | ~5 个 case |

---

## 十一、文件清单汇总

按照开发顺序，需要新增的文件完整列表：

| 序号 | 文件 | 阶段 | 类型 |
|------|------|------|------|
| 1 | `tests/infrastructure/protocol/test_protocol_validator.cpp` | P0 | 测试 |
| 2 | `infrastructure/plc/protocol/validator/ProtocolViolation.h` | P0 | 头文件 |
| 3 | `infrastructure/plc/protocol/validator/ProtocolConstraintValidator.h` | P0 | 头文件 |
| 4 | `infrastructure/plc/protocol/validator/ProtocolConstraintValidator.cpp` | P0 | 实现 |
| 5 | `infrastructure/plc/protocol/RegisterWrite.h` | P1 | 头文件 |
| 6 | `infrastructure/plc/protocol/RegisterRead.h` | P1 | 头文件 |
| 7 | `infrastructure/plc/protocol/RegisterBlock.h` | P1 | 头文件 |
| 8 | `infrastructure/plc/protocol/ModbusFrame.h` | P1 | 头文件 |
| 9 | `tests/infrastructure/protocol/test_batch_write_plan.cpp` | P2 | 测试 |
| 10 | `infrastructure/plc/protocol/BatchWritePlan.h` | P2 | 头文件 |
| 11 | `infrastructure/plc/protocol/BatchWritePlan.cpp` | P2 | 实现 |
| 12 | `tests/infrastructure/protocol/test_plc_register_map.cpp` | P3 | 测试 |
| 13 | `infrastructure/plc/protocol/PlcRegisterMap.h` | P3 | 头文件 |
| 14 | `infrastructure/plc/protocol/PlcRegisterMap.cpp` | P3 | 实现 |

**共 14 个文件，其中 4 个测试文件，10 个源文件。**

---

## 十二、关键注意事项

### 12.1 关于 CMakeLists.txt

每新增一个 `.cpp` 文件（非 header-only），都需要在对应的 `CMakeLists.txt` 中添加：

- `infrastructure/CMakeLists.txt`：添加 L2c/L2e 的源文件
- `tests/CMakeLists.txt`：添加测试文件

### 12.2 关于命名空间

所有新文件统一使用 `namespace plc::protocol { ... }`

### 12.3 关于 include 路径

- 头文件使用项目根目录的相对路径，例如 `#include "infrastructure/plc/protocol/RegisterMetadata.h"`
- 测试文件需额外 `#include <gtest/gtest.h>`

### 12.4 关于 TDD 执行纪律

**必须严格遵循：先写测试 → 编译失败 → 写实现 → 测试通过。**
不允许先写实现再补测试。

### 12.5 P4 的禁区

在 P0~P3 全部完成并通过测试之前，**绝对不要**：

- 创建 `IModbusClient.h`
- 创建 `FakeModbusClient.h`
- 创建 `LibModbusClient.h`
- 创建 `ModbusTcpDriver.h`
- 引入任何 Socket / Network 库

这些组件依赖 Protocol Runtime 世界的完整建立。

---

## 十三、五维度架构评审总结

| 维度 | 评价 | 说明 |
|------|------|------|
| 架构正确性 | ✅ 非常正确 | 多阶段 Lowering 管线（SystemCommand → RegisterWrite → ModbusFrame → TCP Packet）符合工业 Runtime 标准模式 |
| 依赖顺序 | ✅ 正确 | Validator → DTO → Plan → Map → Transport，不跳步、不跨层 |
| Runtime 完整性 | ✅ 成熟 | 覆盖校验、规划、翻译、传输全链路；声明→编译→lowering→planning→transport 完整闭环 |
| TDD 合理性 | ✅ 合理 | 核心层（Validator/Plan/Map）有独立测试，DTO 层编译验证即可 |
| 潜在问题 | ⚠️ 已修正 | v1→v2 修正：PlcRegisterMap 使用 SystemCommand variant + std::visit（不再退化为 string+float）；RegisterBlock 更名为 totalRegisterCount；BatchWritePlan 增加 normalize 排序阶段 |

---

> **规划的核心精神**：把协议世界从"声明"推进到"运行"。
> - **ProtocolConstraintValidator** 是守门人（编译阶段）
> - **Planning 基元** 是基础设施（协议 IR）
> - **BatchWritePlan** 是优化引擎（Lowering Stage 2→3）
> - **PlcRegisterMap** 是业务桥梁（Lowering Stage 1→2，类型安全）
>
> 每一步都有明确的"为什么"和 TDD 验证。
