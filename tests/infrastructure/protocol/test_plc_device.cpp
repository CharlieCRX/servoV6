// tests/infrastructure/protocol/test_plc_device.cpp
// P4: PlcDevice TDD 测试
// 遵循 v5 架构：const RegisterInfo& 编译期类型安全
//
// 测试覆盖：
//   Part 1: 读取 — decode 通过 Snapshot 管线
//   Part 2: 写入 — encode + dispatch 到 FakeModbusClient，含 CommunicationResult 返回值
//   Part 3: Transport 管理 — bind/unbind 错误处理
//   Part 4: Snapshot 生命周期 — 更新/信任/不完整快照
//   Part 5: 编译期类型安全 — const RegisterInfo& 语义
//   Part 6: 综合回环测试
//   Part 7: 多轴集成使用场景
//   Part 8: 写错误返回值测试（CommunicationResult 透传）
//   Part 9: FakeModbusClient 读接口单元测试

#include <gtest/gtest.h>
#include "infrastructure/plc/protocol/PlcDevice.h"
#include "infrastructure/plc/protocol/PlcValue.h"
#include "infrastructure/plc/protocol/PlcSnapshot.h"
#include "infrastructure/plc/protocol/MemorySnapshot.h"
#include "infrastructure/plc/protocol/RegisterAddressAll.h"
#include "infrastructure/plc/protocol/ProtocolProfile.h"
#include "infrastructure/ISystemDriver.h"

using namespace plc::protocol;

// ============================================================================
// 测试辅助：FakeModbusClient —— 读/写接口 + 可编程返回值（Test Spy + Fake）
// ============================================================================

class FakeModbusClient : public IModbusClient {
public:
    // ===== 可编程返回配置 =====
    CommunicationResult nextReadResult  = CommunicationResult{CommunicationResult::Status::Sent};
    CommunicationResult nextWriteResult = CommunicationResult{CommunicationResult::Status::Sent};

    std::vector<uint8_t>  programmedCoilPayload;       // readCoils 返回的数据
    std::vector<uint16_t> programmedRegisterPayload;   // readHoldingRegisters 返回的数据

    // ===== 读调用计数 =====
    int readCoilCount = 0;
    int readRegCount = 0;

    // ===== 写调用计数 =====
    int coilWriteCount = 0;
    int singleRegWriteCount = 0;
    int multiRegWriteCount = 0;

    // ===== 最近一次调用的参数 =====
    uint16_t lastCoilAddress = 0;
    bool lastCoilValue = false;

    uint16_t lastSingleRegAddress = 0;
    uint16_t lastSingleRegValue = 0;

    uint16_t lastMultiStartAddress = 0;
    std::vector<uint16_t> lastMultiValues;

    // ═══════════════════════════════════════
    //  读通道 — 实现 IModbusClient 读接口
    // ═══════════════════════════════════════

    CommunicationResult readCoils(uint16_t startAddress, uint16_t count,
                                   std::vector<uint8_t>& payload) override {
        readCoilCount++;
        if (nextReadResult.ok())
            payload = programmedCoilPayload;
        return nextReadResult;
    }

    CommunicationResult readHoldingRegisters(uint16_t startAddress, uint16_t count,
                                              std::vector<uint16_t>& payload) override {
        readRegCount++;
        if (nextReadResult.ok())
            payload = programmedRegisterPayload;
        return nextReadResult;
    }

    // ═══════════════════════════════════════
    //  写通道 — 返回 nextWriteResult
    // ═══════════════════════════════════════

    CommunicationResult writeSingleCoil(uint16_t address, bool value) override {
        coilWriteCount++;
        lastCoilAddress = address;
        lastCoilValue = value;
        return nextWriteResult;
    }

    CommunicationResult writeSingleRegister(uint16_t address, uint16_t value) override {
        singleRegWriteCount++;
        lastSingleRegAddress = address;
        lastSingleRegValue = value;
        return nextWriteResult;
    }

    CommunicationResult writeMultipleRegisters(uint16_t startAddress, const std::vector<uint16_t>& values) override {
        multiRegWriteCount++;
        lastMultiStartAddress = startAddress;
        lastMultiValues = values;
        return nextWriteResult;
    }
};

