// tests/test_businesslogic.cpp (新的测试夹具和用例)
#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "BusinessLogic.h"
#include "MovementCommand.h"
#include "mocks/MockServoAdapters.h" // 引入 Mock 适配器
#include "Logger.h" // 假设你有日志

using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;

// Test fixture for linear motor scenarios
class LinearServoBusinessLogicTest : public ::testing::Test {
protected:
    MockLinearServoAdapter* mockLinearAdapterRawPtr;
    std::unique_ptr<BusinessLogic> businessLogic;

    void SetUp() override {
        auto mockLinearAdapterUniquePtr = std::make_unique<MockLinearServoAdapter>();
        mockLinearAdapterRawPtr = mockLinearAdapterUniquePtr.get();

        std::map<std::string, std::unique_ptr<IServoAdapter>> adapters;
        adapters["linear_motor"] = std::move(mockLinearAdapterUniquePtr);
        businessLogic = std::make_unique<BusinessLogic>(std::move(adapters));
    }
};
