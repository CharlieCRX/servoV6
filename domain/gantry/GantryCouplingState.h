#pragma once

class GantryCouplingState {
public:
    enum class Status {
        Decoupled,
        CouplingRequested,
        Coupled
    };

    Status status() const { return m_status; }

    bool isCoupled() const { return m_status == Status::Coupled; }
    
    bool isCouplingRequested() const { return m_status == Status::CouplingRequested; }

    void requestCouple() { m_status = Status::CouplingRequested; }


    // @note feedback methods for testing purposes
    void applyCoupledFeedback() { m_status = Status::Coupled; }

    void applyDecoupledFeedback() { m_status = Status::Decoupled; }

private:
    Status m_status = Status::Decoupled;
};