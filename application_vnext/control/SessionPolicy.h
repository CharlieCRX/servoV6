// ============================================================================
// SessionPolicy.h —— Phase 0：会话抽象 ISessionPolicy
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》§5.3。
//
// Abs/Rel/Jog/GantryLifecycle 的统一运行抽象；由 MotionControlService 唯一 tick。
// 必须支持「受控取消/停止」，否则 Stop、急停、失联、RevisionChanged 时仅从
// sessions_ 删除对象不足以安全终止（点动要写方向 OFF 并停心跳、定位要调 stop()、
// 龙门要进入明确的失败/取消态）。
//
// 纯抽象接口：不依赖 Qt / Modbus / 旧 domain/*。
// ============================================================================
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "application_vnext/control/OperationState.h"

namespace application_vnext::control {

/// 所有策略会话的统一运行抽象；由 MotionControlService 唯一 tick。
struct ISessionPolicy {
    virtual ~ISessionPolicy() = default;
    virtual void tick() = 0;

    // ---- 受控取消 / 停止 ----
    virtual void requestStop() = 0;               // 优雅停止（Stop/StopJog）
    virtual void cancel(std::string_view reason) = 0;  // 强制取消，保留原因供 UI/UDP
    virtual bool isStopping() const = 0;

    // ---- 运行状态 ----
    virtual bool isDone() const = 0;
    virtual bool hasError() const = 0;
    virtual std::string diag() const = 0;
    virtual std::string currentStepName() const = 0;

    // ---- 资源 ----
    virtual std::vector<ControlResource> resources() const = 0;  // 本会话占用的资源集合
};

}  // namespace application_vnext::control
