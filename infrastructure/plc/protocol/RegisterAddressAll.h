#pragma once
#include "RegisterMetadata.h"

namespace plc::reg::system_global {
using namespace plc::protocol;

namespace command {
  constexpr RegisterInfo ESTOP_TRIGGER = { 
    RegisterArea::Coil, 80, RegisterType::Bool, RegisterAccess::ReadWrite, 
    RegisterBehavior::Level, RegisterGroup::Command, 
    "", "设备急停触发", 0 
  };
}

namespace feedback {
  constexpr RegisterInfo ESTOP_ACTIVE = { 
    RegisterArea::Coil, 130, RegisterType::Bool, RegisterAccess::ReadOnly, 
    RegisterBehavior::Continuous, RegisterGroup::Alarm, 
    "", "设备急停中", 0 
  };

  constexpr RegisterInfo GANTRY_ERROR_CODE = { 
    RegisterArea::HoldingReg, 180, RegisterType::Int16, RegisterAccess::ReadOnly, 
    RegisterBehavior::Latch, RegisterGroup::Alarm, 
    "", "Gantry_Error_Code (龙门同步报警)", 0 
  };
}
} // namespace plc::reg::system_global


namespace plc::reg::x_axis {
using namespace plc::protocol;

namespace command {
  // --- 线圈控制 (Coils) ---
  constexpr RegisterInfo ENABLE_REQUEST = { 
    RegisterArea::Coil, 0, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "", "使能轴X电机", 0 };
  
  constexpr RegisterInfo LINKAGE_ENABLE = { 
    RegisterArea::Coil, 4, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "", "轴X联动使能", 0 };
  
