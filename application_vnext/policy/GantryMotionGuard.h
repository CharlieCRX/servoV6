// ============================================================================
// GantryMotionGuard.h —— 龙门逻辑轴（slot13）运动许可判定
// ============================================================================
// 龙门联动成功后，slot 13 以"单轴"名义被调用，但其运动必须在 PLC 明确开放
// 逻辑控制后才是安全的。领域 AxisStateMachine 只在 Unconfigured 时拒绝运动，
// 不足以作为龙门保护（见 AxisStateMachine::submit 的龙门同步检查），因此由本
// guard 承担严格准入：建立完成需 State=3 / InternalStep=80 / CommandResult=2 /
// CommandErrorCode=0 / !Fault / X1InGear / X2InGear / LogicalControlAllowed /
// !MemberControlAllowed / 状态可信 / 系统未急停。
// 数据来源：SystemManagerVnext::gantryStatus(g)（poll 缓存的完整 GantryStatusModel），
// 不依赖领域状态机（其不保留 InternalStep / InGear 等详细字段）。
// 纯 C++：只依赖 application_vnext + domain_vnext model + plc_vnext contracts。
// ============================================================================
#pragma once

#include "application_vnext/SystemManagerVnext.h"
#include "domain_vnext/model/GantryStatus.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"

namespace application_vnext::policy {

/// guard 判定结果：allowed=false 时 reason 给出不可运动的可读原因。
struct GantryGuardResult {
    bool allowed = false;
    const char* reason = "";
};

/// 逻辑轴运动许可守卫。不可变绑定一个组；运行中每 tick 调用 evaluate()。
class GantryMotionGuard {
public:
    GantryMotionGuard(SystemManagerVnext& m, plc_vnext::contracts::PlcGroupIndex g)
        : m_(&m), g_(g) {}

    /// 是否允许逻辑轴运动。allowed=false 时 reason 说明具体不满足项。
    GantryGuardResult evaluate() const;

private:
    SystemManagerVnext* m_;
    plc_vnext::contracts::PlcGroupIndex g_;
};

inline GantryGuardResult GantryMotionGuard::evaluate() const {
    const auto& st = m_->gantryStatus(g_);
    if (!st.trusted)                     return {false, "gantry status not trusted"};
    if (m_->isSystemLocked())            return {false, "system safety locked"};
    if (st.rawState != 3)                return {false, "gantry not coupled (state!=3)"};
    if (st.internalStep != 80)           return {false, "gantry internalStep!=80"};
    if (st.commandResult != 2)           return {false, "gantry commandResult!=2"};
    if (st.commandErrorCode != 0)        return {false, "gantry commandErrorCode!=0"};
    if (st.fault)                        return {false, "gantry fault"};
    if (!st.x1InGear)                    return {false, "X1 not in gear"};
    if (!st.x2InGear)                    return {false, "X2 not in gear"};
    if (!st.logicalControlAllowed)       return {false, "logical control not allowed"};
    if (st.memberControlAllowed)         return {false, "member control still allowed"};
    return {true, ""};
}

}  // namespace application_vnext::policy
