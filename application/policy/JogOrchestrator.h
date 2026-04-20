#pragma once

#include "application/axis/EnableUseCase.h"
#include "application/axis/JogAxisUseCase.h"

class JogOrchestrator {
public:
    // 状态枚举可以预先定义好，方便测试用例和流转使用
    enum class Step {
        Idle,
        EnsuringEnabled,
        IssuingJog,
        Jogging,
        IssuingStop,
        WaitingForIdle,
        EnsuringDisabled,
        Done,
        Error
    };

    JogOrchestrator(EnableUseCase& enableUc, JogAxisUseCase& jogUc)
        : m_enableUc(enableUc), m_jogUc(jogUc) {}

    // 满足 Test 1, 2, 3, 4 的入口初始化
    void startJog(Direction dir) {
        m_dir = dir;
        m_step = Step::EnsuringEnabled;
        m_rejectionReason = RejectionReason::None;
    }

    // 最小化实现的 update
    void update(Axis& axis) {
        // ⭐ 全局最高优先级：硬件/状态错误拦截 (满足 Test 1)
        if (axis.state() == AxisState::Error) {
            m_step = Step::Error;
            return;
        }

        // 目前只处理 EnsuringEnabled 的业务语义
        switch (m_step) {
            case Step::EnsuringEnabled:
                // Test 2: Disabled → 必须触发 Enable
                if (axis.state() == AxisState::Disabled) {
                    m_enableUc.execute(axis, true);
                    break; 
                } 
                // Test 4: Idle → 进入 IssuingJog（仅仅流转状态）
                if (axis.state() == AxisState::Idle) {
                    m_step = Step::IssuingJog;
                    break;
                }
                // Test 3: Unknown -> 默认无动作，依靠 break 保持当前状态
                break;
                
            default:
                // 其他状态在当前的 TDD 阶段尚未进入，直接跳过
                break;
        }
    }

    // 满足测试用例中断言的查询接口
    Step currentStep() const { return m_step; }
    RejectionReason errorReason() const { return m_rejectionReason; }

private:
    EnableUseCase& m_enableUc;
    JogAxisUseCase& m_jogUc;

    Step m_step = Step::Idle;
    Direction m_dir = Direction::Forward;
    RejectionReason m_rejectionReason = RejectionReason::None;
};