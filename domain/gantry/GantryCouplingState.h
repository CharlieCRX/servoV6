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
    bool m_coupled = false; // 默认解耦状态，需显式调用 couple() 进入联动模式
};