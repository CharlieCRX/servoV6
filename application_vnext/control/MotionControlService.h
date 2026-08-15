// ============================================================================
// MotionControlService.h —— Phase 1：唯一控制协调器（命令入口 + 仲裁 + 会话 + 快照）
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》§5.1。
//
// UI / 摇杆 / UDP 是三个「命令来源」，只提交业务意图命令（不携带 PLC 地址）；
// PLC 反馈与应用会话状态是唯一「状态来源」。MotionControlService 是唯一协调者：
//   - 线程安全命令队列 + submit() 立即返回 Queued + operationId；
//   - tick() 每 20~50ms 唯一调度：取命令 -> 急停/停止优先 -> 读反馈与安全（经
//     IControlRuntime 每 tick 只读一次 runtime）-> 更新领域与全局锁定 -> 过期 ->
//     仲裁/执行 -> tick 会话 -> 发布快照；
//   - 组合根 SystemManagerVnext + AxisMotionApi + GantryMotionApi 均为私有，不外泄
//     可写入口，杜绝外部绕过仲裁直接写 PLC。
//
// 本头文件仅声明；实现放 MotionControlService.cpp。为保持头文件轻量，组合根以
// unique_ptr 持有（前向声明），具体类型在 .cpp 中引入。
// ============================================================================
#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <array>

#include "application_vnext/control/ControlCommand.h"
#include "application_vnext/control/ControlStateStore.h"
#include "application_vnext/control/IControlRuntime.h"
#include "application_vnext/control/OperationState.h"
#include "application_vnext/control/SessionPolicy.h"
#include "infrastructure/plc_vnext/contracts/ConnectionState.h"
#include "infrastructure/plc_vnext/contracts/RuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/SafetySnapshot.h"

namespace application_vnext::policy {
class AxisMotionApi;
class GantryMotionApi;
}  // namespace application_vnext::policy

namespace application_vnext {
class SystemManagerVnext;
}  // namespace application_vnext

namespace domain_vnext::gateway {
class IPlcDriver;
}  // namespace domain_vnext::gateway

namespace application_vnext::control {

/// 唯一控制协调器。见文件头注释与实施文档 §5.1。
class MotionControlService {
public:
    /// 注入领域驱动（组合根 SystemManagerVnext 的底层）与应用层运行时
    /// （safety / 连接 / 急停）。safety/连接/急停能力必须走 IControlRuntime，
    /// 不依赖 domain 的 IPlcDriver（后者没有这些能力）。
    MotionControlService(domain_vnext::gateway::IPlcDriver& driver,
                         IControlRuntime& runtime);
    ~MotionControlService();

    MotionControlService(const MotionControlService&) = delete;
    MotionControlService& operator=(const MotionControlService&) = delete;

    /// 命令入口（线程安全）：任何来源调用，立即返回 Queued + operationId。
    /// 状态先置 Queued；由唯一 tick 在仲裁后转为 Accepted/Rejected。
    std::string submit(ControlCommand cmd);

    /// 操作状态查询（UDP / UI 用）。
    std::optional<OperationEntry> queryOperation(const std::string& operationId) const;

    /// 唯一调度循环：取命令 -> 急停/停止优先 -> 读反馈/安全/连接（每 tick 只读一次
    /// runtime）-> 更新领域与全局锁定 -> 过期 -> 仲裁并启动 -> 推进会话 -> 发布快照。
    /// 见实施文档 §5.2。仲裁/执行/会话推进留 Phase 3。
    void tick();

    /// 快照存储（供 Qt ViewModel / UDP 查询，纯 C++、线程安全）。
    ControlStateStore& store() { return store_; }
    const ControlStateStore& store() const { return store_; }

    // ---- 测试钩子（Phase 1 断言用）----
    /// 当前全局锁定状态（boot + 首读可信后才释放）。
    bool globallyLocked() const { return globallyLocked_; }
    /// 尚未被 tick 取走的排队命令数（submit 后 tick 前为 1）。
    std::size_t queuedCount() const;

private:
    std::string nextOperationId(ControlSource src);
    void ensureBootedIfNeeded();   // 只读 topology（不读 runtime）；失败按指数退避重试