  constexpr RegisterInfo HOME_TRIGGER = { 
    RegisterArea::Coil, 10, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴X回原点", 50 };
  
  constexpr RegisterInfo SET_REL_ZERO = { 
    RegisterArea::Coil, 14, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴X相对原点设置", 50 };
  
  constexpr RegisterInfo CLEAR_REL_ZERO = { 
    RegisterArea::Coil, 18, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴X相对原点清除", 50 };
  
  constexpr RegisterInfo CLEAR_ABS_POS = { 
    RegisterArea::Coil, 30, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴X绝对位置清零", 50 };
  
  constexpr RegisterInfo ABS_MOVE_TRIGGER = { 
    RegisterArea::Coil, 40, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴X绝对定位触发", 50 };
  
  constexpr RegisterInfo REL_MOVE_TRIGGER = { 
    RegisterArea::Coil, 41, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴X相对定位触发", 50 };
  
  // X1/X2 独立控制
  constexpr RegisterInfo X1_JOG_FORWARD = { RegisterArea::Coil, 50, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "", "轴X1点动正转", 0 };
  constexpr RegisterInfo X1_JOG_BACKWARD = { RegisterArea::Coil, 51, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "", "轴X1点动反转", 0 };
  constexpr RegisterInfo X2_JOG_FORWARD = { RegisterArea::Coil, 52, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "", "轴X2点动正转", 0 };
  constexpr RegisterInfo X2_JOG_BACKWARD = { RegisterArea::Coil, 53, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "", "轴X2点动反转", 0 };
  
  constexpr RegisterInfo ALARM_RESET = { 
    RegisterArea::Coil, 60, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴X报警解除触发", 50 };
  
  constexpr RegisterInfo X1_ABS_MOVE_TRIGGER = { RegisterArea::Coil, 70, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴X1单独绝对定位触发", 50 };
  constexpr RegisterInfo X2_ABS_MOVE_TRIGGER = { RegisterArea::Coil, 71, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴X2单独绝对定位触发", 50 };
  
  constexpr RegisterInfo X_ESTOP_TRIGGER = { 
    RegisterArea::Coil, 122, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "X轴急停触发", 50 };

  // --- 保持寄存器 (Holding Regs) ---
  constexpr RegisterInfo ABS_TARGET = { RegisterArea::HoldingReg, 20, RegisterType::Float32, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "mm", "轴X绝对定位距离", 0 };
  constexpr RegisterInfo REL_TARGET = { RegisterArea::HoldingReg, 22, RegisterType::Float32, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "mm", "轴X相对定位距离", 0 };
  constexpr RegisterInfo X1_ABS_TARGET = { RegisterArea::HoldingReg, 40, RegisterType::Float32, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "mm", "轴X1单独绝对定位距离", 0 };
  constexpr RegisterInfo X2_ABS_TARGET = { RegisterArea::HoldingReg, 42, RegisterType::Float32, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "mm", "轴X2单独绝对定位距离", 0 };
  
  constexpr RegisterInfo JOG_SPEED = { RegisterArea::HoldingReg, 1000, RegisterType::Float32, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "mm/s", "轴X手动速度", 0 };
  constexpr RegisterInfo MOVE_SPEED = { RegisterArea::HoldingReg, 1002, RegisterType::Float32, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "mm/s", "轴X定位速度", 0 };
  constexpr RegisterInfo TOLERANCE_LIMIT = { RegisterArea::HoldingReg, 1030, RegisterType::Float32, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Parameter, "mm", "轴X超差阈值", 0 };
  
  constexpr RegisterInfo SOFT_LIMIT_NEG = { RegisterArea::HoldingReg, 1040, RegisterType::Float32, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Parameter, "mm", "轴X软件负限位", 0 };
  constexpr RegisterInfo SOFT_LIMIT_POS = { RegisterArea::HoldingReg, 1042, RegisterType::Float32, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Parameter, "mm", "轴X软件正限位", 0 };
}

namespace feedback {
  // --- 线圈状态反馈 (Coils) ---
  constexpr RegisterInfo MOVE_DONE = { RegisterArea::Coil, 100, RegisterType::Bool, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "轴X运动完成", 0 };
  constexpr RegisterInfo ABS_MOVING = { RegisterArea::Coil, 110, RegisterType::Bool, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "轴X绝对定位中", 0 };
  constexpr RegisterInfo REL_MOVING = { RegisterArea::Coil, 111, RegisterType::Bool, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "轴X相对定位中", 0 };
  constexpr RegisterInfo JOGGING = { RegisterArea::Coil, 112, RegisterType::Bool, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "轴X点动中", 0 };
  
  constexpr RegisterInfo TOLERANCE_FLAG = { RegisterArea::Coil, 123, RegisterType::Bool, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Alarm, "", "X轴超差标志", 0 };
  constexpr RegisterInfo TOLERANCE_TIMEOUT = { RegisterArea::Coil, 124, RegisterType::Bool, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Alarm, "", "X轴超差时间到", 0 };
  constexpr RegisterInfo LINKAGE_STATE = { RegisterArea::Coil, 125, RegisterType::Bool, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "轴X联动状态", 0 };
  constexpr RegisterInfo SOFT_LIMIT_STATE = { RegisterArea::Coil, 126, RegisterType::Bool, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Alarm, "", "轴X软件限位状态", 0 };

  // --- 寄存器状态反馈 (Holding Regs) ---
  constexpr RegisterInfo REL_POSITION_OLD = { RegisterArea::HoldingReg, 0, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "mm", "轴X相对位置(旧)", 0 };
  constexpr RegisterInfo ABS_POSITION_OLD = { RegisterArea::HoldingReg, 10, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "mm", "轴X绝对位置(旧)", 0 };
  
  constexpr RegisterInfo STATE = { RegisterArea::HoldingReg, 100, RegisterType::Int16, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "轴X状态显示", 0 };
  constexpr RegisterInfo ALARM_CODE = { RegisterArea::HoldingReg, 110, RegisterType::Int16, RegisterAccess::ReadOnly, RegisterBehavior::Latch, RegisterGroup::Alarm, "", "轴X报警代码", 0 };
  
  constexpr RegisterInfo ABS_POSITION = { RegisterArea::HoldingReg, 120, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "mm", "轴X当前绝对位置", 0 };
  constexpr RegisterInfo REL_POSITION = { RegisterArea::HoldingReg, 122, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "mm", "轴X当前相对位置", 0 };
  constexpr RegisterInfo REL_ZERO_OFFSET = { RegisterArea::HoldingReg, 136, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "mm", "轴X当前相对零点", 0 };
  
  constexpr RegisterInfo X1_SOFT_LIMIT_POS = { RegisterArea::HoldingReg, 150, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Parameter, "mm", "轴X1软正极限", 0 };
  constexpr RegisterInfo X1_SOFT_LIMIT_NEG = { RegisterArea::HoldingReg, 152, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Parameter, "mm", "轴X1软负极限", 0 };
  constexpr RegisterInfo X2_SOFT_LIMIT_POS = { RegisterArea::HoldingReg, 162, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Parameter, "mm", "轴X2软正极限", 0 };
  constexpr RegisterInfo X2_SOFT_LIMIT_NEG = { RegisterArea::HoldingReg, 164, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Parameter, "mm", "轴X2软负极限", 0 };
  
  constexpr RegisterInfo X1_CURRENT_POS = { RegisterArea::HoldingReg, 170, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "mm", "轴X1当前位置", 0 };
  constexpr RegisterInfo X2_CURRENT_POS = { RegisterArea::HoldingReg, 172, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "mm", "轴X2当前位置", 0 };
  
  constexpr RegisterInfo REL_ZERO_RECORD = { RegisterArea::HoldingReg, 1020, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "mm", "轴X相对原点记录", 0 };
}
} // namespace plc::reg::x_axis


namespace plc::reg::y_axis {
using namespace plc::protocol;

namespace command {
  // --- 线圈控制 (Coils) ---
  constexpr RegisterInfo ENABLE_REQUEST = { RegisterArea::Coil, 1, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "", "使能轴Y电机", 0 };
  constexpr RegisterInfo HOME_TRIGGER = { RegisterArea::Coil, 11, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴Y回原点", 50 };
  constexpr RegisterInfo SET_REL_ZERO = { RegisterArea::Coil, 15, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴Y相对原点设置", 50 };
  constexpr RegisterInfo CLEAR_REL_ZERO = { RegisterArea::Coil, 19, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴Y相对原点清除", 50 };
  constexpr RegisterInfo CLEAR_ABS_POS = { RegisterArea::Coil, 31, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴Y绝对位置清零", 50 };
  
  constexpr RegisterInfo ABS_MOVE_TRIGGER = { RegisterArea::Coil, 42, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴Y绝对定位触发", 50 };
  constexpr RegisterInfo REL_MOVE_TRIGGER = { RegisterArea::Coil, 43, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴Y相对定位触发", 50 };
  
  constexpr RegisterInfo JOG_FORWARD = { RegisterArea::Coil, 54, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "", "轴Y点动正转", 0 };
  constexpr RegisterInfo JOG_BACKWARD = { RegisterArea::Coil, 55, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "", "轴Y点动反转", 0 };
  
  constexpr RegisterInfo ALARM_RESET = { RegisterArea::Coil, 61, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴Y报警解除触发", 50 };

  // --- 保持寄存器 (Holding Regs) ---
  constexpr RegisterInfo ABS_TARGET = { RegisterArea::HoldingReg, 24, RegisterType::Float32, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "mm", "轴Y绝对定位距离", 0 };
  constexpr RegisterInfo REL_TARGET = { RegisterArea::HoldingReg, 26, RegisterType::Float32, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "mm", "轴Y相对定位距离", 0 };
  
  constexpr RegisterInfo JOG_SPEED = { RegisterArea::HoldingReg, 1004, RegisterType::Float32, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "mm/s", "轴Y手动速度", 0 };
  constexpr RegisterInfo MOVE_SPEED = { RegisterArea::HoldingReg, 1006, RegisterType::Float32, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "mm/s", "轴Y定位速度", 0 };
}

namespace feedback {
  // --- 线圈状态反馈 (Coils) ---
  constexpr RegisterInfo MOVE_DONE = { RegisterArea::Coil, 101, RegisterType::Bool, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "轴Y运动完成", 0 };
  constexpr RegisterInfo ABS_MOVING = { RegisterArea::Coil, 113, RegisterType::Bool, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "轴Y绝对定位中", 0 };
  constexpr RegisterInfo REL_MOVING = { RegisterArea::Coil, 114, RegisterType::Bool, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "轴Y相对定位中", 0 };
  constexpr RegisterInfo JOGGING = { RegisterArea::Coil, 115, RegisterType::Bool, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "轴Y点动中", 0 };

  // --- 寄存器状态反馈 (Holding Regs) ---
  constexpr RegisterInfo REL_POSITION_OLD = { RegisterArea::HoldingReg, 2, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "mm", "轴Y相对位置(旧)", 0 };
  constexpr RegisterInfo ABS_POSITION_OLD = { RegisterArea::HoldingReg, 12, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "mm", "轴Y绝对位置(旧)", 0 };
  
  constexpr RegisterInfo STATE = { RegisterArea::HoldingReg, 101, RegisterType::Int16, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "轴Y状态显示", 0 };
  constexpr RegisterInfo ALARM_CODE = { RegisterArea::HoldingReg, 111, RegisterType::Int16, RegisterAccess::ReadOnly, RegisterBehavior::Latch, RegisterGroup::Alarm, "", "轴Y报警代码", 0 };
  
  constexpr RegisterInfo ABS_POSITION = { RegisterArea::HoldingReg, 124, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "mm", "轴Y当前绝对位置", 0 };
  constexpr RegisterInfo REL_POSITION = { RegisterArea::HoldingReg, 126, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "mm", "轴Y当前相对位置", 0 };
  constexpr RegisterInfo REL_ZERO_OFFSET = { RegisterArea::HoldingReg, 138, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "mm", "轴Y当前相对零点", 0 };
  
  constexpr RegisterInfo SOFT_LIMIT_POS = { RegisterArea::HoldingReg, 154, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Parameter, "mm", "轴Y软正极限", 0 };
  constexpr RegisterInfo SOFT_LIMIT_NEG = { RegisterArea::HoldingReg, 156, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Parameter, "mm", "轴Y软负极限", 0 };
  
  constexpr RegisterInfo REL_ZERO_RECORD = { RegisterArea::HoldingReg, 1022, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "mm", "轴Y相对原点记录", 0 };
}
} // namespace plc::reg::y_axis

namespace plc::reg::z_axis {
using namespace plc::protocol;

namespace command {
  constexpr RegisterInfo ENABLE_REQUEST = { RegisterArea::Coil, 2, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "", "使能轴Z电机", 0 };
  constexpr RegisterInfo HOME_TRIGGER = { RegisterArea::Coil, 12, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴Z回原点", 50 };
  constexpr RegisterInfo SET_REL_ZERO = { RegisterArea::Coil, 16, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴Z相对原点设置", 50 };
  constexpr RegisterInfo CLEAR_REL_ZERO = { RegisterArea::Coil, 20, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴Z相对原点清除", 50 };
  constexpr RegisterInfo CLEAR_ABS_POS = { RegisterArea::Coil, 32, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴Z绝对位置清零", 50 };
  constexpr RegisterInfo ABS_MOVE_TRIGGER = { RegisterArea::Coil, 44, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴Z绝对定位触发", 50 };
  constexpr RegisterInfo REL_MOVE_TRIGGER = { RegisterArea::Coil, 45, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴Z相对定位触发", 50 };
  constexpr RegisterInfo JOG_FORWARD = { RegisterArea::Coil, 56, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "", "轴Z点动正转", 0 };
  constexpr RegisterInfo JOG_BACKWARD = { RegisterArea::Coil, 57, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "", "轴Z点动反转", 0 };
  constexpr RegisterInfo ALARM_RESET = { RegisterArea::Coil, 62, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴Z报警解除触发", 50 };

  constexpr RegisterInfo ABS_TARGET = { RegisterArea::HoldingReg, 28, RegisterType::Float32, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "mm", "轴Z绝对定位距离", 0 };
  constexpr RegisterInfo REL_TARGET = { RegisterArea::HoldingReg, 30, RegisterType::Float32, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "mm", "轴Z相对定位距离", 0 };
  constexpr RegisterInfo JOG_SPEED = { RegisterArea::HoldingReg, 1008, RegisterType::Float32, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "mm/s", "轴Z手动速度", 0 };
  constexpr RegisterInfo MOVE_SPEED = { RegisterArea::HoldingReg, 1010, RegisterType::Float32, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "mm/s", "轴Z定位速度", 0 };
}

namespace feedback {
  constexpr RegisterInfo MOVE_DONE = { RegisterArea::Coil, 102, RegisterType::Bool, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "轴Z运动完成", 0 };
  constexpr RegisterInfo ABS_MOVING = { RegisterArea::Coil, 116, RegisterType::Bool, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "轴Z绝对定位中", 0 };
  constexpr RegisterInfo REL_MOVING = { RegisterArea::Coil, 117, RegisterType::Bool, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "轴Z相对定位中", 0 };
  constexpr RegisterInfo JOGGING = { RegisterArea::Coil, 118, RegisterType::Bool, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "轴Z点动中", 0 };

  constexpr RegisterInfo REL_POSITION_OLD = { RegisterArea::HoldingReg, 4, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "mm", "轴Z相对位置", 0 };
  constexpr RegisterInfo ABS_POSITION_OLD = { RegisterArea::HoldingReg, 14, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "mm", "轴Z绝对位置", 0 };
  constexpr RegisterInfo STATE = { RegisterArea::HoldingReg, 102, RegisterType::Int16, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "轴Z状态显示", 0 };
  constexpr RegisterInfo ALARM_CODE = { RegisterArea::HoldingReg, 112, RegisterType::Int16, RegisterAccess::ReadOnly, RegisterBehavior::Latch, RegisterGroup::Alarm, "", "轴Z报警代码", 0 };
  constexpr RegisterInfo ABS_POSITION = { RegisterArea::HoldingReg, 128, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "mm", "轴Z当前绝对位置", 0 };
  constexpr RegisterInfo REL_POSITION = { RegisterArea::HoldingReg, 130, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "mm", "轴Z当前相对位置", 0 };
  constexpr RegisterInfo REL_ZERO_OFFSET = { RegisterArea::HoldingReg, 140, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "mm", "轴Z当前相对零点", 0 };
  constexpr RegisterInfo SOFT_LIMIT_POS = { RegisterArea::HoldingReg, 158, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Parameter, "mm", "轴Z软正极限", 0 };
  constexpr RegisterInfo SOFT_LIMIT_NEG = { RegisterArea::HoldingReg, 160, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Parameter, "mm", "轴Z软负极限", 0 };
  constexpr RegisterInfo REL_ZERO_RECORD = { RegisterArea::HoldingReg, 1024, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "mm", "轴Z相对原点记录", 0 };
}
} // namespace plc::reg::z_axis


namespace plc::reg::r_axis {
using namespace plc::protocol;

namespace command {
  constexpr RegisterInfo ENABLE_REQUEST = { RegisterArea::Coil, 3, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "", "使能轴R电机", 0 };
  constexpr RegisterInfo HOME_TRIGGER = { RegisterArea::Coil, 13, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴R回原点", 50 };
  constexpr RegisterInfo SET_REL_ZERO = { RegisterArea::Coil, 17, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴R相对原点设置", 50 };
  constexpr RegisterInfo CLEAR_REL_ZERO = { RegisterArea::Coil, 21, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴R相对原点清除", 50 };
  constexpr RegisterInfo CLEAR_ABS_POS = { RegisterArea::Coil, 33, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴R绝对位置清零", 50 };
  constexpr RegisterInfo ABS_MOVE_TRIGGER = { RegisterArea::Coil, 46, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴R绝对定位触发", 50 };
  constexpr RegisterInfo REL_MOVE_TRIGGER = { RegisterArea::Coil, 47, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴R相对定位触发", 50 };
  constexpr RegisterInfo JOG_FORWARD = { RegisterArea::Coil, 58, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "", "轴R点动正转", 0 };
  constexpr RegisterInfo JOG_BACKWARD = { RegisterArea::Coil, 59, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "", "轴R点动反转", 0 };
  constexpr RegisterInfo ALARM_RESET = { RegisterArea::Coil, 63, RegisterType::Bool, RegisterAccess::ReadWrite, RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command, "", "轴R报警解除触发", 50 };

  constexpr RegisterInfo ABS_TARGET = { RegisterArea::HoldingReg, 32, RegisterType::Float32, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "deg", "轴R绝对定位距离", 0 };
  constexpr RegisterInfo REL_TARGET = { RegisterArea::HoldingReg, 34, RegisterType::Float32, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "deg", "轴R相对定位距离", 0 };
  constexpr RegisterInfo JOG_SPEED = { RegisterArea::HoldingReg, 1012, RegisterType::Float32, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "deg/s", "轴R手动速度", 0 };
  constexpr RegisterInfo MOVE_SPEED = { RegisterArea::HoldingReg, 1014, RegisterType::Float32, RegisterAccess::ReadWrite, RegisterBehavior::Level, RegisterGroup::Command, "deg/s", "轴R定位速度", 0 };
}

namespace feedback {
  constexpr RegisterInfo MOVE_DONE = { RegisterArea::Coil, 103, RegisterType::Bool, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "轴R运动完成", 0 };
  constexpr RegisterInfo ABS_MOVING = { RegisterArea::Coil, 119, RegisterType::Bool, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "轴R绝对定位中", 0 };
  constexpr RegisterInfo REL_MOVING = { RegisterArea::Coil, 120, RegisterType::Bool, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "轴R相对定位中", 0 };
  constexpr RegisterInfo JOGGING = { RegisterArea::Coil, 121, RegisterType::Bool, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "轴R点动中", 0 };

  constexpr RegisterInfo REL_POSITION_OLD = { RegisterArea::HoldingReg, 6, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "deg", "轴R相对位置", 0 };
  constexpr RegisterInfo ABS_POSITION_OLD = { RegisterArea::HoldingReg, 16, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "deg", "轴R绝对位置", 0 };
  constexpr RegisterInfo STATE = { RegisterArea::HoldingReg, 103, RegisterType::Int16, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "", "轴R状态显示", 0 };
  constexpr RegisterInfo ALARM_CODE = { RegisterArea::HoldingReg, 113, RegisterType::Int16, RegisterAccess::ReadOnly, RegisterBehavior::Latch, RegisterGroup::Alarm, "", "轴R报警代码", 0 };
  constexpr RegisterInfo ABS_POSITION = { RegisterArea::HoldingReg, 132, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "deg", "轴R当前绝对位置", 0 };
  constexpr RegisterInfo REL_POSITION = { RegisterArea::HoldingReg, 134, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "deg", "轴R当前相对位置", 0 };
  constexpr RegisterInfo ABS_ZERO_OFFSET = { RegisterArea::HoldingReg, 142, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "deg", "轴R当前绝对零点", 0 }; // 注意：点位表写的是绝对零点
  constexpr RegisterInfo REL_ZERO_RECORD = { RegisterArea::HoldingReg, 1026, RegisterType::Float32, RegisterAccess::ReadOnly, RegisterBehavior::Continuous, RegisterGroup::Feedback, "deg", "轴R相对原点记录", 0 };
}
} // namespace plc::reg::r_axis