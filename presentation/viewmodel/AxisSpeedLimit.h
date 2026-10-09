// ============================================================================
// AxisSpeedLimit.h —— 界面速度上限（所有轴统一：点动速度 / 定位速度）
// ============================================================================
// 需求：界面上可设置的点动速度与位置移动速度，所有轴统一上限 30
//       （直线轴单位 mm/s，旋转轴 R 单位 °/s）；界面输入超过上限时按上限（30）设置。
//
// 本头文件是该上限在 C++ 侧的唯一事实来源；QML 侧对应 Theme.maxAxisSpeed
// （presentation/qml/core/Theme.qml），修改上限时必须两处同步。
//
// UI 写入闸门（本上限的生效位置）：
//   - UiControlCommandAdapter：vnext 链路（QML `controlCommand`）全部速度写入；
//   - QtAxisViewModel        ：legacy 链路（QML `viewModel`）点动 / 定位速度写入；
//   - MotionController       ：摇杆 Position 模式从快照读取的定位速度。
// 界面输入控件（NumPad / TextField）另在 QML 侧同步钳制，做到"输入即受限"。
//
// 说明：本上限是界面侧的输入约束，不改变领域层 / 协调层既有校验（速度必须为正等）。
// ============================================================================
#ifndef AXIS_SPEED_LIMIT_H
#define AXIS_SPEED_LIMIT_H

#include <algorithm>

namespace presentation {

/// 界面允许设置的最大轴速度：mm/s（直线轴 X/X1/X2/Y/Z）/ °/s（旋转轴 R）。
inline constexpr double kMaxAxisSpeed = 30.0;

/// 把界面输入的速度钳制到上限；非正数 / NaN 原样返回，交由既有校验拒绝。
inline double clampAxisSpeed(double v) {
    if (!(v > 0.0)) return v;              // NaN / <=0：保持原值
    return std::min(v, kMaxAxisSpeed);     // 超过上限 → 按上限
}

}  // namespace presentation

#endif  // AXIS_SPEED_LIMIT_H