// ============================================================================
// 测试辅助：Profile 和 Snapshot 构建器
// ============================================================================

// 汇川 H5U: BigEndian + LowWordFirst (CDAB), coilUsesFF00=true
constexpr ProtocolProfile TEST_PROFILE = {
    "Inovance_H5U_Test",
    {ByteOrder::BigEndian, WordOrder::LowWordFirst},
    120, 120, true, false
};

/// @brief 构建包含指定地址 Bool 值的 Coil 快照
PlcSnapshot buildSnapWithCoil(uint16_t address, bool value) {
    std::vector<uint8_t> payload(1, value ? 0x01 : 0x00);
    RawBitSnapshot bits(address, 1, payload);
    RawWordSnapshot words;
    return PlcSnapshot(std::move(bits), std::move(words), true, 1000);
}

/// @brief 构建包含指定地址 Int16 值的 Word 快照
PlcSnapshot buildSnapWithInt16(uint16_t address, int16_t value) {
    RawBitSnapshot bits;
    std::vector<uint16_t> payload{static_cast<uint16_t>(value)};
    RawWordSnapshot words(address, payload);
    return PlcSnapshot(std::move(bits), std::move(words), true, 1000);
}

/// @brief 构建包含指定地址 Float32 值的 Word 快照（CDAB = BigEndian + LowWordFirst）
/// 例: 150.0f = 0x43160000 → [HiWord, LoWord] = [0x4316, 0x0000]
/// CDAB: LowWordFirst → [0x0000, 0x4316]
PlcSnapshot buildSnapWithFloat(uint16_t address, float value) {
    RawBitSnapshot bits;
    uint32_t raw;
    std::memcpy(&raw, &value, sizeof(float));
    uint16_t hiWord = static_cast<uint16_t>((raw >> 16) & 0xFFFF);
    uint16_t loWord = static_cast<uint16_t>(raw & 0xFFFF);
    // CDAB = LowWordFirst → 低位字先
    std::vector<uint16_t> payload{loWord, hiWord};
    RawWordSnapshot words(address, payload);
    return PlcSnapshot(std::move(bits), std::move(words), true, 1000);
}


// ============================================================================
// Part 1: 读取 — decode 通过 Snapshot 管线
// ============================================================================

class PlcDeviceReadTest : public ::testing::Test {
protected:
    PlcDevice device{TEST_PROFILE};
};

TEST_F(PlcDeviceReadTest, ReadBool_True_FromCoilSnapshot) {
    PlcSnapshot snap = buildSnapWithCoil(
        plc::reg::y_axis::feedback::MOVE_DONE.address, true);
    device.updateSnapshot(std::move(snap));

    bool result = device.readBool(plc::reg::y_axis::feedback::MOVE_DONE);
    EXPECT_TRUE(result);
}

TEST_F(PlcDeviceReadTest, ReadBool_False_FromCoilSnapshot) {
    PlcSnapshot snap = buildSnapWithCoil(
        plc::reg::y_axis::feedback::MOVE_DONE.address, false);
    device.updateSnapshot(std::move(snap));

    bool result = device.readBool(plc::reg::y_axis::feedback::MOVE_DONE);
    EXPECT_FALSE(result);
}

TEST_F(PlcDeviceReadTest, ReadInt16_FromWordSnapshot) {
    PlcSnapshot snap = buildSnapWithInt16(
        plc::reg::y_axis::feedback::STATE.address, 3);
    device.updateSnapshot(std::move(snap));

    int16_t result = device.readInt16(plc::reg::y_axis::feedback::STATE);
    EXPECT_EQ(result, 3);
}

TEST_F(PlcDeviceReadTest, ReadFloat_FromWordSnapshot) {
    PlcSnapshot snap = buildSnapWithFloat(
        plc::reg::y_axis::feedback::ABS_POSITION.address, 150.0f);
    device.updateSnapshot(std::move(snap));

    float result = device.readFloat(plc::reg::y_axis::feedback::ABS_POSITION);
    EXPECT_FLOAT_EQ(result, 150.0f);
}

TEST_F(PlcDeviceReadTest, ReadFloat_NegativeValue) {
    PlcSnapshot snap = buildSnapWithFloat(
        plc::reg::y_axis::feedback::ABS_POSITION.address, -13.25f);
    device.updateSnapshot(std::move(snap));

    float result = device.readFloat(plc::reg::y_axis::feedback::ABS_POSITION);
    EXPECT_FLOAT_EQ(result, -13.25f);
}

