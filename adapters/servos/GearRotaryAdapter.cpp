#include "GearRotaryAdapter.h"
#include <cmath> // 用于数学计算

// 构造函数实现
GearRotaryAdapter::GearRotaryAdapter(IMotor* motor, double gearDiameter, double reductionRatio)
    : m_motor(motor), m_reductionRatio(reductionRatio)
{
    // 构造函数中可以处理 gearDiameter，例如用于计算其他参数
    // 为了让当前测试通过，我们仅需初始化 reductionRatio
}

// 实现 absoluteAngularMove 方法，这是测试的核心
bool GearRotaryAdapter::absoluteAngularMove(double targetDegrees) {
    // 业务单位 (度) -> 电机单位 (圈) 的转换
    double revolutions = (targetDegrees / 360.0) * m_reductionRatio;

    // 调用底层电机接口
    if (!m_motor->setAbsoluteTargetRevolutions(revolutions)) {
        return false;
    }

    return m_motor->triggerMove();
}
