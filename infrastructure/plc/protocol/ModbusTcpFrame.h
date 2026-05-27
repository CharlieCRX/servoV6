// infrastructure/plc/protocol/ModbusTcpFrame.h
// P4 Phase 1 — Modbus TCP 帧封包/解包工具（纯静态函数，零依赖）
//
// 职责:
//   - 构建完整的 Modbus TCP 请求帧（MBAP 头 + PDU）
//   - 解析 Modbus TCP 响应帧（MBAP 头校验 + PDU 数据提取 + 异常检测）
//
// 设计原则:
//   - 纯静态函数，无状态，无 I/O — 100% 可单元测试
//   - 只处理字节级别的拼接/解析，不理解业务语义
//   - 放在 plc::protocol::tcp 子命名空间，避免污染 plc::protocol
//   - 使用 Big-Endian 字节序（Modbus 协议规范）

#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace plc::protocol::tcp {

/**
 * @brief Modbus TCP 帧封包/解包工具（纯静态函数）
 *
 * @details
 * MBAP 报文头结构（7 字节）:
 *   ┌──────────┬──────────┬──────────┬─────────┐
 *   │ Trans ID │ Proto ID │ Length   │ Unit ID │
 *   │  2 bytes │  2 bytes │  2 bytes │ 1 byte  │
 *   └──────────┴──────────┴──────────┴─────────┘
 *
 * - Transaction ID: 请求-响应配对标识（由调用方管理单调递增）
 * - Protocol ID:    Modbus = 0x0000
 * - Length:         后续字节数（Unit ID + FC + Data），不含自身
 * - Unit ID:        设备地址
 *
 * 防坑要点:
 *   - 所有多字节值使用 Big-Endian 字节序
 *   - Length = UnitID(1) + FC(1) + Data(N)，不含 MBAP 自身的 7 字节
 */
struct ModbusTcpFrame {

    /// @brief MBAP 头解析结果
    struct MbapHeader {
        uint16_t transactionId = 0; ///< 事务 ID（请求-响应配对）
        uint16_t length        = 0; ///< 后续字节数（Unit ID + FC + Data）
    };

    // ═══════════════════════════════════════════════
    //  内部工具: Big-Endian 字节写
    // ═══════════════════════════════════════════════

    /// @brief 向 buffer 指定位置写入一个 Big-Endian uint16_t
    /// @param buf  目标缓冲区
    /// @param pos  写入位置（字节偏移）
    /// @param val  要写入的值
    static void writeUint16BE(std::vector<uint8_t>& buf, size_t pos, uint16_t val) {
        buf[pos]     = static_cast<uint8_t>((val >> 8) & 0xFF);
        buf[pos + 1] = static_cast<uint8_t>(val & 0xFF);
    }

    /// @brief 向 buffer 追加一个 Big-Endian uint16_t
    /// @param buf  目标缓冲区
    /// @param val  要追加的值
    static void appendUint16BE(std::vector<uint8_t>& buf, uint16_t val) {
        buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
        buf.push_back(static_cast<uint8_t>(val & 0xFF));
    }

    // ═══════════════════════════════════════════════
    //  内部工具: MBAP 头构建
    // ═══════════════════════════════════════════════

    /// @brief 向 buffer 开头写入 MBAP 头（假定 buf 已预先分配 7 字节空位）
    /// @param buf    目标缓冲区（大小必须 ≥ 7）
    /// @param tid    事务 ID
    /// @param unitId 单元 ID
    /// @param pduLen PDU 长度（仅 FC + Data，不含 Unit ID）
    ///
    /// MBAP Length 字段 = UnitID(1) + PDU(pduLen)
    static void writeMbap(std::vector<uint8_t>& buf, uint16_t tid, uint8_t unitId, uint16_t pduLen) {
        writeUint16BE(buf, 0, tid);              // Transaction ID
        writeUint16BE(buf, 2, 0x0000);           // Protocol ID = 0x0000
        writeUint16BE(buf, 4, pduLen + 1);       // Length = UnitID(1) + PDU
        buf[6] = unitId;                         // Unit ID
    }

    // ═══════════════════════════════════════════════
    //  封包 — 构建完整 Modbus TCP 请求帧
    // ═══════════════════════════════════════════════

    /// @brief 构建 FC01 请求帧（读线圈）
    /// @param tid       事务 ID
    /// @param unitId    单元 ID
    /// @param startAddr 起始线圈地址
    /// @param count     线圈数量
    /// @return 完整帧字节序列（MBAP + PDU）
    ///
    /// 帧格式: MBAP | 01 | StartAddr(2B BE) | Quantity(2B BE)
    /// PDU Len = FC(1) + StartAddr(2) + Quantity(2) = 5
    static std::vector<uint8_t> buildReadCoils(
        uint16_t tid, uint8_t unitId,
        uint16_t startAddr, uint16_t count)
    {
        constexpr uint16_t pduLen = 1 + 2 + 2; // FC + StartAddr + Quantity = 5
        std::vector<uint8_t> frame(7 + pduLen); // MBAP(7) + PDU(5) = 12
        writeMbap(frame, tid, unitId, pduLen);
        frame[7] = 0x01;                            // PDU: FC = 01 Read Coils
        writeUint16BE(frame, 8, startAddr);         // PDU: Start Address
        writeUint16BE(frame, 10, count);            // PDU: Quantity
        return frame;
    }

