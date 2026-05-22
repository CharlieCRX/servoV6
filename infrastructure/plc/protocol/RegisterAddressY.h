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

  // 暂不开放的回零功能（注释保留，作为防呆参考）
  /*
  constexpr RegisterInfo HOME_TRIGGER = { 
    RegisterArea::Coil, 11, RegisterType::Bool, RegisterAccess::ReadWrite, 
    RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, 
    "", "轴Y回原点", 50 
  };
  */

  constexpr RegisterInfo SET_REL_ZERO = { 
    RegisterArea::Coil, 15, RegisterType::Bool, RegisterAccess::ReadWrite, 
    RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, 
    "", "轴Y相对原点设置", 50 
  };

  constexpr RegisterInfo CLEAR_REL_ZERO = { 
    RegisterArea::Coil, 19, RegisterType::Bool, RegisterAccess::ReadWrite, 
    RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, 
    "", "轴Y相对原点清除", 50 
  };

  constexpr RegisterInfo CLEAR_ABS_POS = { 
    RegisterArea::Coil, 31, RegisterType::Bool, RegisterAccess::ReadWrite, 
    RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, 
    "", "轴Y绝对位置清零", 50 
  };
  
  // 赋予明确的 pulseWidthMs (假设该 PLC 要求至少保持 50ms 脉冲)
  constexpr RegisterInfo ABS_MOVE_TRIGGER = { 
    RegisterArea::Coil, 42, RegisterType::Bool, RegisterAccess::ReadWrite, 
    RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, 
    "", "绝对定位触发脉冲", 50 
  };

  constexpr RegisterInfo REL_MOVE_TRIGGER = { 
    RegisterArea::Coil, 43, RegisterType::Bool, RegisterAccess::ReadWrite, 
    RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, 
    "", "相对定位触发脉冲", 50 
  };

  constexpr RegisterInfo JOG_FORWARD = { 
    RegisterArea::Coil, 54, RegisterType::Bool, RegisterAccess::ReadWrite, 
    RegisterBehavior::Level, RegisterGroup::Command, 
    "", "轴Y点动正转", 0 
  };

  constexpr RegisterInfo JOG_BACKWARD = { 
    RegisterArea::Coil, 55, RegisterType::Bool, RegisterAccess::ReadWrite, 
    RegisterBehavior::Level, RegisterGroup::Command, 
    "", "轴Y点动反转", 0 
  };

  constexpr RegisterInfo ALARM_RESET = { 
    RegisterArea::Coil, 61, RegisterType::Bool, RegisterAccess::ReadWrite, 
    RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, 
    "", "轴Y报警解除触发", 50 
  };

  // ==========================================
  // 保持寄存器区 (HoldingRegs - 目标与参数)
  // ==========================================
  constexpr RegisterInfo ABS_TARGET = { 
    RegisterArea::HoldingReg, 24, RegisterType::Float32, RegisterAccess::ReadWrite, 
    RegisterBehavior::Level, RegisterGroup::Command, 
    "mm", "绝对定位目标距离", 0 
  };

  constexpr RegisterInfo REL_TARGET = { 
    RegisterArea::HoldingReg, 26, RegisterType::Float32, RegisterAccess::ReadWrite, 
    RegisterBehavior::Level, RegisterGroup::Command, 
    "mm", "相对定位目标距离", 0 
  };

  constexpr RegisterInfo JOG_SPEED = { 
    RegisterArea::HoldingReg, 1004, RegisterType::Float32, RegisterAccess::ReadWrite, 
    RegisterBehavior::Level, RegisterGroup::Command, 
    "mm/s", "轴Y手动(点动)速度", 0 
  };

  constexpr RegisterInfo MOVE_SPEED = { 
    RegisterArea::HoldingReg, 1006, RegisterType::Float32, RegisterAccess::ReadWrite, 
    RegisterBehavior::Level, RegisterGroup::Command, 
    "mm/s", "轴Y绝对/相对定位速度", 0 
  };
}

namespace feedback {
  // ==========================================
  // 线圈反馈区 (Coils - 实时布尔状态)
  // ==========================================
  constexpr RegisterInfo MOVE_DONE = { 
    RegisterArea::Coil, 101, RegisterType::Bool, RegisterAccess::ReadOnly, 
    RegisterBehavior::Continuous, RegisterGroup::Feedback, 
    "", "轴Y运动完成", 0 
  };

