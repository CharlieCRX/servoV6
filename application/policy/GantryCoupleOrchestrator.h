#pragma once

#include "entity/SystemContext.h"
#include "gantry/GantryGroup.h"

class GantryCoupleOrchestrator {
public:
    enum class Step {
        Initial,
        EnsuringEnabled,
        WaitingAxisReady,
        Coupling,
        WaitingCoupled,
        Done,
        Error
    };
    
    GantryCoupleOrchestrator(
        SystemContext& ctx,
        GantryGroup& gantry)
        : m_ctx(ctx)
        , m_gantry(gantry)
        , m_step(Step::Initial)
    {
    }

    void start()
    {
        m_step = Step::EnsuringEnabled;
    }

    void tick()
    {
        auto& x = m_ctx.getAxis(AxisId::X);

        switch (m_step) {
            case Step::Initial:
                break;

            case Step::EnsuringEnabled:
                if (x.state() == AxisState::Disabled) {
                    x.enable(true);
                    m_step = Step::WaitingAxisReady;
                } 
                else if (x.state() == AxisState::Idle) {
                    m_step = Step::Coupling;
                } 
                else {
                    m_step = Step::Error;
                }
                break;

            case Step::WaitingAxisReady:
                if (x.state() == AxisState::Idle) {
                    m_step = Step::Coupling;
                } else if (x.state() == AxisState::Error) {
                    m_step = Step::Error;
                }
                break;

            case Step::Coupling:
                // 上位机只负责表达意图 (Request)
                m_gantry.requestCouple();
                m_step = Step::WaitingCoupled;
                break;

            case Step::WaitingCoupled:
                // Orchestrator 绝不做 m_gantry.markCoupled()，只做状态观测。
                // 真正的 markCoupled 由底层 PLC 通讯机制去触发。
                if (m_gantry.isCoupled()) {
                    m_step = Step::Done;
                }
                break;

            case Step::Done:
            case Step::Error:
                break;
        }
    }

    Step currentStep() const { return m_step; }

private:
    SystemContext& m_ctx;
    GantryGroup& m_gantry;

    Step m_step;
};