    /// @brief 构建 FC03 请求帧（读保持寄存器）
    /// @param tid       事务 ID
    /// @param unitId    单元 ID
    /// @param startAddr 起始寄存器地址
    /// @param count     寄存器数量
    /// @return 完整帧字节序列
    ///
    /// 帧格式: MBAP | 03 | StartAddr(2B BE) | Quantity(2B BE)
    /// PDU Len = FC(1) + StartAddr(2) + Quantity(2) = 5
    static std::vector<uint8_t> buildReadHoldingRegisters(
        uint16_t tid, uint8_t unitId,
        uint16_t startAddr, uint16_t count)
    {
        constexpr uint16_t pduLen = 1 + 2 + 2; // 5
        std::vector<uint8_t> frame(7 + pduLen);
        writeMbap(frame, tid, unitId, pduLen);
        frame[7] = 0x03;                            // PDU: FC = 03 Read Holding Registers
        writeUint16BE(frame, 8, startAddr);         // PDU: Start Address
        writeUint16BE(frame, 10, count);            // PDU: Quantity
        return frame;
    }

    /// @brief 构建 FC05 请求帧（写单线圈）
    /// @param tid     事务 ID
    /// @param unitId  单元 ID
    /// @param addr    线圈地址
    /// @param value   true = ON (0xFF00), false = OFF (0x0000)
    /// @return 完整帧字节序列
    ///
    /// 帧格式: MBAP | 05 | Addr(2B BE) | Value(2B: 0xFF00 or 0x0000)
    /// PDU Len = FC(1) + Addr(2) + Value(2) = 5
    static std::vector<uint8_t> buildWriteSingleCoil(
        uint16_t tid, uint8_t unitId,
        uint16_t addr, bool value)
    {
        constexpr uint16_t pduLen = 1 + 2 + 2; // 5
        std::vector<uint8_t> frame(7 + pduLen);
        writeMbap(frame, tid, unitId, pduLen);
        frame[7] = 0x05;                            // PDU: FC = 05 Write Single Coil
        writeUint16BE(frame, 8, addr);              // PDU: Address
        writeUint16BE(frame, 10, value ? 0xFF00 : 0x0000); // PDU: Value
        return frame;
    }

    /// @brief 构建 FC06 请求帧（写单寄存器）
    /// @param tid     事务 ID
    /// @param unitId  单元 ID
    /// @param addr    寄存器地址
    /// @param value   16 位值
    /// @return 完整帧字节序列
    ///
    /// 帧格式: MBAP | 06 | Addr(2B BE) | Value(2B BE)
    /// PDU Len = FC(1) + Addr(2) + Value(2) = 5
    static std::vector<uint8_t> buildWriteSingleRegister(
        uint16_t tid, uint8_t unitId,
        uint16_t addr, uint16_t value)
    {
        constexpr uint16_t pduLen = 1 + 2 + 2; // 5
        std::vector<uint8_t> frame(7 + pduLen);
        writeMbap(frame, tid, unitId, pduLen);
        frame[7] = 0x06;                            // PDU: FC = 06 Write Single Register
        writeUint16BE(frame, 8, addr);              // PDU: Address
        writeUint16BE(frame, 10, value);            // PDU: Value
        return frame;
    }

    /// @brief 构建 FC16 (0x10) 请求帧（写多寄存器）
    /// @param tid       事务 ID
    /// @param unitId    单元 ID
    /// @param startAddr 起始寄存器地址
    /// @param values    连续的 16 位值序列
    /// @return 完整帧字节序列
    ///
    /// 帧格式: MBAP | 10 | StartAddr(2B) | Quantity(2B) | ByteCount(1B) | RegData(N×2B)
    /// PDU Len = FC(1) + StartAddr(2) + Quantity(2) + ByteCount(1) + values.size()*2
    ///         = 6 + values.size()*2
    static std::vector<uint8_t> buildWriteMultipleRegisters(
        uint16_t tid, uint8_t unitId,
        uint16_t startAddr, const std::vector<uint16_t>& values)
    {
        const size_t dataBytes = values.size() * 2;
        const uint16_t pduLen  = static_cast<uint16_t>(1 + 2 + 2 + 1 + dataBytes);
        // PDU = FC(1) + StartAddr(2) + Quantity(2) + ByteCount(1) + data
        //     = 6 + dataBytes
        std::vector<uint8_t> frame(7 + pduLen);
        writeMbap(frame, tid, unitId, pduLen);
        frame[7] = 0x10;                            // PDU: FC = 16 (0x10) Write Multiple Registers
        writeUint16BE(frame, 8, startAddr);         // PDU: Start Address
        writeUint16BE(frame, 10, static_cast<uint16_t>(values.size())); // PDU: Quantity
        frame[12] = static_cast<uint8_t>(dataBytes); // PDU: Byte Count
        size_t pos = 13;
        for (uint16_t v : values) {
            writeUint16BE(frame, pos, v);
            pos += 2;
        }
        return frame;
    }

