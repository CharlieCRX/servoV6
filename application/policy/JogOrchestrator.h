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

    void startJog(Direction dir) {
        m_dir = dir;
        m_step = Step::EnsuringEnabled;
        m_rejectionReason = RejectionReason::None;

        m_jogIssued = false;
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

                if (axis.state() == AxisState::Disabled) {
                    m_enableUc.execute(axis, true);
                    break; 
                } 

                if (axis.state() == AxisState::Idle) {
                    m_step = Step::IssuingJog;
                    break;
                }

                break;
            case Step::IssuingJog:
                // Test 7: 幂等性保护，如果已经发过指令，直接跳过
                if (m_jogIssued) {
                    break;
                }

                // 尝试执行领域层动作 (Test 5, Test 6 方向透传)
                m_rejectionReason = m_jogUc.execute(axis, m_dir);
                
                if (m_rejectionReason == RejectionReason::None) {
                    // Test 5: 成功下发，流转到 Jogging
                    m_jogIssued = true;
                    m_step = Step::Jogging;
                } else {
                    // Test 8: 失败熔断（如被限位拦截） -> 安全掉电 + 报错
                    m_enableUc.execute(axis, false);
                    m_step = Step::Error;
                }
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

    // ⭐ 新增：用于防重入的标志位
    bool m_jogIssued = false;
};