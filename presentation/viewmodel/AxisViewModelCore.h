#ifndef AXIS_VIEW_MODEL_CORE_H
#define AXIS_VIEW_MODEL_CORE_H

#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <atomic>

#include "entity/Axis.h"
#include "entity/AxisId.h"
#include "ViewModelError.h"

class SystemManager;
class AutoAbsMoveOrchestrator;
class AutoRelMoveOrchestrator;
class JogOrchestrator;
class EnableUseCase;
class JogAxisUseCase;
class MoveAbsoluteUseCase;
class MoveRelativeUseCase;
class StopAxisUseCase;

struct ErrorEntry {
    ViewModelError error;
    std::chrono::steady_clock::time_point timestamp;
    std::string source;
};

class AxisViewModelCore {
public:
    AxisViewModelCore(SystemManager& manager,
                      const std::string& groupName,
                      AxisId axisId);
    ~AxisViewModelCore();

    // ── 状态投影 ──
    AxisState  state() const;
    double    absPos() const;
    double    relPos() const;
    bool      isEnabled() const;
    double    jogVelocity() const;
    double    moveVelocity() const;
    double    posLimit() const;
    double    negLimit() const;

    // ── 错误接口 ──
    bool hasError() const;
    ViewModelError lastError() const;
    size_t errorCount() const;
    std::vector<ViewModelError> allErrors() const;
    void acknowledgeError(size_t index);
    void clearAllErrors();
    void clearError();

    // ── 控制指令 ──
    void enable(bool active);
    void disable();
    void jog(Direction dir);
    void jogStop(Direction dir);
    void moveAbsolute(double targetPos);
    void moveRelative(double distance);
    void stop();
    void setJogVelocity(double v);
    void setMoveVelocity(double v);

    // ── 零位操作 ──
    void zeroAbsolutePosition();
    void setRelativeZero();
    void clearRelativeZero();

    // ── 帧驱动 ──
    void tick();

    // ── 辅助方法（供 Qt 层 / 外部日志使用）──
    static std::string axisIdToString(AxisId id);
    const std::string& groupName() const { return m_groupName; }
    AxisId axisId() const { return m_axisId; }

private:
    SystemManager& m_manager;
    std::string    m_groupName;
    AxisId         m_axisId;

    std::unique_ptr<EnableUseCase>         m_enableUc;
    std::unique_ptr<JogAxisUseCase>        m_jogUc;
    std::unique_ptr<MoveAbsoluteUseCase>   m_moveAbsUc;
    std::unique_ptr<MoveRelativeUseCase>   m_moveRelUc;
    std::unique_ptr<StopAxisUseCase>       m_stopUc;

    std::unique_ptr<JogOrchestrator>         m_jogOrch;
    std::unique_ptr<AutoAbsMoveOrchestrator>  m_absOrch;
    std::unique_ptr<AutoRelMoveOrchestrator>  m_relOrch;

    std::vector<ErrorEntry> m_errorHistory;

    void pushError(const ViewModelError& error, const std::string& source);

    template<typename Orch>
    void collectOrchError(Orch& orch, const std::string& source);

    void consumePendingCommands();

    static std::string generateTraceId();
    std::string logPrefix() const;
};

template<typename Orch>
void AxisViewModelCore::collectOrchError(Orch& orch, const std::string& source) {
    if (orch.hasError()) {
        auto vmError = translate(orch.lastError());
        if (vmError.isValid()) {
            m_errorHistory.push_back({
                .error = vmError,
                .timestamp = std::chrono::steady_clock::now(),
                .source = source
            });
        }
    }
}

#endif // AXIS_VIEW_MODEL_CORE_H