TEST_F(PlcDeviceReadTest, ReadFloat_Zero) {
    PlcSnapshot snap = buildSnapWithFloat(
        plc::reg::y_axis::feedback::ABS_POSITION.address, 0.0f);
    device.updateSnapshot(std::move(snap));

    float result = device.readFloat(plc::reg::y_axis::feedback::ABS_POSITION);
    EXPECT_FLOAT_EQ(result, 0.0f);
}

TEST_F(PlcDeviceReadTest, ReadValue_Generic_ReturnsCorrectType) {
    PlcSnapshot snap = buildSnapWithFloat(
        plc::reg::y_axis::feedback::ABS_POSITION.address, 150.0f);
    device.updateSnapshot(std::move(snap));

    PlcValue val = device.readValue(plc::reg::y_axis::feedback::ABS_POSITION);
    EXPECT_TRUE(isFloat(val));
    EXPECT_FLOAT_EQ(getValue<float>(val), 150.0f);
}

// v5 核心用例：跨轴区分 — 同名变量在 C++ 命名空间中天然隔离
TEST_F(PlcDeviceReadTest, ReadFloat_CrossAxis_Distinction_ByNamespace) {
    PlcSnapshot snapX = buildSnapWithFloat(
        plc::reg::x_axis::feedback::ABS_POSITION.address, 100.0f);
    device.updateSnapshot(std::move(snapX));
    float xPos = device.readFloat(plc::reg::x_axis::feedback::ABS_POSITION);
    EXPECT_FLOAT_EQ(xPos, 100.0f);

    PlcSnapshot snapY = buildSnapWithFloat(
        plc::reg::y_axis::feedback::ABS_POSITION.address, 200.0f);
    device.updateSnapshot(std::move(snapY));
    float yPos = device.readFloat(plc::reg::y_axis::feedback::ABS_POSITION);
    EXPECT_FLOAT_EQ(yPos, 200.0f);
}

TEST_F(PlcDeviceReadTest, ReadInt16_State_XAxis_Vs_YAxis) {
    PlcSnapshot snapX = buildSnapWithInt16(
        plc::reg::x_axis::feedback::STATE.address, 1);
    device.updateSnapshot(std::move(snapX));
    EXPECT_EQ(device.readInt16(plc::reg::x_axis::feedback::STATE), 1);

    PlcSnapshot snapY = buildSnapWithInt16(
        plc::reg::y_axis::feedback::STATE.address, 2);
    device.updateSnapshot(std::move(snapY));
    EXPECT_EQ(device.readInt16(plc::reg::y_axis::feedback::STATE), 2);
}

TEST_F(PlcDeviceReadTest, ReadBool_XAxisEnableRequest) {
    PlcSnapshot snap = buildSnapWithCoil(
        plc::reg::x_axis::command::ENABLE_REQUEST.address, true);
    device.updateSnapshot(std::move(snap));

    bool result = device.readBool(plc::reg::x_axis::command::ENABLE_REQUEST);
    EXPECT_TRUE(result);
}


// ============================================================================
// Part 2: 写入 — encode + dispatch 到 FakeModbusClient（含 CommunicationResult 返回值）
// ============================================================================

class PlcDeviceWriteTest : public ::testing::Test {
protected:
    PlcDevice device{TEST_PROFILE};
    FakeModbusClient fakeClient;

    void SetUp() override {
        device.bindTransport(&fakeClient);
    }
};

TEST_F(PlcDeviceWriteTest, WriteBool_Coil_UsesFC05) {
    fakeClient.nextWriteResult = CommunicationResult{CommunicationResult::Status::Sent};
    CommunicationResult result = device.writeBool(plc::reg::x_axis::command::ENABLE_REQUEST, true);

    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.status, CommunicationResult::Status::Sent);
    EXPECT_EQ(fakeClient.coilWriteCount, 1);
    EXPECT_EQ(fakeClient.singleRegWriteCount, 0);
    EXPECT_EQ(fakeClient.multiRegWriteCount, 0);
    EXPECT_EQ(fakeClient.lastCoilAddress, plc::reg::x_axis::command::ENABLE_REQUEST.address);
    EXPECT_TRUE(fakeClient.lastCoilValue);
}

