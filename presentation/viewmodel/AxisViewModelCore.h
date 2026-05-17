#ifndef AXIS_VIEW_MODEL_CORE_H
#define AXIS_VIEW_MODEL_CORE_H

#include <memory>
#include <string>

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

/**
 * @brief 轴 ViewModel 核心（重构后）
 *
 * 职责：
 *   - 持有 SystemManager 引用，通过 groupName + axisId 定位目标轴
 *   - 内部创建所有 UseCase（值语义）和 Orchestrator（unique_ptr）
 *   - 控制指令自动经历：UseCase 执行 → 错误收集 → ErrorTranslator 翻译
 *   - tick() 驱动所有 Orchestrator 状态机 + 收集翻译错误
 *
 * 使用方式（main.cpp）：
 *   AxisViewModelCore vm(mgr, "Machine_A", AxisId::Y);
 *   vm.enable(true);   // 上电
 *   vm.jog(Direction::Forward);
 *   vm.tick();         // 每帧调用
 *   if (vm.hasError()) { auto e = vm.lastError(); ... }
 */
class AxisViewModelCore {
public:
    /**
     * @brief 构造 ViewModel
     * @param manager  系统管理器（用于分组路由和轴定位）
     * @param groupName 目标分组名称
     * @param axisId   目标轴 ID
     */
    AxisViewModelCore(SystemManager& manager,
                      const std::string& groupName,
                      AxisId axisId);

    ~AxisViewModelCore();

    // ========== 1. 状态投影（透传 Domain） ==========

    AxisState  state() const;
    double    absPos() const;
    double    relPos() const;
    bool      isEnabled() const;

    double    jogVelocity() const;
    double    moveVelocity() const;
    double    posLimit() const;
    double    negLimit() const;

    // ========== 2. 错误接口（替换旧 string 接口） ==========

    /**
     * @brief 是否有待处理错误（不清除）
     */
    bool hasError() const;

    /**
     * @brief 获取最后一个 ViewModelError
     */
    ViewModelError lastError() const;

    /**
     * @brief 清除当前错误
     */
    void clearError();

    // ========== 3. 控制指令 ==========

    void enable(bool active);
    void disable();

    void jog(Direction dir);
    void jogStop(Direction dir);

    void moveAbsolute(double targetPos);
    void moveRelative(double distance);
    void stop();

    void setJogVelocity(double v);
    void setMoveVelocity(double v);

    // ========== 4. 零位操作 ==========

    void zeroAbsolutePosition();
    void setRelativeZero();
    void clearRelativeZero();

    // ========== 5. 帧驱动 ==========

    /**
     * @brief 每帧调用：驱动所有 Orchestrator 状态机，收集并翻译错误
     *
     * 典型调用方式（main.cpp）：
     *   QTimer 每 10ms → vm.tick()
     */
    void tick();

private:
    // 定位信息
    SystemManager& m_manager;
    std::string    m_groupName;
    AxisId         m_axisId;

    // 内部创建的 UseCase（值语义）
    std::unique_ptr<EnableUseCase>         m_enableUc;
    std::unique_ptr<JogAxisUseCase>        m_jogUc;
    std::unique_ptr<MoveAbsoluteUseCase>   m_moveAbsUc;
    std::unique_ptr<MoveRelativeUseCase>   m_moveRelUc;
    std::unique_ptr<StopAxisUseCase>       m_stopUc;

    // 内部创建的 Orchestrator（unique_ptr，生命周期唯一）
    std::unique_ptr<JogOrchestrator>         m_jogOrch;
    std::unique_ptr<AutoAbsMoveOrchestrator>  m_absOrch;
    std::unique_ptr<AutoRelMoveOrchestrator>  m_relOrch;

    // 错误状态
    ViewModelError m_lastError = {};
    bool           m_hasError   = false;
};

#endif // AXIS_VIEW_MODEL_CORE_H
