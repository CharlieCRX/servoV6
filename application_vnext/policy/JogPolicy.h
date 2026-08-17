// ============================================================================
// JogPolicy.h -- vnext jog policy with explicit stop handling.
// ============================================================================
#pragma once

#include <chrono>
#include <string>

#include "application_vnext/SystemManagerVnext.h"
#include "application_vnext/policy/AxisMotionCommon.h"
#include "application_vnext/policy/GantryMotionGuard.h"
#include "domain_vnext/model/AxisFunction.h"
#include "domain_vnext/system/AxisRegistry.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"

namespace application_vnext::policy {

class JogPolicy {
public:
    enum class Step {
        Idle, EnsuringEnabled, PostEnableDelay, IssuingJog, Jogging,
        IssuingStop, WaitingForIdle, PostStopDelay, EnsuringDisabled, Done, Error
    };

    JogPolicy(SystemManagerVnext& m, plc_vnext::contracts::PlcAxisSlot slot,
              bool forward, int durationMs = 0, int heartbeatPeriodMs = 500)
        : m_(&m), slot_(slot), forward_(forward),
          durationMs_(durationMs), heartbeatPeriodMs_(heartbeatPeriodMs) {}

    void start() {
        m_step = (power_ == PowerOwnership::LifecycleManaged) ? Step::PostEnableDelay
                                                              : Step::EnsuringEnabled;
        m_diag.clear();
        m_fnValid = false;
        m_enableSent = false;
        m_stopSent = false;
        m_disableSent = false;
        m_stopRequested = false;
        m_heartbeatFailed = false;
        m_heartbeatWritten = false;
        m_idleReachedTime = std::chrono::steady_clock::now();
        if (const auto* axis = m_->system().findBySlot(slot_)) {
            m_group = axis->key().group;
            m_fn = axis->key().function;
            m_fnValid = true;
        }
        if (!m_fnValid) {
            m_step = Step::Error;
            m_diag = "slot not registered";
        }
    }

    void requestStop() { forceStopNow(); }

    void forceStopNow() {
        m_stopRequested = true;
        stopHeartbeatAndDirection();
        m_stopSent = true;
        if (m_step == Step::PostEnableDelay || m_step == Step::IssuingJog ||
            m_step == Step::Jogging || m_step == Step::IssuingStop) {
            m_step = Step::WaitingForIdle;
        }
    }

    void setPowerOwnership(PowerOwnership p) {
        if (m_step == Step::Idle) power_ = p;
    }
    void setGantryGuard(const GantryMotionGuard* g) { guard_ = g; }
    void setUnavailable(const char* reason) { m_step = Step::Error; m_diag = reason; }

    void tick() {
        if (m_step == Step::Done || m_step == Step::Error) return;

        if (m_->isSystemLocked()) {
            if (m_step != Step::Idle) {
                stopHeartbeatAndDirection();
                disableMotor();
                m_diag = "safety locked, aborted";
                m_step = Step::Done;
            }
            return;
        }

        const domain_vnext::system::Axis* axis = m_->system().findBySlot(slot_);
        if (!axis) {
            m_step = Step::Error;
            m_diag = "slot not registered";
            stopHeartbeatAndDirection();
            disableMotor();
            return;
        }
        m_group = axis->key().group;
        m_fn = axis->key().function;
        m_fnValid = true;

        const auto& fb = axis->feedback();
        const int16_t ms = fb.motionState;
        const bool alarm = fb.alarmWord != 0;
        const bool limit = fb.motionLimit != 0;

        if (m_step != Step::Idle && alarm) {
            m_step = Step::Error;
            m_diag = "axis alarm";
            stopHeartbeatAndDirection();
            disableMotor();
            return;
        }

        if (power_ == PowerOwnership::LifecycleManaged && guard_) {
            const auto gr = guard_->evaluate();
            if (!gr.allowed) {
                stopHeartbeatAndDirection();
                m_step = Step::Error;
                m_diag = std::string("gantry permit lost: ") + gr.reason;
                return;
            }
        }

        using clock = std::chrono::steady_clock;
        const auto now = clock::now();
        auto elapsedSince = [&](clock::time_point t) {
            return std::chrono::duration<double>(now - t).count();
        };

        switch (m_step) {
        case Step::EnsuringEnabled:
            if (ms == kMotionMotorIdle) {
                m_enableSent = false;
                m_idleReachedTime = now;
                m_step = Step::PostEnableDelay;
                break;
            }
            if (!m_enableSent) {
                bool ok = true;
                if (ms == kMotionNotEnabled) ok = appResultOk(m_->enableAxis(m_group, m_fn, true));
                if (ok) ok = appResultOk(m_->enableMotor(m_group, m_fn, true));
                if (!ok) { m_step = Step::Error; m_diag = "enable failed"; return; }
                m_enableSent = true;
                m_enableSentTime = now;
            } else if (elapsedSince(m_enableSentTime) > kEnableTimeoutSeconds) {
                m_step = Step::Error;
                m_diag = "enable timeout (motionState!=2)";
                return;
            }
            break;

        case Step::PostEnableDelay:
            if (ms != kMotionMotorIdle) {
                m_step = Step::Error;
                m_diag = "axis left idle during enable delay";
                return;
            }
            if (elapsedSince(m_idleReachedTime) >= kPostEnableDelaySeconds) {
                m_step = Step::IssuingJog;
            }
            break;

        case Step::IssuingJog:
            if (!m_heartbeatWritten) {
                if (!appResultOk(m_->jogHeartbeat(m_group, m_fn, true))) {
                    m_step = Step::Error;
                    m_diag = "jog heartbeat ON failed";
                    return;
                }
                m_heartbeatWritten = true;
                m_lastHeartbeatTime = now;
                if (!appResultOk(m_->jog(m_group, m_fn, forward_, true))) {
                    m_step = Step::Error;
                    m_diag = "jog direction ON failed";
                    stopHeartbeatAndDirection();
                    return;
                }
                m_jogStartTime = now;
            }
            m_step = Step::Jogging;
            break;

        case Step::Jogging: {
            const bool timedOut = durationMs_ > 0 &&
                elapsedSince(m_jogStartTime) >= static_cast<double>(durationMs_) / 1000.0;
            if (!timedOut && !m_stopRequested && !limit) {
                if (elapsedSince(m_lastHeartbeatTime) >=
                    static_cast<double>(heartbeatPeriodMs_) / 1000.0) {
                    if (!appResultOk(m_->jogHeartbeat(m_group, m_fn, true))) m_heartbeatFailed = true;
                    m_lastHeartbeatTime = now;
                }
            }
            if (m_stopRequested || m_heartbeatFailed || timedOut || limit) {
                m_step = Step::IssuingStop;
            }
            break;
        }

        case Step::IssuingStop:
            if (!m_stopSent) {
                stopHeartbeatAndDirection();
                m_stopSent = true;
            }
            m_step = Step::WaitingForIdle;
            break;

        case Step::WaitingForIdle:
            if (ms == kMotionMotorIdle) {
                m_stopIdleReachedTime = now;
                m_step = Step::PostStopDelay;
            }
            break;

        case Step::PostStopDelay:
            if (elapsedSince(m_stopIdleReachedTime) >= kPostStopDelaySeconds) {
                m_step = Step::EnsuringDisabled;
            }
            break;

        case Step::EnsuringDisabled:
            disableMotor();
            m_step = Step::Done;
            break;

        case Step::Idle:
        case Step::Done:
        case Step::Error:
            break;
        }
    }

