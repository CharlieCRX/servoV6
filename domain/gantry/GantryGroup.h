#pragma once

#include "entity/Axis.h"
#include "gantry/GantryCouplingState.h"

class GantryGroup {
public:
    GantryGroup(Axis& x1,
                 Axis& x2)
        : m_x1(x1)
        , m_x2(x2)
    {
    }

    bool isCoupled() const
    {
        return m_state.isCoupled();
    }

private:
    Axis& m_x1;
    Axis& m_x2;

    GantryCouplingState m_state;
};