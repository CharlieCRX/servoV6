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
                // 发送耦合指令，然后立刻交出控制权，切换到倾听状态
                m_gantry.couple();
                m_step = Step::WaitingCoupled;
                break;

            case Step::WaitingCoupled:
                // ⭐ 新增：轮询底层的真实物理状态
                if (m_gantry.isCoupled()) {
                    m_step = Step::Done;
                }
                // TODO: 未来可以在这里加上 Timeout 逻辑
                // else if (timeout) { m_step = Step::Error; }
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