    Step currentStep() const { return m_step; }
    bool isDone() const { return m_step == Step::Done; }
    bool hasError() const { return m_step == Step::Error; }
    const std::string& diag() const { return m_diag; }

    static const char* stepName(Step s) {
        switch (s) {
            case Step::Idle:             return "Idle";
            case Step::EnsuringEnabled:  return "EnsuringEnabled";
            case Step::PostEnableDelay:  return "PostEnableDelay";
            case Step::IssuingJog:       return "IssuingJog";
            case Step::Jogging:          return "Jogging";
            case Step::IssuingStop:      return "IssuingStop";
            case Step::WaitingForIdle:   return "WaitingForIdle";
            case Step::PostStopDelay:    return "PostStopDelay";
            case Step::EnsuringDisabled: return "EnsuringDisabled";
            case Step::Done:             return "Done";
            case Step::Error:            return "Error";
        }
        return "?";
    }

private:
    void stopHeartbeatAndDirection() {
        if (m_fnValid) {
            m_->jogHeartbeat(m_group, m_fn, false);
            m_->jog(m_group, m_fn, true, false);
            m_->jog(m_group, m_fn, false, false);
        }
        m_heartbeatWritten = false;
    }

    void disableMotor() {
        if (m_disableSent) return;
        m_disableSent = true;
        if (m_fnValid && power_ != PowerOwnership::LifecycleManaged) {
            m_->enableMotor(m_group, m_fn, false);
        }
    }

    SystemManagerVnext* m_;
    plc_vnext::contracts::PlcAxisSlot slot_;
    bool forward_;
    int durationMs_;
    int heartbeatPeriodMs_;
    Step m_step = Step::Idle;
    plc_vnext::contracts::PlcGroupIndex m_group{0};
    domain_vnext::model::AxisFunction m_fn = domain_vnext::model::AxisFunction::X;
    bool m_fnValid = false;
    std::string m_diag;

    bool m_enableSent = false;
    std::chrono::steady_clock::time_point m_enableSentTime;
    std::chrono::steady_clock::time_point m_idleReachedTime;

    bool m_heartbeatWritten = false;
    std::chrono::steady_clock::time_point m_lastHeartbeatTime;
    std::chrono::steady_clock::time_point m_jogStartTime;

    bool m_stopRequested = false;
    bool m_heartbeatFailed = false;
    bool m_stopSent = false;
    std::chrono::steady_clock::time_point m_stopIdleReachedTime;

    PowerOwnership power_ = PowerOwnership::SelfManaged;
    const GantryMotionGuard* guard_ = nullptr;
    bool m_disableSent = false;
};

}  // namespace application_vnext::policy
