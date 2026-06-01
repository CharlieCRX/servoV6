// =============================================================================
// IClock — 时钟抽象接口 (header-only)
//
// 用途: 为 EdgeTrigger 脉冲管理提供时间注入点，使测试可完全控制时间推进。
//   生产环境使用 SteadyClock (委托 std::chrono::steady_clock)
//   测试环境使用 FakeClock (手动 advance)
//
// 设计依据: 《边沿触发协议（ManualResetEdgeTrigger）TDD 实现文档》§4
// =============================================================================

#pragma once

#include <chrono>

/**
 * @brief 时钟抽象接口，用于时间注入与测试可控
 *
 * 生产环境使用 SteadyClock，测试环境使用 FakeClock。
 * 主要用于 EdgeTrigger 脉冲管理中计算 elapsed >= pulseWidthMs。
 */
class IClock {
public:
    virtual ~IClock() = default;
    virtual std::chrono::steady_clock::time_point now() const = 0;
};

/**
 * @brief 生产环境时钟：直接委托 std::chrono::steady_clock::now()
 */
class SteadyClock : public IClock {
public:
    std::chrono::steady_clock::time_point now() const override {
        return std::chrono::steady_clock::now();
    }
};

/**
 * @brief 测试环境时钟：时间完全可控
 *
 * 初始时刻为 epoch (0)。
 * 每次调用 advance() 累加时间偏移。
 */
class FakeClock : public IClock {
public:
    std::chrono::steady_clock::time_point now() const override {
        return m_now;
    }

    void advance(std::chrono::milliseconds ms) {
        m_now += ms;
    }

private:
    std::chrono::steady_clock::time_point m_now{};
};
