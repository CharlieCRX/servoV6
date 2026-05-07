#pragma once

#include "domain/port/IGantryFeedbackPort.h"
#include "infrastructure/FakePLC.h"
#include "infrastructure/logger/Logger.h"

/**
 * @brief IGantryFeedbackPort 的 Fake 实现
 *
 * 从 FakePLC 读取 X1/X2 状态并转换为 PhysicalAxisState DTO。
 *
 * 状态映射规则：
 *   - enabled:  AxisState 不是 Disabled 也不是 Unknown
 *   - alarmed:  AxisState == Error
 *   - posLimitActive: AxisFeedback::posLimit
 *   - negLimitActive: AxisFeedback::negLimit
 *   - position:  AxisFeedback::absPos
 */
class FakeGantryFeedbackPort : public IGantryFeedbackPort {
public:
    explicit FakeGantryFeedbackPort(FakePLC& plc)
        : m_plc(plc) {}

    PhysicalAxisState getX1Feedback() const override {
        return toPhysicalAxisState(m_plc.getFeedback(AxisId::X1), "X1");
    }

    PhysicalAxisState getX2Feedback() const override {
        return toPhysicalAxisState(m_plc.getFeedback(AxisId::X2), "X2");
    }

    void resetAlarm() override {
        // 将 X1/X2 从 Error 状态恢复到 Idle
        auto x1Fb = m_plc.getFeedback(AxisId::X1);
        if (x1Fb.state == AxisState::Error) {
            m_plc.forceState(AxisId::X1, AxisState::Idle);
        }
        auto x2Fb = m_plc.getFeedback(AxisId::X2);
        if (x2Fb.state == AxisState::Error) {
            m_plc.forceState(AxisId::X2, AxisState::Idle);
        }
    }

    bool isAnyAlarm() const override {
        return m_plc.getFeedback(AxisId::X1).state == AxisState::Error ||
               m_plc.getFeedback(AxisId::X2).state == AxisState::Error;
    }

    bool isAnyLimit() const override {
        auto x1Fb = m_plc.getFeedback(AxisId::X1);
        auto x2Fb = m_plc.getFeedback(AxisId::X2);
        return x1Fb.posLimit || x1Fb.negLimit ||
               x2Fb.posLimit || x2Fb.negLimit;
    }

private:
    FakePLC& m_plc;

    /// 将 FakePLC 的 AxisFeedback 转换为 PhysicalAxisState
    static PhysicalAxisState toPhysicalAxisState(const AxisFeedback& fb,
                                                  const char* axisName) {
        PhysicalAxisState state;
        state.position = fb.absPos;

        // enabled: 轴不在 Disabled/Unknown/Error 状态即为已使能
        state.enabled = (fb.state != AxisState::Disabled &&
                         fb.state != AxisState::Unknown &&
                         fb.state != AxisState::Error);

        // alarmed: Error 状态
        state.alarmed = (fb.state == AxisState::Error);

        state.posLimitActive = fb.posLimit;
        state.negLimitActive = fb.negLimit;

        return state;
    }
};
