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

/**
 * @brief 错误历史条目
 *
 * 每条错误记录包含翻译后的 ViewModelError、发生时间戳和来源标记。
 * 支持按时间排序、按索引确认。
 */
struct ErrorEntry {
    ViewModelError error;
    std::chrono::steady_clock::time_point timestamp;
    std::string source;  // "JogOrch" / "AbsOrch" / "RelOrch" / "EnableUC" / "StopUC" / "ZeroAbsOp" / ...
};

/**
 * @brief 轴 ViewModel 核心（重构后 v2）
 *
 * 职责：
 *   - 持有 SystemManager 引用，通过 groupName + axisId 定位目标轴
 *   - 内部创建所有 UseCase（值语义）和 Orchestrator（unique_ptr）
 *   - 控制指令自动经历：UseCase 执行 → 错误收集 → ErrorTranslator 翻译
 *   - tick() 驱动所有 Orchestrator 状态机 + 收集翻译错误（列表模式）
 *   - 全操作入口日志追踪（TraceScope + LOG_INFO）
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

    // ========== 2. 错误接口（列表收集模式） ==========

    /**
     * @brief 是否有待处理错误（不清除）
     */
    bool hasError() const;

    /**
     * @brief 获取最后一条 ViewModelError
     */
    ViewModelError lastError() const;

    /**
     * @brief 获取错误总数
     */
    size_t errorCount() const;

    /**
     * @brief 获取全部未确认错误的副本
     */
    std::vector<ViewModelError> allErrors() const;

    /**
     * @brief 确认单条错误（按索引移除）
     */
    void acknowledgeError(size_t index);

    /**
     * @brief 确认所有错误
     */
    void clearAllErrors();

    /**
     * @brief 清除当前错误（保留兼容性，内部调用 clearAllErrors）
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

    // 错误列表（替换旧的 m_lastError + m_hasError）
    std::vector<ErrorEntry> m_errorHistory;

    // ===== 内部辅助方法 =====

    /**
     * @brief 向错误历史追加一条错误
     * @param error  翻译后的 ViewModelError
     * @param source 来源标记字符串
     */
    void pushError(const ViewModelError& error, const std::string& source);

    /**
     * @brief 从编排器收集错误（追加模式）
     * @tparam Orch 编排器类型
     * @param orch   编排器实例
     * @param source 来源标记字符串
     */
    template<typename Orch>
    void collectOrchError(Orch& orch, const std::string& source);

    /**
     * @brief 消费零位/速度类 pending command
     *
     * 零位/速度操作没有独立 Orchestrator，命令暂存在 Axis 中，
     * 在 tick() 中消费并发送到 driver。
     */
    void consumePendingCommands();

    /**
     * @brief 获取 groupName
     */
    const std::string& groupName() const { return m_groupName; }

    // ===== TraceScope / 日志辅助方法 =====

    /**
     * @brief 生成唯一追踪 ID（基于时间戳 + 原子序列号）
     */
    static std::string generateTraceId();

    /**
     * @brief AxisId → 字符串表示（无硬编码 "Y"/"Z"）
     */
    static std::string axisIdToString(AxisId id);

    /**
     * @brief 构建标准日志前缀 [group]/[axis]
     */
    std::string logPrefix() const;
};

// ===== 模板方法实现（必须在头文件中） =====

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