TEST_F(PlcDeviceWriteTest, WriteBool_Coil_False_UsesFC05) {
    fakeClient.nextWriteResult = CommunicationResult{CommunicationResult::Status::Sent};
    CommunicationResult result = device.writeBool(plc::reg::x_axis::command::X1_JOG_FORWARD, false);

    EXPECT_TRUE(result.ok());
    EXPECT_EQ(fakeClient.coilWriteCount, 1);
    EXPECT_EQ(fakeClient.lastCoilAddress, plc::reg::x_axis::command::X1_JOG_FORWARD.address);
    EXPECT_FALSE(fakeClient.lastCoilValue);
}

TEST_F(PlcDeviceWriteTest, WriteInt16_HoldingReg_UsesFC06) {
    fakeClient.nextWriteResult = CommunicationResult{CommunicationResult::Status::Sent};
    constexpr RegisterInfo testReg = {
        RegisterArea::HoldingReg, 200, RegisterType::Int16,
        RegisterAccess::ReadWrite, RegisterBehavior::Level,
        RegisterGroup::Command, "", "TestWritableInt16", 0
    };

    CommunicationResult result = device.writeValue(testReg, PlcValue{static_cast<int16_t>(42)});

    EXPECT_TRUE(result.ok());
    EXPECT_EQ(fakeClient.singleRegWriteCount, 1);
    EXPECT_EQ(fakeClient.lastSingleRegAddress, 200);
    EXPECT_EQ(fakeClient.lastSingleRegValue, 42);
}

TEST_F(PlcDeviceWriteTest, WriteFloat_HoldingReg_UsesFC10) {
    fakeClient.nextWriteResult = CommunicationResult{CommunicationResult::Status::Sent};
    CommunicationResult result = device.writeFloat(plc::reg::y_axis::command::ABS_TARGET, 150.0f);

    EXPECT_TRUE(result.ok());
    EXPECT_EQ(fakeClient.multiRegWriteCount, 1);
    EXPECT_EQ(fakeClient.lastMultiStartAddress, plc::reg::y_axis::command::ABS_TARGET.address);
    ASSERT_EQ(fakeClient.lastMultiValues.size(), 2);
    // CDAB: 150.0f = 0x43160000 → LowWordFirst → [0x0000, 0x4316]
    EXPECT_EQ(fakeClient.lastMultiValues[0], 0x0000);
    EXPECT_EQ(fakeClient.lastMultiValues[1], 0x4316);
}

TEST_F(PlcDeviceWriteTest, WriteFloat_Negative_HoldingReg_UsesFC10) {
    fakeClient.nextWriteResult = CommunicationResult{CommunicationResult::Status::Sent};
    CommunicationResult result = device.writeFloat(plc::reg::y_axis::command::ABS_TARGET, -1.0f);

    EXPECT_TRUE(result.ok());
    EXPECT_EQ(fakeClient.multiRegWriteCount, 1);
    // -1.0f = 0xBF800000, CDAB → [0x0000, 0xBF80]
    EXPECT_EQ(fakeClient.lastMultiValues[0], 0x0000);
    EXPECT_EQ(fakeClient.lastMultiValues[1], 0xBF80);
}

TEST_F(PlcDeviceWriteTest, WriteValue_GenericBool_DispatchesCorrectly) {
    fakeClient.nextWriteResult = CommunicationResult{CommunicationResult::Status::Sent};
    PlcValue val = true;
    CommunicationResult result = device.writeValue(plc::reg::x_axis::command::HOME_TRIGGER, val);

    EXPECT_TRUE(result.ok());
    EXPECT_EQ(fakeClient.coilWriteCount, 1);
    EXPECT_EQ(fakeClient.lastCoilAddress, plc::reg::x_axis::command::HOME_TRIGGER.address);
    EXPECT_TRUE(fakeClient.lastCoilValue);
}

