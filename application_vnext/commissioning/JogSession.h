// ============================================================================
// JogSession.h —— 阶段3：点动心跳会话（Step 3.1）
// ============================================================================
// 点动心跳必须由“底层会话”维护，不能由 UI 按钮或上层临时定时器维护。
// 本类只负责“点动心跳”的周期刷新与生命周期：
//   - start(): 启动会话线程；线程先立即写一次心跳 ON，再按周期写 ON；
//   - 任一心跳写失败 → 会话自动退出并回调 onFailure（上层据此写点动方向 OFF，
//     禁止自动重放点动命令，依赖 PLC ~3s 看门狗停止点动）；
//   - stop(writeOff): 停止心跳线程并 join；writeOff=true 时补写心跳 OFF
//     （PLC 会自动清心跳，但停止时补写 OFF 有利于状态收尾与测试确认）；
//   - 断线恢复后不自动重放旧点动（本类不缓存、不重连、不重放）。
//
// 心跳周期默认 500ms（协议超时约 3000ms，留有安全余量），可在 Config 调整
// （离线测试用短周期，不改变生产默认）。
// ============================================================================
#pragma once

#include <atomic>
#include <functional>
#include <thread>

namespace application_vnext::commissioning {

class JogSession {
public:
    struct Config {
        int heartbeatPeriodMs = 500;  ///< 心跳周期，默认 500ms
    };

    /// 心跳 ON 写回调：返回 false 表示通讯失败（会话据此退出）。
    using HeartbeatWriter = std::function<bool()>;
    /// 心跳 OFF 写回调（可选；stop(writeOff=true) 时调用）。
    using HeartbeatOffWriter = std::function<bool()>;
    /// 心跳失败回调（可选；用于关闭方向线圈、置忙位等）。
    using FailureCallback = std::function<void()>;

    JogSession(HeartbeatWriter heartbeat,
               HeartbeatOffWriter heartbeatOff,
               FailureCallback onFailure,
               Config config);
    ~JogSession();

    JogSession(const JogSession&) = delete;
    JogSession& operator=(const JogSession&) = delete;

    /// 启动心跳线程并立即写一次心跳 ON；写失败则不启动线程并返回 false。
    bool start();

    /// 停止心跳线程（join）；writeOff=true 时补写心跳 OFF。
    /// 返回心跳 OFF 写是否成功（仅 writeOff=true 时有意义；未注入 OFF 回调视为成功）。
    bool stop(bool writeOff = true);

    /// 是否处于心跳运行中。
    bool active() const;

    /// 心跳是否因写失败而非正常退出（区别于用户主动 stop）。
    bool failed() const;

private:
    void loop();

    HeartbeatWriter heartbeat_;
    HeartbeatOffWriter heartbeatOff_;
    FailureCallback onFailure_;
    Config config_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> failed_{false};
};

}  // namespace application_vnext::commissioning
