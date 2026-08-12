// ============================================================================
// FeedbackDispatcher.h —— P4 system: RuntimeSnapshot -> 实体注入
// ============================================================================
// 设计稿 §5.1 反馈注入链路：
//   IPlcRuntimeGateway.readRuntime() -> RuntimeSnapshot (+AxisParameterSnapshot)
//     -> FeedbackDispatcher::dispatch(AxisSystem&, runtime, params)
//        for g in 0..1:
//            for (function, axis) in GroupModel[g]:
//                axis.applyFeedback(slotSnapshot)      // 前 7 项
//                axis.applyParameters(paramSnapshot)   // 8~13 项
//            GroupModel[g].gantryCoupling.applyFeedback(gantrySnapshot)  // 龙门状态
//        AxisSystem.safety().applyFeedback(estop)      // M224 读回
// 本实现按「已注册槽位」遍历注入（轴实体在全局 16 槽位注册表），龙门状态注入
// 到各组的联动状态机，急停反馈单独提供（M224 不在 RuntimeSnapshot 内，调用方
// 单独读回后调用 dispatchSafety）。
// 纯领域：只依赖自身 + model + state + plc_vnext::contracts（纯 DTO）。
// ============================================================================
#pragma once

#include <array>
#include <cstddef>

#include "domain_vnext/model/GantryStatus.h"
#include "domain_vnext/system/AxisRegistry.h"
#include "domain_vnext/system/AxisSystem.h"
#include "infrastructure/plc_vnext/contracts/AxisParameterSnapshot.h"
#include "infrastructure/plc_vnext/contracts/AxisRuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/GantryStatusSnapshot.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"
#include "infrastructure/plc_vnext/contracts/RuntimeSnapshot.h"

namespace domain_vnext::system {

class FeedbackDispatcher {
public:
    /// 注入运行反馈前 7 项（单槽位）。
    static void dispatchAxis(Axis& axis,
                             const plc_vnext::contracts::AxisRuntimeSnapshot& snap) {
        axis.applyFeedback(snap);
    }

    /// 注入参数区 8~13（单槽位）。
    static void dispatchParameters(
        Axis& axis, const plc_vnext::contracts::AxisParameterSnapshot& snap) {
        axis.applyParameters(snap);
    }

    /// 注入龙门状态 -> 该组联动状态机（GantryStatusSnapshot -> GantryStatusModel）。
    /// 同时把联动状态同步到组内各轴的 AxisStateMachine::gantry_（§5.1），否则
    /// 龙门同步轴（X1/X2/X 逻辑轴）的 requiresGantrySync 校验永远停在
    /// Unconfigured，导致运动意图被 RejectedGantryLocked 拒绝。
    static void dispatchGantry(
        GroupModel& gm, const plc_vnext::contracts::GantryStatusSnapshot& snap) {
        gm.gantryCoupling().applyFeedback(model::gantryStatusModelFromSnapshot(snap));
        const auto state = gm.gantryCoupling().state();
        for (const auto fn : gm.boundFunctions()) {
            if (Axis* a = gm.find(fn); a) {
                a->stateMachine().setGantryState(state);
            }
        }
    }

    /// 注入急停反馈（M224 读回）-> 全局安全状态机。首次调用完成同步。
    static void dispatchSafety(AxisSystem& sys, bool plcEmergencyStopped) {
        sys.safety().applyFeedback(plcEmergencyStopped);
    }

    /// 组合入口：从 RuntimeSnapshot 注入全部已注册轴（前 7 项）+ 各龙门组状态。
    static void dispatch(AxisSystem& sys,
                         const plc_vnext::contracts::RuntimeSnapshot& runtime) {
        for (const auto slot : sys.registry().occupiedSlots()) {
            Axis* axis = sys.registry().find(slot);
            if (!axis) continue;
            axis->applyFeedback(runtime.axes[static_cast<std::size_t>(slot.value())]);
        }
        for (const auto g : {plc_vnext::contracts::PlcGroupIndex(0),
                             plc_vnext::contracts::PlcGroupIndex(1)}) {
            dispatchGantry(sys.group(g),
                           runtime.gantry[static_cast<std::size_t>(g.value())]);
        }
    }

    /// 参数区注入（独立快照数组，需 plc_vnext contracts::AxisParameterSnapshot）。
    static void dispatchParameters(
        AxisSystem& sys,
        const std::array<plc_vnext::contracts::AxisParameterSnapshot,
                         plc_vnext::contracts::kRuntimeAxisCount>& params) {
        for (const auto slot : sys.registry().occupiedSlots()) {
            Axis* axis = sys.registry().find(slot);
            if (!axis) continue;
            axis->applyParameters(params[static_cast<std::size_t>(slot.value())]);
        }
    }
};

}  // namespace domain_vnext::system