    // ═══════════════════════════════════════════════
    //  解包 — 解析 Modbus TCP 响应帧
    // ═══════════════════════════════════════════════

    /// @brief 解析 MBAP 头
    /// @param frame 完整响应帧（至少 7 字节）
    /// @return MbapHeader 或 nullopt（帧太短/Protocol ID 非法）
    ///
    /// 校验:
    ///   - 帧长度 >= 7
    ///   - Protocol ID == 0x0000（Modbus 协议规范）
    static std::optional<MbapHeader> parseMbap(const std::vector<uint8_t>& frame) {
        if (frame.size() < 7) {
            return std::nullopt;
        }
        MbapHeader hdr;
        hdr.transactionId = (static_cast<uint16_t>(frame[0]) << 8) |
                             static_cast<uint16_t>(frame[1]);
        uint16_t protocolId = (static_cast<uint16_t>(frame[2]) << 8) |
                               static_cast<uint16_t>(frame[3]);
        if (protocolId != 0x0000) {
            return std::nullopt;
        }
        hdr.length = (static_cast<uint16_t>(frame[4]) << 8) |
                      static_cast<uint16_t>(frame[5]);
        // Unit ID at frame[6]，不存储在 MbapHeader 中（调用方可自行读取）
        return hdr;
    }

    /// @brief 检查响应是否为异常帧
    /// @param fc          请求的原始功能码
    /// @param responseFc  响应中的功能码字节
    /// @param data        响应 PDU 数据（不含 Unit ID + FC），至少 1 字节
    /// @return 0 = 正常响应，非 0 = 异常码（Modbus Exception Code）
    ///
    /// 异常判定: responseFc == (fc | 0x80)
    /// 异常码从 data[0] 提取
    static int checkException(uint8_t fc, uint8_t responseFc,
                              const std::vector<uint8_t>& data) {
        if (responseFc == (fc | 0x80)) {
            if (!data.empty()) {
                return static_cast<int>(data[0]);
            }
            return -1; // 异常帧但没有异常码数据（非法帧）
        }
        return 0; // 正常响应
    }

    /// @brief 从 FC01 响应帧中提取线圈数据
    /// @param frame 完整响应帧（MBAP + PDU）
    /// @return 线圈字节序列（MSB 打包），调用方负责位展开
    ///
    /// 帧结构: MBAP(7) | PDU(FC + ByteCount + Data)
    /// frame[7] = FC, frame[8] = ByteCount, frame[9] = Data...
    static std::vector<uint8_t> parseCoilResponse(const std::vector<uint8_t>& frame) {
        std::vector<uint8_t> result;
        if (frame.size() < 9) { // MBAP(7) + FC(1) + ByteCount(1) = 9
            return result;
        }
        uint8_t byteCount = frame[8];
        size_t dataStart  = 9;
        if (frame.size() < dataStart + byteCount) {
            return result;
        }
        result.assign(frame.begin() + static_cast<long>(dataStart),
                      frame.begin() + static_cast<long>(dataStart + byteCount));
        return result;
    }

    /// @brief 从 FC03 响应帧中提取寄存器数据
    /// @param frame 完整响应帧（MBAP + PDU）
    /// @return 16 位无符号寄存器值序列
    ///
    /// 帧结构: MBAP(7) | PDU(FC + ByteCount + Data)
    /// frame[7] = FC, frame[8] = ByteCount, frame[9] = Data...
    static std::vector<uint16_t> parseRegisterResponse(const std::vector<uint8_t>& frame) {
        std::vector<uint16_t> result;
        if (frame.size() < 9) { // MBAP(7) + FC(1) + ByteCount(1) = 9
            return result;
        }
        uint8_t byteCount = frame[8];
        size_t dataStart  = 9;
        if (frame.size() < dataStart + byteCount) {
            return result;
        }
        size_t regCount = byteCount / 2;
        result.reserve(regCount);
        for (size_t i = 0; i < regCount; ++i) {
            size_t off = dataStart + i * 2;
            uint16_t val = (static_cast<uint16_t>(frame[off]) << 8) |
                            static_cast<uint16_t>(frame[off + 1]);
            result.push_back(val);
        }
        return result;
    }
};

} // namespace plc::protocol::tcp
