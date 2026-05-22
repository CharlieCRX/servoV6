// infrastructure/plc/protocol/RegisterMetadata.h
#pragma once
#include <cstdint>
#include <string>
#include <optional>
#include "EndianPolicy.h"

namespace plc::protocol {

enum class RegisterArea { Coil, HoldingReg };
enum class RegisterType { Bool, Int16, Float32, String };
enum class RegisterAccess { ReadOnly, WriteOnly, ReadWrite };

// 行为语义（决定 Driver/Map 的触发与复位逻辑）
enum class RegisterBehavior {
  Level,                    // 电平触发 (持续保持)
  ManualResetEdgeTrigger,   // 手动复位边沿触发 (软件需控制 ON -> delay -> OFF)
  AutoResetEdgeTrigger,     // 自动复位边沿触发 (PLC端自动复位，软件只需发 ON)
  Continuous,               // 连续状态反馈
  Latch                     // 锁存状态 (需明确 Reset)
};


// 分组语义（决定 Batch 读取策略）
enum class RegisterGroup {
  Command,        // 下发指令
  Feedback,       // 实时状态反馈 (高频 Poll)
  Parameter,      // 设备参数 (低频/一次性读取)
  Alarm           // 报警区
};

constexpr uint16_t getWordCount(RegisterType type) {
  switch (type) {
    case RegisterType::Bool:    return 1; // Coil 占用 1 bit，但通讯长度算 1
    case RegisterType::Int16:   return 1; // 占用 1 个 16位寄存器
    case RegisterType::Float32: return 2; // 占用 2 个 16位寄存器
    default:                    return 1;
  }
}

// 核心元数据结构升级
struct RegisterInfo {
    RegisterArea area;
    uint16_t address;
    RegisterType type;
    RegisterAccess access;
    
    RegisterBehavior behavior;
    RegisterGroup group;
    
    const char* unit;        // 物理单位 ("mm", "mm/s", "")
    const char* description; // 人类可读描述 (UI/Logger极其需要)
    
    uint32_t pulseWidthMs;   // 脉冲宽度 (仅对 ManualResetEdgeTrigger 有效)

    // 【关键新增】默认为空，表示不覆盖，直接继承 Profile
    std::optional<EndianPolicy> endianOverride = std::nullopt;

    // 动态获取 wordCount，编译期计算，保证绝对安全
    constexpr uint16_t wordCount() const {
        return getWordCount(type);
    }
};

} // namespace plc::protocol