// ============================================================================
// ShadowRunAssessor.h —— 阶段 2：只读影子运行锁定判定（纯函数）
// ============================================================================
// 依据《servoV6剩余迁移工作实施方案》§10.2“真实只读影子运行”通过标准，把
// “普通控制保持锁定”落为可测的纯函数判定。只读影子运行 / 启动同步期间调用：
// 只有以下全部成立才允许解除普通控制锁定：
//   - TCP 已连接；
//   - 拓扑读取成功且 PLC ConfigValid=true（配置无效 → 锁定）；
//     （Revision 变化由 readTopology 的 RevisionChanged 失败表达 → 锁定）
//   - 运行快照可信（readRuntime 成功即 quality==Trusted；部分失败/超时以
//     Transport 失败上报 → 锁定）；
//   - 急停状态已读取成功（急停状态未知 → 锁定，绝不当作“正常”）；
//   - 设备急停 M224=ON 时拒绝普通操作（锁定）。
// 本类为纯函数：无副作用、不写 PLC、不触碰 Domain 状态机；只做“影子运行可信
// 与否”的一次评估，供工具/启动链路展示与决策。急停解除接口不受本判定限制
// （本判定只管“普通控制”）。
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "infrastructure/plc_vnext/contracts/ConnectionState.h"
#include "infrastructure/plc_vnext/contracts/ReadResult.h"
#include "infrastructure/plc_vnext/contracts/RuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/SafetySnapshot.h"
#include "infrastructure/plc_vnext/contracts/TopologySnapshot.h"

namespace application_vnext {

class ShadowRunAssessor {
public:
    struct Decision {
        /// true = 普通控制保持锁定（不允许运动/龙门等普通操作）。
        bool lockOrdinaryControl = false;
        /// 导致锁定的原因（可聚合多条，用于日志/UI 提示）。
        std::vector<std::string> reasons;
    };

    using TopoResult = plc_vnext::contracts::ReadResult<plc_vnext::contracts::TopologySnapshot>;
    using RuntimeResult = plc_vnext::contracts::ReadResult<plc_vnext::contracts::RuntimeSnapshot>;
    using SafetyResult = plc_vnext::contracts::ReadResult<plc_vnext::contracts::SafetySnapshot>;

    /// 评估一次只读影子运行结果。纯函数：不改变任何状态、不发起任何 I/O。
    [[nodiscard]] static Decision evaluate(
        const plc_vnext::contracts::ConnectionState& conn,
        const TopoResult& topo, const RuntimeResult& runtime,
        const SafetyResult& safety) {
        Decision d;
        if (!conn.connected) {
            d.reasons.emplace_back("disconnected");
        }
        if (!topo.hasValue()) {
            // Revision 变化是独立原因（需重新 boot / 安全锁定），与普通传输失败区分。
            d.reasons.emplace_back(
                topo.failureKind() == TopoResult::FailureKind::RevisionChanged
                    ? "revision_changed"
                    : "topology_read_failed");
        } else if (!topo.value().header.configValid) {
            d.reasons.emplace_back("config_invalid");
        }
        if (!runtime.hasValue()) {
            d.reasons.emplace_back("runtime_untrusted");
        }
        if (!safety.hasValue()) {
            d.reasons.emplace_back("safety_unknown");
        } else if (safety.value().emergencyStop) {
            d.reasons.emplace_back("emergency_stop");
        }
        d.lockOrdinaryControl = !d.reasons.empty();
        return d;
    }
};

}  // namespace application_vnext
