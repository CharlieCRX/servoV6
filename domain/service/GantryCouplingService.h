#pragma once

#include "../entity/GantrySystem.h"
#include "../value/CouplingCondition.h"
#include "../value/PositionConsistency.h"
#include "../value/GantryMode.h"
#include <string>
#include <sstream>

/**
 * @file GantryCouplingService.h
 * @brief 龙门联动管理领域服务
 *
 * 职责：
 *   - 封装联动申请/解联的完整流程
 *   - 提供条件诊断（validateConditions）
 *   - 联动维持检查（checkSyncMaintenance）
 *
 * 所有操作委托给聚合根 GantrySystem。
 *
 * 覆盖约束：
 *   约束12 — 联动是"状态申请"
 *   约束13 — 联动建立条件
 *   约束14 — 联动期间持续约束
 */

/// 耦合条件诊断快照
struct CouplingConditionsSnapshot {
    bool x1Enabled = false;
    bool x2Enabled = false;
    bool noAlarm = false;
    bool noLimit = false;
    bool positionConsistent = false;

    bool allSatisfied() const {
        return x1Enabled && x2Enabled && noAlarm && noLimit && positionConsistent;
    }

    std::string unsatisfiedReasons() const {
        std::ostringstream oss;
        if (!x1Enabled)           oss << "X1 not enabled; ";
        if (!x2Enabled)           oss << "X2 not enabled; ";
        if (!noAlarm)             oss << "Alarm active; ";
        if (!noLimit)             oss << "Limit triggered; ";
        if (!positionConsistent)  oss << "Position deviation exceeds epsilon; ";
        return oss.str();
    }
};

class GantryCouplingService {
public:
    /**
     * @brief 申请联动
     *
     * 委托给 GantrySystem::requestCoupling()，
     * 后者内部调用 CouplingCondition::checkAll() 验证所有条件。
     *
     * @return SafetyCheckResult — allowed 表示联动已建立
     */
    SafetyCheckResult requestCoupling(GantrySystem& system) {
        auto result = system.requestCoupling();
        if (result.allowed) {
            return SafetyCheckResult::allowed();
        }
        // 将 CouplingCondition::Result 转换为 SafetyCheckResult
        return SafetyCheckResult(
            SafetyCheckResult::Verdict::Rejected_Alarm,
            result.failReason
        );
    }

    /**
     * @brief 申请解联
     */
    void requestDecoupling(GantrySystem& system, const std::string& reason = "") {
        system.requestDecoupling(reason);
    }

    /**
     * @brief 联动维持检查
     *
     * 委托给 GantrySystem::checkSyncMaintenance()，
     * 若偏差超限则自动退出联动并发布 DeviationFault 事件。
     *
     * @return CouplingCondition::Result
     */
    CouplingCondition::Result checkSyncMaintenance(GantrySystem& system) {
        bool ok = system.checkSyncMaintenance();
        CouplingCondition::Result r;
        r.allowed = ok;
        if (!ok) {
            double dev = PositionConsistency::computeDeviation(
                system.x1().position(), system.x2().position()
            );
            std::ostringstream oss;
            oss << "Position deviation " << dev << " exceeds epsilon";
            r.failReason = oss.str();
        }
        return r;
    }

    /**
     * @brief 获取当前所有联动条件的诊断快照
     *
     * 纯查询，无副作用。
     */
    CouplingConditionsSnapshot validateConditions(const GantrySystem& system) const {
        CouplingConditionsSnapshot snap;
        snap.x1Enabled = system.x1().isEnabled();
        snap.x2Enabled = system.x2().isEnabled();
        snap.noAlarm = !(system.x1().isAlarmed() || system.x2().isAlarmed());
        snap.noLimit = !(system.x1().isAnyLimitActive() || system.x2().isAnyLimitActive());
        snap.positionConsistent = PositionConsistency::isConsistent(
            system.x1().position(), system.x2().position()
        );
        return snap;
    }
};