TEST_F(PlcDeviceWriteTest, WriteValue_GenericFloat_DispatchesCorrectly) {
    fakeClient.nextWriteResult = CommunicationResult{CommunicationResult::Status::Sent};
    PlcValue val = 150.0f;
    CommunicationResult result = device.writeValue(plc::reg::y_axis::command::ABS_TARGET, val);

    EXPECT_TRUE(result.ok());
    EXPECT_EQ(fakeClient.multiRegWriteCount, 1);
    EXPECT_EQ(fakeClient.lastMultiValues[0], 0x0000);
    EXPECT_EQ(fakeClient.lastMultiValues[1], 0x4316);
}


// ============================================================================
// Part 3: 错误处理 — ReadOnly / NoTransport
// ============================================================================

class PlcDeviceErrorTest : public ::testing::Test {
protected:
    PlcDevice device{TEST_PROFILE};
};

TEST_F(PlcDeviceErrorTest, WriteReadOnly_ThrowsInvalidArgument) {
    EXPECT_THROW({
        device.writeFloat(plc::reg::y_axis::feedback::ABS_POSITION, 100.0f);
    }, std::invalid_argument);
}

TEST_F(PlcDeviceErrorTest, WriteBoolReadOnly_ThrowsInvalidArgument) {
    EXPECT_THROW({
        device.writeBool(plc::reg::y_axis::feedback::MOVE_DONE, true);
    }, std::invalid_argument);
}

TEST_F(PlcDeviceErrorTest, WriteInt16ReadOnly_ThrowsInvalidArgument) {
    EXPECT_THROW({
        device.writeInt16(plc::reg::y_axis::feedback::STATE, 5);
    }, std::invalid_argument);
}

TEST_F(PlcDeviceErrorTest, WriteNoTransport_ThrowsRuntimeError) {
    EXPECT_THROW({
        device.writeBool(plc::reg::x_axis::command::ENABLE_REQUEST, true);
    }, std::runtime_error);
}

TEST_F(PlcDeviceErrorTest, WriteNoTransport_Float_ThrowsRuntimeError) {
    EXPECT_THROW({
        device.writeFloat(plc::reg::y_axis::command::ABS_TARGET, 150.0f);
    }, std::runtime_error);
}

TEST_F(PlcDeviceErrorTest, WriteAfterUnbind_ThrowsRuntimeError) {
    FakeModbusClient client;
    device.bindTransport(&client);
    device.writeBool(plc::reg::x_axis::command::ENABLE_REQUEST, true);

    device.bindTransport(nullptr);
    EXPECT_THROW({
        device.writeBool(plc::reg::x_axis::command::ENABLE_REQUEST, true);
    }, std::runtime_error);
}

TEST_F(PlcDeviceErrorTest, ReadWithoutSnapshot_ThrowsOutOfRange) {
    EXPECT_THROW({
        device.readFloat(plc::reg::y_axis::feedback::ABS_POSITION);
    }, std::out_of_range);
}


// ============================================================================
// Part 4: Snapshot 生命周期 — 更新/信任/不完整快照
// ============================================================================

class PlcDeviceSnapshotTest : public ::testing::Test {
protected:
    PlcDevice device{TEST_PROFILE};
};

TEST_F(PlcDeviceSnapshotTest, IsStateTrusted_True_WhenComplete) {
    PlcSnapshot snap = buildSnapWithFloat(124, 150.0f);
    device.updateSnapshot(std::move(snap));

    EXPECT_TRUE(device.isStateTrusted());
}

TEST_F(PlcDeviceSnapshotTest, IsStateTrusted_False_WhenIncomplete) {
    RawBitSnapshot bits;
    RawWordSnapshot words;
    PlcSnapshot snap(std::move(bits), std::move(words), false, 0);
    device.updateSnapshot(std::move(snap));

    EXPECT_FALSE(device.isStateTrusted());
}

TEST_F(PlcDeviceSnapshotTest, SnapshotAccessor_ReturnsReference) {
    PlcSnapshot snap = buildSnapWithFloat(124, 150.0f);
    device.updateSnapshot(std::move(snap));

    const PlcSnapshot& ref = device.snapshot();
    EXPECT_TRUE(ref.isTrusted());
    EXPECT_EQ(ref.timestamp, 1000);
}

