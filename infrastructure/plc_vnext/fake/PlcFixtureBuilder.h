// ============================================================================
// PlcFixtureBuilder.h —— Step 11 fake: 应用层 TDD 用的快照测试夹具
// ============================================================================
// 构造有效 / 无效拓扑快照与 16 槽位 + 龙门状态运行快照的测试夹具，供 application
// 层经 FakePlcRuntimeGateway 消费（readTopology / readRuntime 的输出）。它与
// tests/.../support 下的寄存器级 fixture（TopologyFixture / TelemetryFixture，
// 生成"待解码的原始 D 区字序列"）不同：本文件直接产出**已解码的 contracts DTO**，
// 不经过 Decoder/Validator/reader，因此 application 测试无需接触 Modbus 或解码。
//
// 约束（与 fake/ 目录边界一致）：只依赖 contracts 纯 DTO；不 include layout /
// transport / 旧 FakePLC / Domain。ABI 常量（Magic/Schema/组数/角色数）为已冻结
// 拓扑 ABI 的事实常量（与 layout::AxisTopologyLayout 及
// tools/plc_read_validate.py TOPOLOGY_* 完全一致），故以字面量写出并标注来源，
// 不在 fake 内引入布局层。
//
// 说明（Step 11.2）：脱敏基线基于"已冻结拓扑 ABI + 验证脚本字段"重建，语义与
// makeVersionedPlcTopologyDump 现场转储一致；真实 PLC 现场读数需在维护窗口用
// plc_read_validate.py 重新对拍后固化（见 TDD 文档 §11.2）。
// ============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "infrastructure/plc_vnext/contracts/AxisRuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/GantryStatusSnapshot.h"
#include "infrastructure/plc_vnext/contracts/RuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/SnapshotQuality.h"
#include "infrastructure/plc_vnext/contracts/TopologySnapshot.h"

