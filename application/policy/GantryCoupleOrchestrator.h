#pragma once

#include "entity/SystemContext.h"
#include "gantry/GantryGroup.h"

class GantryCoupleOrchestrator {
public:
    GantryCoupleOrchestrator(
        SystemContext& ctx,
        GantryGroup& gantry)
        : m_ctx(ctx)
        , m_gantry(gantry)
    {
    }

    void start()
    {
        m_started = true;
    }

    void tick()
    {
        if (!m_started) {
            return;
        }

        auto& x = m_ctx.getAxis(AxisId::X);

        if (x.state() != AxisState::Idle) {
            x.enable(true);
        }

        if (!m_gantry.isCoupled()) {
            m_gantry.couple();
        }
    }

private:
    SystemContext& m_ctx;
    GantryGroup& m_gantry;

    bool m_started = false;
};