TEST_F(PlcDeviceSnapshotTest, UpdateSnapshot_ReplacesPrevious) {
    PlcSnapshot snap1 = buildSnapWithFloat(124, 100.0f);
    device.updateSnapshot(std::move(snap1));
    EXPECT_FLOAT_EQ(device.readFloat(plc::reg::y_axis::feedback::ABS_POSITION), 100.0f);

    PlcSnapshot snap2 = buildSnapWithFloat(124, 200.0f);
    device.updateSnapshot(std::move(snap2));
    EXPECT_FLOAT_EQ(device.readFloat(plc::reg::y_axis::feedback::ABS_POSITION), 200.0f);
}

TEST_F(PlcDeviceSnapshotTest, ReadFromIncompleteSnapshot_StillDecodesAvailableData) {
    std::vector<uint8_t> coilPayload{0x01};
    RawBitSnapshot bits(101, 1, coilPayload);
    RawWordSnapshot words;
    PlcSnapshot snap(std::move(bits), std::move(words), false, 0);
    device.updateSnapshot(std::move(snap));

    EXPECT_FALSE(device.isStateTrusted());
    bool result = device.readBool(plc::reg::y_axis::feedback::MOVE_DONE);
    EXPECT_TRUE(result);
}


// ============================================================================
// Part 5: Transport 管理
// ============================================================================

class PlcDeviceTransportTest : public ::testing::Test {
protected:
    PlcDevice device{TEST_PROFILE};
};

TEST_F(PlcDeviceTransportTest, HasTransport_False_ByDefault) {
    EXPECT_FALSE(device.hasTransport());
}

TEST_F(PlcDeviceTransportTest, HasTransport_True_AfterBind) {
    FakeModbusClient client;
    device.bindTransport(&client);
    EXPECT_TRUE(device.hasTransport());
}

TEST_F(PlcDeviceTransportTest, HasTransport_False_AfterBindNullptr) {
    FakeModbusClient client;
    device.bindTransport(&client);
    device.bindTransport(nullptr);
    EXPECT_FALSE(device.hasTransport());
}

TEST_F(PlcDeviceTransportTest, RebindTransport_UpdatesClient) {
    FakeModbusClient client1, client2;
    device.bindTransport(&client1);
    device.writeBool(plc::reg::x_axis::command::ENABLE_REQUEST, true);
    EXPECT_EQ(client1.coilWriteCount, 1);
    EXPECT_EQ(client2.coilWriteCount, 0);

    device.bindTransport(&client2);
    device.writeBool(plc::reg::x_axis::command::ENABLE_REQUEST, false);
    EXPECT_EQ(client1.coilWriteCount, 1);
    EXPECT_EQ(client2.coilWriteCount, 1);
}


// ============================================================================
// Part 6: 综合回环测试 (Write → Read 模拟)
// ============================================================================

class PlcDeviceRoundtripTest : public ::testing::Test {
protected:
    PlcDevice device{TEST_PROFILE};
};

TEST_F(PlcDeviceRoundtripTest, WriteThenVerify_Int16_ViaSnapshot) {
    FakeModbusClient client;
    device.bindTransport(&client);

    constexpr RegisterInfo testReg = {
        RegisterArea::HoldingReg, 200, RegisterType::Int16,
        RegisterAccess::ReadWrite, RegisterBehavior::Level,
        RegisterGroup::Command, "", "TestRoundtripInt16", 0
    };

    CommunicationResult result = device.writeInt16(testReg, 42);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(client.lastSingleRegValue, 42);
    EXPECT_EQ(client.lastSingleRegAddress, 200);

    PlcSnapshot snap = buildSnapWithInt16(200, 42);
    device.updateSnapshot(std::move(snap));

    EXPECT_EQ(device.readInt16(testReg), 42);
}

TEST_F(PlcDeviceRoundtripTest, WriteThenVerify_Float32_ViaSnapshot) {
    FakeModbusClient client;
    device.bindTransport(&client);

    CommunicationResult result = device.writeFloat(plc::reg::y_axis::command::ABS_TARGET, 150.0f);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(client.lastMultiValues[0], 0x0000);
    EXPECT_EQ(client.lastMultiValues[1], 0x4316);

    PlcSnapshot snap = buildSnapWithFloat(24, 150.0f);
    device.updateSnapshot(std::move(snap));

    EXPECT_FLOAT_EQ(device.readFloat(plc::reg::y_axis::command::ABS_TARGET), 150.0f);
}


