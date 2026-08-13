// ============================================================================
// PlcAxisCommandWriter.cpp —— Step 8 command: 单轴命令编码与写入实现
// ============================================================================
#include "infrastructure/plc_vnext/command/PlcAxisCommandWriter.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "infrastructure/plc_vnext/codec/EndianPolicy.h"
#include "infrastructure/plc_vnext/codec/RegisterCodec.h"
#include "infrastructure/plc_vnext/layout/AxisSlotCoilLayout.h"
#include "infrastructure/plc_vnext/layout/AxisSlotRegisterLayout.h"

namespace plc_vnext::command {
namespace {

// 《PLC变量协议_Modbus最终地址表.md》§2.3/§3.1：REAL/DINT 采用低字在低地址
// （汇川 CDAB = BigEndian + LowWordFirst）。写入字序与 plc_read_validate.py
// 的自测断言完全一致：1.0f → {0x0000, 0x3F80}、25.8f → {0x6666, 0x41CE}。
constexpr codec::EndianPolicy kPlcEndian{codec::ByteOrder::BigEndian,
                                         codec::WordOrder::LowWordFirst};

}  // namespace

PlcAxisCommandWriter::PlcAxisCommandWriter(transport::IModbusClientPtr client)
    : m_client(std::move(client)) {}

contracts::CommunicationResult PlcAxisCommandWriter::write(
    contracts::PlcAxisSlot slot, const contracts::PlcAxisCommand& cmd) {
    using contracts::PlcAxisCommandKind;
    const int s = slot.value();

    switch (cmd.kind) {
        // ---- 保持寄存器参数写（REAL）----
        case PlcAxisCommandKind::SetManualSpeed:
            return writeParameter(layout::manualSpeed(s), cmd.realValue);
        case PlcAxisCommandKind::SetPositioningSpeed:
            return writeParameter(layout::positioningSpeed(s), cmd.realValue);
        case PlcAxisCommandKind::SetAbsTarget:
            return writeParameter(layout::absPosTarget(s), cmd.realValue);
        case PlcAxisCommandKind::SetRelTarget:
            return writeParameter(layout::relPosTarget(s), cmd.realValue);

        // ---- 保持电平线圈 ----
        case PlcAxisCommandKind::EnableAxis:
            return writeLevelCoil(layout::enableAxis(s), cmd.boolValue);
        case PlcAxisCommandKind::EnableMotor:
            return writeLevelCoil(layout::enableMotor(s), cmd.boolValue);
        case PlcAxisCommandKind::JogForward:
            return writeLevelCoil(layout::jogForward(s), cmd.boolValue);
        case PlcAxisCommandKind::JogBackward:
            return writeLevelCoil(layout::jogBackward(s), cmd.boolValue);
        case PlcAxisCommandKind::JogHeartbeat:
            // 心跳写 ON/OFF：PLC 每扫描周期清 OFF，因此周期写 ON 维持；停止时
            // 补写 OFF 便于状态收尾与测试确认（§阶段3 Step 3.1）。
            return writeLevelCoil(layout::jogHeartbeat(s), cmd.boolValue);

        // ---- PLC 自复位（只写 ON；PLC 自动复位读回 OFF）----
        case PlcAxisCommandKind::TriggerAbsMove:
            return writeSelfReset(layout::triggerAbsMove(s));
        case PlcAxisCommandKind::TriggerRelMove:
            return writeSelfReset(layout::triggerRelMove(s));
        case PlcAxisCommandKind::StopAbsMove:
            return writeSelfReset(layout::stopAbsMove(s));
        case PlcAxisCommandKind::StopRelMove:
            return writeSelfReset(layout::stopRelMove(s));
        case PlcAxisCommandKind::ClearAbsPosition:
            return writeSelfReset(layout::clearAbsPosition(s));
        case PlcAxisCommandKind::ClearRelZero:
            return writeSelfReset(layout::clearRelZero(s));
        case PlcAxisCommandKind::SetRelZero:
            return writeSelfReset(layout::setRelZero(s));
    }
    return contracts::CommunicationResult{
        contracts::CommunicationResult::Status::ProtocolError, 0,
        "PlcAxisCommandWriter: unknown PlcAxisCommandKind"};
}

contracts::CommunicationResult PlcAxisCommandWriter::writeParameter(
    layout::HoldingAddress base, float value) {
    const std::vector<uint16_t> words =
        codec::RegisterCodec::encodeFloat(value, kPlcEndian);
    return m_client->writeMultipleRegisters(static_cast<uint16_t>(base.value()), words);
}

contracts::CommunicationResult PlcAxisCommandWriter::writeLevelCoil(
    layout::CoilAddress addr, bool value) {
    return m_client->writeSingleCoil(static_cast<uint16_t>(addr.value()), value);
}

contracts::CommunicationResult PlcAxisCommandWriter::writeSelfReset(
    layout::CoilAddress addr) {
    return m_client->writeSingleCoil(static_cast<uint16_t>(addr.value()), true);
}

}  // namespace plc_vnext::command