    void drainQueue(std::vector<ControlCommand>& out);
    void handleUrgent(const std::vector<ControlCommand>& cmds);  // 急停/停止 优先（读前）
    void readFeedbackAndSafety();          // 唯一一次 runtime+safety+连接（经 IControlRuntime）
    void updateDomainAndLock();            // 更新领域与全局锁定
    void expireCommands(std::vector<ControlCommand>& cmds);      // 处理已过期普通命令（TimedOut）
    void arbitrate(ControlCommand& cmd);   // 占用/抢占规则（多资源粒度）—— Phase 3
    void execute(ControlCommand& cmd);     // 落地到 Axis/Gantry Api —— Phase 3
    void tickSessions();                   // tick 所有进行中的会话 —— Phase 3
    void publishSnapshot();                // 写 ControlStateStore

    // ---- Phase 3 辅助：OperationEntry 回写 / 租约 / 会话终止 ----
    void setOpState(const std::string& id, OperationState st, std::string diag = {});
    void setOpMotion(const std::string& id, int16_t motionState, float position);
    bool leaseConflict(const std::vector<ControlResource>& res, const std::string& ownerOpId) const;
    void registerLease(const ControlCommand& cmd, const std::vector<ControlResource>& res);
    void releaseLeaseFor(const std::string& opId);
    void cancelAllSessions(const char* reason);
    void stopSessionsForTarget(const ControlCommand& cmd, bool ownerFiltered);
    void mirrorToChildren(const std::string& parentId, OperationState st, const std::string& diag);
    static bool isOneShotAction(ControlAction a);

    // ---- 组合根（不公开）：仅本类内部使用，由 arbitrate/execute 唯一触碰 ----
    std::unique_ptr<application_vnext::SystemManagerVnext>       sysManager_;
    std::unique_ptr<application_vnext::policy::AxisMotionApi>    axisApi_;
    std::unique_ptr<application_vnext::policy::GantryMotionApi>  gantryApi_;
    domain_vnext::gateway::IPlcDriver&                           driver_;   // 供 ensureBootedIfNeeded 读 topology
    IControlRuntime&                                             runtime_;

    // ---- 线程安全命令队列 ----
    mutable std::mutex        qMtx_;
    std::deque<ControlCommand> queue_;

    // ---- 操作条目（供 queryOperation / 快照）----
    std::map<std::string, OperationEntry> operations_;

    // ---- 租约与会话（资源粒度，一操作多资源；Phase 3 填充）----
    std::vector<OperationLease>                                   leases_;
    ResourceIndex                                                 resourceIndex_;
    std::map<std::string, std::shared_ptr<ISessionPolicy>>        sessions_;

    // ---- 安全 / 连接 / 全局锁定 ----
    plc_vnext::contracts::SafetySnapshot   lastSafety_;
    plc_vnext::contracts::ConnectionState  lastConn_;
    std::optional<plc_vnext::contracts::RuntimeSnapshot> lastRuntime_;  // 每 tick 唯一一份
    plc_vnext::contracts::TopologySnapshot lastTopo_;   // boot 成功后缓存，供仲裁 requiredResources
    // SetAbsTarget/SetRelTarget 预填缓存（键=(group, functionIdx)，值=[abs, rel]）。
    // 由 execute 记录、publishSnapshot 投影到快照 axis.absMoveTarget/relMoveTarget，
    // 供摇杆/UDP/UI 触发 Start*Move 时读取（见 §5.3「Set* 仅用于界面预填值」）。
    std::map<std::pair<int, int>, std::array<float, 2>> presetTargets_;
    bool globallyLocked_ = true;     // 初始锁定，boot 成功 + 首读可信后才释放
    bool bootOk_ = false;            // 经 bootFromTopology 成功初始化（topology 读取+校验通过）
    std::size_t bootRetryCount_ = 0; // 连续 boot 失败次数（指数退避用）
    std::chrono::steady_clock::time_point bootRetryDeadline_{};  // 退避期间不再尝试
    std::size_t idCounter_ = 0;      // nextOperationId 序号来源

    ControlStateStore store_;
};

}  // namespace application_vnext::control
