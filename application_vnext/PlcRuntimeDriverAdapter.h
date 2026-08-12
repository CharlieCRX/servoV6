// ============================================================================
// PlcRuntimeDriverAdapter.h —— P5 app: gateway::IPlcDriver 的适配器实现
// ============================================================================
// 实现 domain_vnext::gateway::IPlcDriver（领域侧读/写门面抽象），把每个方法
// 委托给 plc_vnext::IPlcRuntimeGateway（Step 10 高层组合门面）。测试用
// plc_vnext::fake::FakePlcRuntimeGateway 注入（实现同一接口）。
//   - readTopology / readRuntime：原样转发（只读）。
//   - writeAxis：原样转发（单轴写入意图）。
//   - submitGantryRequest：委托 Detailed 入口，返回其底层通讯结果
//     （提交阶段信息保留在 GantrySubmitResult，见 AppVnextError）。
// 依赖方向：application_vnext -> domain_vnext/gateway + plc_vnext。
// ============================================================================
#pragma once

#include "domain_vnext/gateway/IPlcDriver.h"
#include "infrastructure/plc_vnext/IPlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/contracts/GantryRequest.h"
#include "infrastructure/plc_vnext/contracts/GantrySubmitResult.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "infrastructure/plc_vnext/contracts/PlcCommand.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"
#include "infrastructure/plc_vnext/contracts/ReadResult.h"
#include "infrastructure/plc_vnext/contracts/RuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/TopologySnapshot.h"

namespace application_vnext {

/// 领域驱动门面 -> plc_vnext::IPlcRuntimeGateway 的薄适配器。
class PlcRuntimeDriverAdapter : public domain_vnext::gateway::IPlcDriver {
public:
    explicit PlcRuntimeDriverAdapter(plc_vnext::IPlcRuntimeGateway& gw) : gw_(&gw) {}

    plc_vnext::contracts::ReadResult<plc_vnext::contracts::TopologySnapshot>
    readTopology() override {
        return gw_->readTopology();
    }

    plc_vnext::contracts::ReadResult<plc_vnext::contracts::RuntimeSnapshot>
    readRuntime() override {
        return gw_->readRuntime();
    }

    plc_vnext::contracts::CommunicationResult writeAxis(
        plc_vnext::contracts::PlcAxisSlot slot,
        const plc_vnext::contracts::PlcAxisCommand& cmd) override {
        return gw_->writeAxis(slot, cmd);
    }

    plc_vnext::contracts::GantrySubmitResult submitGantryRequest(
        plc_vnext::contracts::PlcGroupIndex g,
        const plc_vnext::contracts::GantryRequest& req) override {
        return gw_->submitGantryRequestDetailed(g, req);
    }

private:
    plc_vnext::IPlcRuntimeGateway* gw_;
};

}  // namespace application_vnext
