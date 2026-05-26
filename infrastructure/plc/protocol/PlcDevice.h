// infrastructure/plc/protocol/PlcDevice.h
// P4: PlcDevice — 终极门面 (v5: const RegisterInfo& 编译期类型安全)
//
// 职责：
// - 封装"Snapshot 持有 + Codec 解码"的一站式读取服务
// - 通过 IModbusClient 代理所有写操作
// - 线程安全：只读的快照更新天然无需加锁
// - 类型安全：所有 API 使用 const RegisterInfo&，编译期即可捕获错误

#pragma once
#include "PlcValue.h"
#include "PlcSnapshot.h"
#include "RegisterMetadata.h"
#include "RegisterCodec.h"
#include "ProtocolProfile.h"
#include "IModbusClient.h"
#include <stdexcept>

namespace plc::protocol {

/**
 * @brief PLC 设备门面 (Device Facade)
 *
 * @details
 * PlcDevice 是业务层直接使用的聚合根，融合了以下职责：
 * 1. 持有最新一次轮询的 PlcSnapshot（数据源）
 * 2. 通过 RegisterCodec 将 Snapshot 翻译为 PlcValue（读通道）
 * 3. 通过 IModbusClient 将业务值编码后写出（写通道）
 *
 * @par 使用示例
 * @code
 *   PlcDevice device(profile);
 *   device.bindTransport(client);
 *
 *   // 主循环
 *   void tick() {
 *       PlcSnapshot snap = poller.poll(*client, registry, Feedback);
 *       device.updateSnapshot(std::move(snap));
 *
 *       if (device.isStateTrusted()) {
 *           float pos = device.readFloat(y_axis::feedback::ABS_POSITION);
 *           device.writeFloat(y_axis::command::TARGET_POSITION, 200.0f);
 *       }
 *   }
 * @endcode
 */
class PlcDevice {
public:
    /// @brief 构造 PlcDevice，绑定设备 Profile（端序策略、线圈 FF00 语义等）
    explicit PlcDevice(const ProtocolProfile& profile)
        : m_profile(profile)
        , m_snapshot()
        , m_client(nullptr)
    {}

    // ═══════════════════════════════════════════════
    //  读取（★ const RegisterInfo& — 编译期类型安全）
    // ═══════════════════════════════════════════════

    /// @brief 以 PlcValue 泛型形式读取寄存器当前值
    /// @param reg 寄存器元数据引用（调用方传入确切命名空间常量）
    /// @return PlcValue 标准化多态值
    PlcValue readValue(const RegisterInfo& reg) const {
        return RegisterCodec::decode(reg, m_snapshot, m_profile);
    }

    /// @brief 读取 Bool 类型寄存器
    /// @throws std::bad_variant_access 如果寄存器不是 Bool 类型
    bool readBool(const RegisterInfo& reg) const {
        return getValue<bool>(readValue(reg));
    }

    /// @brief 读取 Int16 类型寄存器
    /// @throws std::bad_variant_access 如果寄存器不是 Int16 类型
    int16_t readInt16(const RegisterInfo& reg) const {
        return getValue<int16_t>(readValue(reg));
    }

    /// @brief 读取 Float32 类型寄存器
    /// @throws std::bad_variant_access 如果寄存器不是 Float 类型
    float readFloat(const RegisterInfo& reg) const {
        return getValue<float>(readValue(reg));
    }

    // ═══════════════════════════════════════
    //  写入 (返回 CommunicationResult 表达通讯结果)
    // ═══════════════════════════════════════

    /// @brief 以泛型 PlcValue 形式写入寄存器
    /// @throws std::invalid_argument 如果寄存器为 ReadOnly（编程错误，非通讯错误）
    /// @throws std::runtime_error 如果未绑定传输层（配置错误，非通讯错误）
    /// @return 通讯结果 — 成功/失败/可重试信息
    CommunicationResult writeValue(const RegisterInfo& reg, const PlcValue& value) {
        validateWritable(reg);
        std::vector<uint16_t> encoded = RegisterCodec::encode(value, reg, m_profile);
        return dispatchWrite(reg, encoded);
    }

    /// @brief 写入 Bool 值
    /// @return 通讯结果
    CommunicationResult writeBool(const RegisterInfo& reg, bool v) {
        return writeValue(reg, PlcValue{v});
    }

    /// @brief 写入 Int16 值
    /// @return 通讯结果
    CommunicationResult writeInt16(const RegisterInfo& reg, int16_t v) {
        return writeValue(reg, PlcValue{v});
    }

    /// @brief 写入 Float32 值
    /// @return 通讯结果
    CommunicationResult writeFloat(const RegisterInfo& reg, float v) {
        return writeValue(reg, PlcValue{v});
    }

    // ═══════════════════════════════════════
    //  Snapshot 管理
    // ═══════════════════════════════════════

    /// @brief 更新内部快照为最新一轮轮询结果
    void updateSnapshot(PlcSnapshot snap) {
        m_snapshot = std::move(snap);
    }

    /// @brief 返回当前快照的只读引用
    const PlcSnapshot& snapshot() const {
        return m_snapshot;
    }

    /// @brief 当前快照是否可信（所有子快照来自成功的网络读取）
    bool isStateTrusted() const {
        return m_snapshot.isTrusted();
    }

    // ═══════════════════════════════════════
    //  Transport 绑定
    // ═══════════════════════════════════════

    /// @brief 绑定 Modbus 传输客户端（写操作代理）
    /// @note 可多次调用以替换传输层；传入 nullptr 解绑
    void bindTransport(IModbusClient* client) {
        m_client = client;
    }

    /// @brief 检查是否已绑定传输层
    bool hasTransport() const {
        return m_client != nullptr;
    }

private:
    const ProtocolProfile& m_profile;
    PlcSnapshot m_snapshot;
    IModbusClient* m_client;

    /// @brief 校验寄存器可写性
    void validateWritable(const RegisterInfo& reg) const {
        if (reg.access == RegisterAccess::ReadOnly) {
            throw std::invalid_argument("PlcDevice: Cannot write to ReadOnly register");
        }
    }

    /// @brief 确保 transport 已绑定
    void ensureTransport() const {
        if (!m_client) {
            throw std::runtime_error("PlcDevice: No transport bound for write operation");
        }
    }

    /// @brief 根据寄存器区域分发写操作到 Modbus 协议函数
    /// @return 通讯结果，透传自 IModbusClient 写接口
    CommunicationResult dispatchWrite(const RegisterInfo& reg, const std::vector<uint16_t>& encoded) {
        ensureTransport();

        switch (reg.area) {
            case RegisterArea::Coil: {
                // Coil 写入使用 FC05
                // encoded[0] 通常为 0xFF00 (ON) 或 0x0000 (OFF)
                bool state = (encoded.size() > 0 && encoded[0] != 0);
                return m_client->writeSingleCoil(reg.address, state);
            }
            case RegisterArea::HoldingReg: {
                if (encoded.size() == 1) {
                    // 单寄存器写入使用 FC06
                    return m_client->writeSingleRegister(reg.address, encoded[0]);
                } else {
                    // 多寄存器写入使用 FC10
                    return m_client->writeMultipleRegisters(reg.address, encoded);
                }
            }
        }
        // 不应到达此处
        return CommunicationResult{CommunicationResult::Status::ProtocolError, 0, "Unknown register area"};
    }
};

} // namespace plc::protocol
