#pragma once

#include "application/axis/EnableUseCase.h"
#include "application/axis/JogAxisUseCase.h"
#include "domain/entity/AxisId.h"
#include "infrastructure/logger/Logger.h"
#include "infrastructure/logger/TraceScope.h"

class JogOrchestrator {
public:
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

    // ⭐ 接收目标轴标识 AxisId
    void startJog(AxisId id, Direction dir) {
        m_targetId = id; // 记录目标轴
        m_dir = dir;
        m_step = Step::EnsuringEnabled;
        m_rejectionReason = RejectionReason::None;

        m_jogIssued = false;
        m_stopIssued = false;
        m_disableIssued = false;

        m_traceId = TraceScope::current().traceId;
        std::string dirStr = (dir == Direction::Forward) ? "Forward(+)" : "Backward(-)";
        LOG_INFO(LogLayer::APP, "JogOrch", "START Jog direction=" + dirStr);
    }

    // ⭐ 终极防误杀：必须同时校验 AxisId 和 方向
    void stopJog(AxisId id, Direction dir) {
        if (id != m_targetId || dir != m_dir) {
            LOG_WARN(LogLayer::APP, "JogOrch", 
                "Received stopJog for wrong axis or direction. "
                "Expected Axis: " + std::to_string(static_cast<int>(m_targetId)) + 
                ", Got: " + std::to_string(static_cast<int>(id)) + ". Ignoring.");
            return;
        }

        if (m_step == Step::EnsuringEnabled || 
            m_step == Step::IssuingJog || 
            m_step == Step::Jogging) {
                LOG_INFO(LogLayer::APP, "JogOrch", "Stop Jog Requested by UI");
                m_step = Step::IssuingStop;
        }
    }

    void update(Axis& axis) {
        TraceScope scope("G1", "Y", m_traceId);

        // 全局最高优先级：硬件/状态错误拦截
        if (axis.state() == AxisState::Error) {
            m_step = Step::Error;
            return;
        }

        switch (m_step) {
            case Step::EnsuringEnabled:
                if (axis.state() == AxisState::Disabled) {
                    m_enableUc.execute(m_targetId, true); // ⭐ 按 ID 路由
                    break; 
                } 
                if (axis.state() == AxisState::Idle) {
                    LOG_DEBUG(LogLayer::APP, "JogOrch", "Step: EnsuringEnabled -> IssuingJog");
                    m_step = Step::IssuingJog;
                    break;
                }
                break;

            case Step::IssuingJog:
                if (m_jogIssued) {
                    break;
                }

                // ⭐ 按 ID 路由
                m_rejectionReason = m_jogUc.execute(m_targetId, m_dir);
                
                if (m_rejectionReason == RejectionReason::None) {
                    LOG_DEBUG(LogLayer::APP, "JogOrch", "Step: IssuingJog -> Jogging");
                    m_jogIssued = true;
                    m_step = Step::Jogging;
                } else {
                    LOG_ERROR(LogLayer::APP, "JogOrch", "Jog start rejected by Domain");
                    m_enableUc.execute(m_targetId, false); // ⭐ 按 ID 路由，失败熔断掉电
                    m_step = Step::Error;
                }
                break;

            case Step::Jogging:
                if (axis.state() == AxisState::Idle && !axis.hasPendingCommand()) {
                    LOG_ERROR(LogLayer::APP, "JogOrch", "Axis unexpectedly stopped during Jog! Forcing stop sequence.");
                    m_step = Step::IssuingStop; 
                }
                break;

            case Step::IssuingStop:
                if (!m_stopIssued) {
                    m_jogUc.stop(m_targetId, m_dir); // ⭐ 按 ID 路由
                    m_stopIssued = true;
                }
                LOG_DEBUG(LogLayer::APP, "JogOrch", "Step: IssuingStop -> WaitingForIdle");
                m_step = Step::WaitingForIdle;
                break;

            case Step::WaitingForIdle:
                if (axis.state() == AxisState::Idle) {
                    LOG_DEBUG(LogLayer::APP, "JogOrch", "Step: WaitingForIdle -> EnsuringDisabled");
                    m_step = Step::EnsuringDisabled;
                }
                break;

            case Step::EnsuringDisabled:
                if (!m_disableIssued) {
                    m_enableUc.execute(m_targetId, false); // ⭐ 按 ID 路由
                    m_disableIssued = true;
                }
                
                if (axis.state() == AxisState::Disabled) {
                    LOG_SUMMARY(LogLayer::APP, "JogOrch", "Jog Operation -> SUCCESS (Safely Stopped)");
                    m_step = Step::Done;
                }
                break;
            
            case Step::Done:
            case Step::Error:
            default:
                break;
        }
    }

    Step currentStep() const { return m_step; }
    RejectionReason errorReason() const { return m_rejectionReason; }

private:
    EnableUseCase& m_enableUc;
    JogAxisUseCase& m_jogUc;

    Step m_step = Step::Idle;
    AxisId m_targetId = AxisId::Y;   // ⭐ 目标轴标识
    Direction m_dir = Direction::Forward;
    RejectionReason m_rejectionReason = RejectionReason::None;

    bool m_jogIssued = false;
    bool m_stopIssued = false;
    bool m_disableIssued = false;

    std::string m_traceId = "N/A";
};