namespace plc_vnext::fake {

// ---- 已冻结拓扑 ABI 事实常量（与 layout::AxisTopologyLayout 一致）----
/// Magic 0x013527C6（2026 现场与 PLC 协议约定值，已冻结）。
constexpr int32_t kFrozenTopologyMagic = static_cast<int32_t>(0x013527C6);
/// 当前支持（兼容）的最大 SchemaVersion（地址表 §5 已确认）。
constexpr int16_t kFrozenSchemaVersion = 1;
/// 组数量（A/B 两组，g=0..1）。
constexpr int kFixtureGroupCount = 2;
/// 角色 ABI 容量（ST_GroupAxisMap.Role[8]）。
constexpr int kFixtureRoleCount = 8;

/// 构造 A 组单个角色的 TopologyRole（Valid/HmiVisible 镜像为 valid）。
inline contracts::TopologyRole makeRole(bool valid, int16_t plcAxisIndex,
                                        int16_t motorNo, int32_t axisClass,
                                        int32_t unitType, int32_t motionMode) {
    contracts::TopologyRole r;
    r.valid = valid;
    r.hmiVisible = valid;
    r.plcAxisIndex = plcAxisIndex;
    r.motorNo = motorNo;
    r.axisClass = axisClass;
    r.unitType = unitType;
    r.motionMode = motionMode;
    return r;
}


/// 有效拓扑快照（干净基线）：Magic/Schema/Revision 正确、ConfigValid=true、
/// A 组有效（Role[0] X1 / Role[1] X2 / Role[5] SYN0），其余角色与 B 组为无效
/// 角色（PlcAxisIndex=-1）。与 makeValidTopologyRegisters 解码结果一致。
inline contracts::TopologySnapshot makeValidTopologySnapshot(int32_t revision = 0) {
    contracts::TopologySnapshot snap;
    snap.header.magic = kFrozenTopologyMagic;
    snap.header.schemaVersion = kFrozenSchemaVersion;
    snap.header.revision = revision;
    snap.header.configValid = true;
    snap.header.configErrorCode = 0;

    contracts::TopologyGroup ga;
    ga.valid = true;
    ga.hmiVisible = true;
    ga.groupCode = 0;
    ga.roles = {
        makeRole(true, 0, 1, 0, 0, 1),      // Role[0] X1（龙门X1）
        makeRole(true, 1, 2, 0, 0, 2),      // Role[1] X2（龙门X2）
        makeRole(false, -1, 0, 0, 0, 0),    // Role[2] 预留
        makeRole(false, -1, 0, 0, 0, 0),    // Role[3] 预留
        makeRole(false, -1, 0, 0, 0, 0),    // Role[4] 预留
        makeRole(true, 13, 0, 2, 0, 5),     // Role[5] SYN0（龙门逻辑轴，虚轴）
        makeRole(false, -1, 0, 0, 0, 0),    // Role[6] 预留
        makeRole(false, -1, 0, 0, 0, 0),    // Role[7] 预留
    };
    snap.groups.push_back(ga);

    contracts::TopologyGroup gb;  // B 组禁用
    gb.valid = false;
    gb.hmiVisible = false;
    gb.groupCode = 1;
    for (int i = 0; i < kFixtureRoleCount; ++i) {
        gb.roles.push_back(makeRole(false, -1, 0, 0, 0, 0));
    }
    snap.groups.push_back(gb);

    return snap;
}

/// 无效（配置未就绪）拓扑快照：可解码，但 ConfigValid=false、
/// ConfigErrorCode=0x08（PLC 标定未完成/拓扑未提交）。按设计 §11.2 语义，
/// 这属于快照内容，不属于 ReadResult 失败——Fake 仍以 success 返回，上层据此
/// 决定禁用运动，而不是把"配置未就绪"误当通讯失败。
inline contracts::TopologySnapshot makeInvalidTopologySnapshot(int32_t revision = 0) {
    contracts::TopologySnapshot snap = makeValidTopologySnapshot(revision);
    snap.header.configValid = false;
    snap.header.configErrorCode = 0x08;
    return snap;
}

/// 构造单个槽位的运行反馈快照（槽位下标 0..15；缺省可信）。
inline contracts::AxisRuntimeSnapshot makeAxisSnapshot(
    int slot, float manualSpeed = 0.0f, float positioningSpeed = 0.0f,
    float absPosition = 0.0f, float relPosition = 0.0f,
    int16_t motionState = 0, int16_t motionLimit = 0,
    uint16_t alarmWord = 0, bool trusted = true) {
    contracts::AxisRuntimeSnapshot a;
    a.slot = static_cast<int16_t>(slot);
    a.manualSpeed = manualSpeed;
    a.positioningSpeed = positioningSpeed;
    a.absPosition = absPosition;
    a.relPosition = relPosition;
    a.motionState = motionState;
    a.motionLimit = motionLimit;
    a.alarmWord = alarmWord;
    a.trusted = trusted;
    return a;
}

/// 构造单个龙门组的状态快照（组下标 0..1；缺省可信）。
inline contracts::GantryStatusSnapshot makeGantryStatusSnapshot(
    int g, int16_t state = 0, int32_t ackSeq = 0, int16_t commandResult = 0,
    bool x1InGear = false, bool x2InGear = false, bool trusted = true) {
    contracts::GantryStatusSnapshot s;
    s.state = state;
    s.ackSeq = ackSeq;
    s.commandResult = commandResult;
    s.x1InGear = x1InGear;
    s.x2InGear = x2InGear;
    s.readyToCouple = !x1InGear && !x2InGear;
    s.trusted = trusted;
    (void)g;  // 组号由调用方决定写回哪个槽位；本 helper 不持有组号字段
    return s;
}

/// 可信完整运行快照：16 槽位全部 trusted=true + 两个龙门组 trusted=true，
/// 整体 quality=Trusted。作为 application 层 readRuntime() 走通的最简基线。
/// 槽位 i 填充可辨识的值，便于断言"快照内容确实来自 Fake"。
inline contracts::RuntimeSnapshot makeTrustedRuntimeSnapshot() {
    contracts::RuntimeSnapshot snap;
    for (int i = 0; i < static_cast<int>(contracts::kRuntimeAxisCount); ++i) {
        snap.axes[static_cast<std::size_t>(i)] =
            makeAxisSnapshot(i, static_cast<float>(i) * 10.0f, 100.0f,
                             static_cast<float>(i) * 1000.0f, 0.0f,
                             /*motionState=*/1, /*motionLimit=*/0, 0);
    }
    // A 组已联动（State=3，AckSeq=9，CommandResult=2 成功，X1/X2 啮合）。
    snap.gantry[0] = makeGantryStatusSnapshot(0, /*state=*/3, /*ackSeq=*/9,
                                              /*commandResult=*/2,
                                              /*x1InGear=*/true,
                                              /*x2InGear=*/true);
    // B 组为缺省（可信但未配置）。
    snap.gantry[1] = makeGantryStatusSnapshot(1);
    snap.quality = contracts::SnapshotQuality::Trusted;
    return snap;
}

}  // namespace plc_vnext::fake