// ============================================================================
// Part 7: 多轴集成使用场景
// ============================================================================

TEST(PlcDeviceMultiAxisTest, PollAllAxesThenRead) {
    PlcDevice device(TEST_PROFILE);

    PlcSnapshot snapX = buildSnapWithFloat(120, 100.0f);
    device.updateSnapshot(std::move(snapX));
    EXPECT_FLOAT_EQ(device.readFloat(plc::reg::x_axis::feedback::ABS_POSITION), 100.0f);

    PlcSnapshot snapY = buildSnapWithFloat(124, 200.0f);
    device.updateSnapshot(std::move(snapY));
    EXPECT_FLOAT_EQ(device.readFloat(plc::reg::y_axis::feedback::ABS_POSITION), 200.0f);

    PlcSnapshot snapZ = buildSnapWithFloat(128, 300.0f);
    device.updateSnapshot(std::move(snapZ));
    EXPECT_FLOAT_EQ(device.readFloat(plc::reg::z_axis::feedback::ABS_POSITION), 300.0f);

    PlcSnapshot snapR = buildSnapWithFloat(132, 45.0f);
    device.updateSnapshot(std::move(snapR));
    EXPECT_FLOAT_EQ(device.readFloat(plc::reg::r_axis::feedback::ABS_POSITION), 45.0f);
}

TEST(PlcDeviceMultiAxisTest, WriteCommandsToMultipleAxes) {
    FakeModbusClient client;
    PlcDevice device(TEST_PROFILE);
    device.bindTransport(&client);

    CommunicationResult r1 = device.writeBool(plc::reg::x_axis::command::ENABLE_REQUEST, true);
    EXPECT_TRUE(r1.ok());
    EXPECT_EQ(client.lastCoilAddress, 0);
    EXPECT_TRUE(client.lastCoilValue);

    CommunicationResult r2 = device.writeFloat(plc::reg::y_axis::command::ABS_TARGET, 200.0f);
    EXPECT_TRUE(r2.ok());
    EXPECT_EQ(client.lastMultiStartAddress, 24);

    CommunicationResult r3 = device.writeBool(plc::reg::z_axis::command::ABS_MOVE_TRIGGER, true);
    EXPECT_TRUE(r3.ok());
    EXPECT_EQ(client.lastCoilAddress, 44);
}


// ============================================================================
// Part 8: 写错误返回值测试（CommunicationResult 透传）
// ============================================================================

class PlcDeviceWriteErrorTest : public ::testing::Test {
protected:
    PlcDevice device{TEST_PROFILE};
    FakeModbusClient fakeClient;

    void SetUp() override {
        device.bindTransport(&fakeClient);
    }
};

TEST_F(PlcDeviceWriteErrorTest, WriteBool_ReturnsTimeout_Retryable) {
    fakeClient.nextWriteResult = CommunicationResult{
        CommunicationResult::Status::Timeout, 0, "read timeout 500ms"
    };

    CommunicationResult result = device.writeBool(plc::reg::x_axis::command::ENABLE_REQUEST, true);

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.retryable());
    EXPECT_EQ(result.status, CommunicationResult::Status::Timeout);
    EXPECT_EQ(fakeClient.coilWriteCount, 1);
}

TEST_F(PlcDeviceWriteErrorTest, WriteBool_ReturnsBusy_Retryable) {
    fakeClient.nextWriteResult = CommunicationResult{
        CommunicationResult::Status::Busy, 0x06, "PLC Busy"
    };

    CommunicationResult result = device.writeBool(plc::reg::x_axis::command::ENABLE_REQUEST, true);

    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.retryable());
    EXPECT_EQ(result.status, CommunicationResult::Status::Busy);
}

TEST_F(PlcDeviceWriteErrorTest, WriteFloat_ReturnsNetworkError_NotRetryable) {
    fakeClient.nextWriteResult = CommunicationResult{
        CommunicationResult::Status::NetworkError, 0, "ECONNRESET"
    };

    CommunicationResult result = device.writeFloat(plc::reg::y_axis::command::ABS_TARGET, 200.0f);

    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.retryable());
    EXPECT_TRUE(result.isNetworkIssue());
    EXPECT_EQ(result.status, CommunicationResult::Status::NetworkError);
}

