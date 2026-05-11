#pragma once

class GantryCouplingState {
public:
    bool isCoupled() const
    {
        return m_coupled;
    }

    void couple()
    {
        m_coupled = true;
    }

    void decouple()
    {
        m_coupled = false;
    }

private:
    bool m_coupled = false;
};