  constexpr RegisterInfo ABS_MOVING = { 
    RegisterArea::Coil, 113, RegisterType::Bool, RegisterAccess::ReadOnly, 
    RegisterBehavior::Continuous, RegisterGroup::Feedback, 
    "", "轴Y绝对定位中", 0 
  };

  constexpr RegisterInfo REL_MOVING = { 
    RegisterArea::Coil, 114, RegisterType::Bool, RegisterAccess::ReadOnly, 
    RegisterBehavior::Continuous, RegisterGroup::Feedback, 
    "", "轴Y相对定位中", 0 
  };

  constexpr RegisterInfo JOGGING = { 
    RegisterArea::Coil, 115, RegisterType::Bool, RegisterAccess::ReadOnly, 
    RegisterBehavior::Continuous, RegisterGroup::Feedback, 
    "", "轴Y点动中", 0 
  };

  // ==========================================
  // 保持寄存器区 (HoldingRegs - 状态字与数据反馈)
  // ==========================================
  constexpr RegisterInfo REL_POSITION_OLD = { 
    RegisterArea::HoldingReg, 2, RegisterType::Float32, RegisterAccess::ReadOnly, 
    RegisterBehavior::Continuous, RegisterGroup::Feedback, 
    "mm", "轴Y相对位置 (旧)", 0 
  };

  constexpr RegisterInfo ABS_POSITION_OLD = { 
    RegisterArea::HoldingReg, 12, RegisterType::Float32, RegisterAccess::ReadOnly, 
    RegisterBehavior::Continuous, RegisterGroup::Feedback, 
    "mm", "轴Y绝对位置 (旧)", 0 
  };

  // 明确引入物理设备的真实反馈状态
  constexpr RegisterInfo STATE = { 
    RegisterArea::HoldingReg, 101, RegisterType::Int16, RegisterAccess::ReadOnly, 
    RegisterBehavior::Continuous, RegisterGroup::Feedback, 
    "", "轴Y当前运行状态", 0 
  };

  constexpr RegisterInfo ALARM_CODE = { 
    RegisterArea::HoldingReg, 111, RegisterType::Int16, RegisterAccess::ReadOnly, 
    RegisterBehavior::Latch, RegisterGroup::Alarm, 
    "", "轴Y故障报警代码", 0 
  };

  constexpr RegisterInfo ABS_POSITION = { 
    RegisterArea::HoldingReg, 124, RegisterType::Float32, RegisterAccess::ReadOnly, 
    RegisterBehavior::Continuous, RegisterGroup::Feedback, 
    "mm", "轴Y当前绝对位置", 0 
  };

  constexpr RegisterInfo REL_POSITION = { 
    RegisterArea::HoldingReg, 126, RegisterType::Float32, RegisterAccess::ReadOnly, 
    RegisterBehavior::Continuous, RegisterGroup::Feedback, 
    "mm", "轴Y当前相对位置", 0 
  };

  constexpr RegisterInfo REL_ZERO_OFFSET = { 
    RegisterArea::HoldingReg, 138, RegisterType::Float32, RegisterAccess::ReadOnly, 
    RegisterBehavior::Continuous, RegisterGroup::Feedback, 
    "mm", "轴Y当前相对零点", 0 
  };

  // 软极限属于设备设定类参数，配置为 Parameter 组以便于拆分读取频率
  constexpr RegisterInfo SOFT_LIMIT_POS = { 
    RegisterArea::HoldingReg, 154, RegisterType::Float32, RegisterAccess::ReadOnly, 
    RegisterBehavior::Continuous, RegisterGroup::Parameter, 
    "mm", "轴Y软正极限", 0 
  };

  constexpr RegisterInfo SOFT_LIMIT_NEG = { 
    RegisterArea::HoldingReg, 156, RegisterType::Float32, RegisterAccess::ReadOnly, 
    RegisterBehavior::Continuous, RegisterGroup::Parameter, 
    "mm", "轴Y软负极限", 0 
  };

  constexpr RegisterInfo REL_ZERO_RECORD = { 
    RegisterArea::HoldingReg, 1022, RegisterType::Float32, RegisterAccess::ReadOnly, 
    RegisterBehavior::Continuous, RegisterGroup::Feedback, 
    "mm", "轴Y相对原点记录", 0 
  };
}

} // namespace plc::reg::y_axis