#ifndef AXIS_VIEW_MODEL_CORE_H
#define AXIS_VIEW_MODEL_CORE_H

#include <string>
#include "entity/Axis.h"
#include "policy/JogOrchestrator.h"
#include "policy/AutoAbsMoveOrchestrator.h"
#include "policy/AutoRelMoveOrchestrator.h"
#include "axis/StopAxisUseCase.h"

class AxisViewModelCore {
public:
    // 依赖注入：注入领域实体与策略编排器
    AxisViewModelCore(Axis& axis, 
                      JogOrchestrator& jogOrch, 
                      AutoAbsMoveOrchestrator& absOrch,
                      AutoRelMoveOrchestrator& relOrch,
                      StopAxisUseCase& stopUc);

    // --- 1. 状态投影 (State Projection) ---
    AxisState state() const;
    double absPos() const;
    double relPos() const;
    bool isEnabled() const;
    bool hasError() const;
    std::string errorMessage() const;

    double jogVelocity() const;
    double moveVelocity() const;

    double posLimit() const;
    double negLimit() const;

    // --- 2. 控制指令 (Control Inputs) ---
    void jogPositivePressed();
    void jogPositiveReleased();
    void jogNegativePressed();
    void jogNegativeReleased();
    
    void moveAbsolute(double targetPos);
    void moveRelative(double distance);
    void stop();

    void setJogVelocity(double v);
    void setMoveVelocity(double v);

    // --- 3. 驱动机制 (Tick) ---
    // 驱动 Orchestrator 状态机流转
    void tick();

private:
    Axis& m_axis;
    JogOrchestrator& m_jogOrch;
    AutoAbsMoveOrchestrator& m_absOrch;
    AutoRelMoveOrchestrator& m_relOrch;
    StopAxisUseCase& m_stopUc;
};

#endif // AXIS_VIEW_MODEL_CORE_H