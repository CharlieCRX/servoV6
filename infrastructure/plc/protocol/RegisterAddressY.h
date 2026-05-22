#pragma once
#include "RegisterMetadata.h"

namespace plc::reg::y_axis {
using namespace plc::protocol;

namespace command {
  // 只是 REQUEST，而不是真正的状态
  constexpr RegisterInfo ENABLE_REQUEST = { 
    RegisterArea::Coil, 1, RegisterType::Bool, RegisterAccess::ReadWrite, 
    RegisterBehavior::Level, RegisterGroup::Command, 
    "", "使能轴Y电机请求", 0 
  };
  
  // 赋予明确的 pulseWidthMs (假设该 PLC 要求至少保持 50ms 脉冲)
  constexpr RegisterInfo ABS_MOVE_TRIGGER = { 
    RegisterArea::Coil, 42, RegisterType::Bool, RegisterAccess::ReadWrite, 
    RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, 
    "", "绝对定位触发脉冲", 50 
  };

  constexpr RegisterInfo ABS_TARGET = { 
    RegisterArea::HoldingReg, 24, RegisterType::Float32, RegisterAccess::ReadWrite, 
    RegisterBehavior::Level, RegisterGroup::Command, 
    "mm", "绝对定位目标距离", 0 
  };
  
  constexpr RegisterInfo MOVE_SPEED = { 
    RegisterArea::HoldingReg, 1006, RegisterType::Float32, RegisterAccess::ReadWrite, 
    RegisterBehavior::Level, RegisterGroup::Command, 
    "mm/s", "绝对/相对定位速度", 0 
  };
}

namespace feedback {
  // 明确引入物理设备的真实反馈状态
  constexpr RegisterInfo STATE = { 
    RegisterArea::HoldingReg, 101, RegisterType::Int16, RegisterAccess::ReadOnly, 
    RegisterBehavior::Continuous, RegisterGroup::Feedback, 
    "", "轴Y当前运行状态", 0 
  };

  constexpr RegisterInfo ABS_POSITION = { 
    RegisterArea::HoldingReg, 124, RegisterType::Float32, RegisterAccess::ReadOnly, 
    RegisterBehavior::Continuous, RegisterGroup::Feedback, 
    "mm", "轴Y实时绝对位置", 0 
  };
  
  constexpr RegisterInfo ALARM_CODE = { 
    RegisterArea::HoldingReg, 111, RegisterType::Int16, RegisterAccess::ReadOnly, 
    RegisterBehavior::Latch, RegisterGroup::Alarm, 
    "", "轴Y故障报警代码", 0 
  };
}

} // namespace plc::reg::y_axis