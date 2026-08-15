// ============================================================================
// SessionAdapter.h —— Phase 3：具体运动策略 -> ISessionPolicy 会话适配器
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》§5.3 与 Phase 3。
//
// MotionControlService 通过适配器把 AbsMovePolicy / RelMovePolicy / JogPolicy /
// GantryLifecyclePolicy 包成 ISessionPolicy，统一收进 sessions_，屏蔽各策略差异：
//   - requestStop()：优雅停止（Jog 走 requestStop；定位写 stop() 让 PLC 自然回 idle）；
//   - cancel(reason) ：强制取消并保留原因（急停 / 失联 / Revision 变化 / 全局锁定）；
//   - resources()    ：本会话占用的资源集合（租约/互斥用）；
//   - tick()/isDone()/hasError()/diag()/currentStepName()：唯一 tick 推进与终结判定。
//
// 只依赖 application_vnext + domain_vnext + plc_vnext contracts，无 Qt / Modbus。
// ============================================================================
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "application_vnext/control/ControlCommand.h"
#include "application_vnext/control/OperationState.h"
#include "application_vnext/control/SessionPolicy.h"
#include "application_vnext/policy/AbsMovePolicy.h"
#include "application_vnext/policy/GantryLifecyclePolicy.h"
#include "application_vnext/policy/JogPolicy.h"
#include "application_vnext/policy/RelMovePolicy.h"
#include "application_vnext/SystemManagerVnext.h"
#include "domain_vnext/model/AxisFunction.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"

namespace application_vnext::control::session_adapter {

// 策略类位于 application_vnext::policy，此处收敛便于本层统一引用。
using application_vnext::policy::AbsMovePolicy;
using application_vnext::policy::RelMovePolicy;
using application_vnext::policy::JogPolicy;
using application_vnext::policy::GantryLifecyclePolicy;

/// 会话公共底座：持有 operationId / owner / target / resources / kind，
/// 以及「受控停止/取消」的通用状态。具体策略差异由子类收敛。
class SessionBase : public ISessionPolicy {
public:
    SessionBase(std::string opId, ControlSource owner, AxisTarget target,
                std::vector<ControlResource> res, OperationKind kind)
        : opId_(std::move(opId)), owner_(owner), target_(target),
          resources_(std::move(res)), kind_(kind) {}

    // ---- ISessionPolicy：公共实现 ----
    bool isStopping() const override { return stopping_; }
    std::vector<ControlResource> resources() const override { return resources_; }

    // ---- 供协调层查询 ----
    const std::string& operationId() const { return opId_; }
    ControlSource owner() const { return owner_; }
    AxisTarget target() const { return target_; }
    OperationKind kind() const { return kind_; }
    const std::string& cancelReason() const { return cancelReason_; }

protected:
    std::string opId_;
    ControlSource owner_;
    AxisTarget target_;
    std::vector<ControlResource> resources_;
    OperationKind kind_;
    bool stopping_ = false;        // 已请求停止/取消（终止后 OperationEntry -> Cancelled）
    std::string cancelReason_;
};

/// 定位会话（绝对 / 相对）：requestStop / cancel 写一次 stop()（自复位脉冲），
/// PLC 停止运动后策略自然走到 Done；不用后台线程，完全由唯一 tick 驱动。
template <typename Policy>
class PositioningSession : public SessionBase {
public:
    PositioningSession(SystemManagerVnext& m, Policy p, std::string opId,
                       ControlSource owner, AxisTarget target,
                       std::vector<ControlResource> res, OperationKind kind)
        : SessionBase(std::move(opId), owner, target, std::move(res), kind),
          m_(&m), policy_(std::move(p)) {}

    void tick() override { policy_.tick(); }

    void requestStop() override {
        if (policy_.isDone() || policy_.hasError()) return;
        issueStop();
        stopping_ = true;
    }
    void cancel(std::string_view reason) override {
        if (policy_.isDone() || policy_.hasError()) return;
        issueStop();
        stopping_ = true;
        cancelReason_ = std::string(reason);
    }

    bool isDone() const override { return policy_.isDone(); }
    bool hasError() const override { return policy_.hasError(); }
    std::string diag() const override { return policy_.diag(); }
    std::string currentStepName() const override {
        return Policy::stepName(policy_.currentStep());
    }

private:
    void issueStop() {
        if (stopIssued_) return;
        stopIssued_ = true;
        m_->stop(target_.group, target_.function);
    }

    SystemManagerVnext* m_;
    Policy policy_;
    bool stopIssued_ = false;
};

/// 点动会话：requestStop / cancel 委托给 JogPolicy::requestStop()
/// （方向 OFF + 心跳 OFF + 掉电，均为策略内收尾，无后台线程）。
class JogSession : public SessionBase {
public:
    JogSession(JogPolicy p, std::string opId, ControlSource owner, AxisTarget target,
               std::vector<ControlResource> res, OperationKind kind)
        : SessionBase(std::move(opId), owner, target, std::move(res), kind),
          policy_(std::move(p)) {}

    void tick() override { policy_.tick(); }

    void requestStop() override {
        if (policy_.isDone() || policy_.hasError()) return;
        policy_.requestStop();
        stopping_ = true;
    }
    void cancel(std::string_view reason) override {
        if (policy_.isDone() || policy_.hasError()) return;
        policy_.requestStop();
        stopping_ = true;
        cancelReason_ = std::string(reason);
    }

    bool isDone() const override { return policy_.isDone(); }
    bool hasError() const override { return policy_.hasError(); }
    std::string diag() const override { return policy_.diag(); }
    std::string currentStepName() const override {
        return JogPolicy::stepName(policy_.currentStep());
    }

private:
    JogPolicy policy_;
};

/// 龙门生命周期会话：具体 GantryLifecyclePolicy 无 requestStop/cancel 原语。
/// 优雅停止由「解除（Decouple）」流程自然完成；强制取消在此仅记录原因，
/// 让生命周期策略自行推进到 Done/Error（龙门急停收口留 Phase 7）。
class GantryLifecycleSession : public SessionBase {
public:
    GantryLifecycleSession(GantryLifecyclePolicy p, std::string opId,
                           ControlSource owner, AxisTarget target,
                           std::vector<ControlResource> res, OperationKind kind)
        : SessionBase(std::move(opId), owner, target, std::move(res), kind),
          policy_(std::move(p)) {}

    void tick() override { policy_.tick(); }

    void requestStop() override {
        policy_.cancel();   // 停止 = 生命周期安全取消（急停/断线同样走 cancel）
        stopping_ = true;
    }
    void cancel(std::string_view reason) override {
        policy_.cancel();
        stopping_ = true;
        cancelReason_ = std::string(reason);
    }

    bool isDone() const override {
        // 龙门生命周期成功终态：couple 停在 Ready（isReady()，经 PLC 侧
        // State=3/Step=80/CommandResult=2 确认）、decouple 停在 Done（isDone()）。
        // 二者都是「操作成功结束」：必须让 tickSessions() 据此释放 gantry:A:0 租约并
        // 移除会话，否则 couple 会话会永久停在 Running、租约永不释放（Phase 7）。
        return policy_.isDone() || policy_.isReady();
    }
    bool hasError() const override { return policy_.hasError(); }
    std::string diag() const override { return policy_.diag(); }
    std::string currentStepName() const override {
        return GantryLifecyclePolicy::stepName(policy_.currentStep());
    }

private:
    GantryLifecyclePolicy policy_;
};

}  // namespace application_vnext::control::session_adapter
