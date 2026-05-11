#pragma once

#include "entity/Axis.h"
#include "gantry/GantryCouplingState.h"

class GantryGroup {
public:
    GantryGroup(Axis& x1, Axis& x2)
        : m_x1(x1), m_x2(x2) {}

    bool isCoupled() const { return m_state.isCoupled(); }
    
    bool isCouplingRequested() const { return m_state.isCouplingRequested(); }

    void requestCouple() { m_state.requestCouple(); }


    // @note feedback methods for testing purposes
    void applyCoupledFeedback() { m_state.applyCoupledFeedback(); }
    void applyDecoupledFeedback() { m_state.applyDecoupledFeedback(); }

private:
    Axis& m_x1;
    Axis& m_x2;
    GantryCouplingState m_state;
};