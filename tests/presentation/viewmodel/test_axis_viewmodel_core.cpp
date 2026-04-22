#include <gtest/gtest.h>
#include "presentation/viewmodel/AxisViewModelCore.h"

// 辅助测试类：带记录功能的 Driver，用于断言指令下发
class MockDriverForVM : public IAxisDriver {
public:
    void send(const AxisCommand& cmd) override { history.push_back(cmd); }
    std::vector<AxisCommand> history;
    
    void clear() { history.clear(); }
    
    template<typename T>
    bool hasCommand() const {
        return std::any_of(history.begin(), history.end(), [](const AxisCommand& c) { return std::holds_alternative<T>(c); });
    }
};

class AxisViewModelCoreTest : public ::testing::Test {
protected:
    Axis axis;
    MockDriverForVM driver;
    
    // UseCases
    EnableUseCase enableUc{driver};
    JogAxisUseCase jogUc{driver};
    MoveAbsoluteUseCase moveAbsUc{driver};
    StopAxisUseCase stopUc{driver};

    // Orchestrators
    JogOrchestrator jogOrch{enableUc, jogUc};
    AutoAbsMoveOrchestrator absOrch{enableUc, moveAbsUc};

    // 待测目标：ViewModel Core
    std::unique_ptr<AxisViewModelCore> vm;

    void SetUp() override {
        // 实例化 ViewModel
        vm = std::make_unique<AxisViewModelCore>(axis, jogOrch, absOrch, stopUc);
        
        // 给系统一个干净的初始 Disabled 状态
        axis.applyFeedback({
            AxisState::Disabled,
            0.0, 0.0, 0.0, false, false, 1000.0, -1000.0
        });
    }
};

// 🎯 测试 1：状态投影的最小语义 —— VM 必须忠实反映 Axis 状态
TEST_F(AxisViewModelCoreTest, StateShouldReflectAxisState) {
    // 强制给底层注入一个状态
    axis.applyFeedback({AxisState::MovingAbsolute, 100.0, 0.0, 0.0, false, false, 1000.0, -1000.0});
    
    // 验证：VM 不能有任何自己的状态缓存，必须直接返回底层状态
    EXPECT_EQ(vm->state(), AxisState::MovingAbsolute);
    EXPECT_DOUBLE_EQ(vm->absPos(), 100.0);
}