TEST_F(PlcDeviceWriteErrorTest, WriteBool_ReturnsProtocolError_WithExceptionCode) {
    fakeClient.nextWriteResult = CommunicationResult{
        CommunicationResult::Status::ProtocolError, 0x02, "Illegal Data Address"
    };

    CommunicationResult result = device.writeBool(plc::reg::x_axis::command::ENABLE_REQUEST, true);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.exceptionCode, 0x02);
    EXPECT_EQ(result.status, CommunicationResult::Status::ProtocolError);
    EXPECT_TRUE(result.isProtocolIssue());
}

TEST_F(PlcDeviceWriteErrorTest, WriteInt16_ReturnsDisconnected) {
    fakeClient.nextWriteResult = CommunicationResult{
        CommunicationResult::Status::Disconnected
    };

    constexpr RegisterInfo testReg = {
        RegisterArea::HoldingReg, 300, RegisterType::Int16,
        RegisterAccess::ReadWrite, RegisterBehavior::Level,
        RegisterGroup::Command, "", "TestDisconnectedInt16", 0
    };

    CommunicationResult result = device.writeInt16(testReg, 100);

    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.retryable());
    EXPECT_TRUE(result.isNetworkIssue());
    EXPECT_EQ(result.status, CommunicationResult::Status::Disconnected);
}

TEST_F(PlcDeviceWriteErrorTest, WriteValue_ReturnsInvalidResponse) {
    fakeClient.nextWriteResult = CommunicationResult{
        CommunicationResult::Status::InvalidResponse, 0, "unexpected response length"
    };

    CommunicationResult result = device.writeValue(
        plc::reg::y_axis::command::ABS_TARGET, PlcValue{150.0f});

    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.retryable());
    EXPECT_EQ(result.status, CommunicationResult::Status::InvalidResponse);
}


// ============================================================================
// Part 9: FakeModbusClient 读接口单元测试
// ============================================================================

class FakeModbusClientReadTest : public ::testing::Test {
protected:
    FakeModbusClient client;
};

TEST_F(FakeModbusClientReadTest, ReadCoils_ReturnsPayload_OnSuccess) {
    client.programmedCoilPayload = {0x0F};
    std::vector<uint8_t> payload;
    CommunicationResult result = client.readCoils(0, 8, payload);

    EXPECT_TRUE(result.ok());
    EXPECT_EQ(payload.size(), 1);
    EXPECT_EQ(payload[0], 0x0F);
    EXPECT_EQ(client.readCoilCount, 1);
}

TEST_F(FakeModbusClientReadTest, ReadCoils_ReturnsNetworkError_PayloadUnchanged) {
    client.nextReadResult = CommunicationResult{
        CommunicationResult::Status::NetworkError, 0, "ECONNREFUSED"
    };
    std::vector<uint8_t> payload = {0x00};
    CommunicationResult result = client.readCoils(0, 8, payload);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status, CommunicationResult::Status::NetworkError);
    EXPECT_EQ(result.diagnostic, "ECONNREFUSED");
    EXPECT_EQ(payload[0], 0x00);
}

TEST_F(FakeModbusClientReadTest, ReadHoldingRegisters_ReturnsPayload_OnSuccess) {
    client.programmedRegisterPayload = {0x1234, 0x5678};
    std::vector<uint16_t> payload;
    CommunicationResult result = client.readHoldingRegisters(100, 2, payload);

    EXPECT_TRUE(result.ok());
    ASSERT_EQ(payload.size(), 2);
    EXPECT_EQ(payload[0], 0x1234);
    EXPECT_EQ(payload[1], 0x5678);
    EXPECT_EQ(client.readRegCount, 1);
}

TEST_F(FakeModbusClientReadTest, ReadHoldingRegisters_ReturnsTimeout_PayloadUnchanged) {
    client.nextReadResult = CommunicationResult{
        CommunicationResult::Status::Timeout, 0, "read timeout"
    };
    std::vector<uint16_t> payload = {0x0000};
    CommunicationResult result = client.readHoldingRegisters(100, 1, payload);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status, CommunicationResult::Status::Timeout);
    EXPECT_EQ(payload[0], 0x0000);
}
