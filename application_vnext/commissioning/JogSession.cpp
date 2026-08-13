// ============================================================================
// JogSession.cpp —— 阶段3：点动心跳会话实现
// ============================================================================
#include "application_vnext/commissioning/JogSession.h"

#include <chrono>
#include <utility>

namespace application_vnext::commissioning {

JogSession::JogSession(HeartbeatWriter heartbeat, HeartbeatOffWriter heartbeatOff,
                       FailureCallback onFailure, Config config)
    : heartbeat_(std::move(heartbeat)),
      heartbeatOff_(std::move(heartbeatOff)),
      onFailure_(std::move(onFailure)),
      config_(config) {}

JogSession::~JogSession() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

bool JogSession::start() {
    if (running_.load()) return false;
    failed_ = false;
    running_ = true;
    thread_ = std::thread(&JogSession::loop, this);
    return true;
}

bool JogSession::stop(bool writeOff) {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    bool offOk = true;
    if (writeOff && heartbeatOff_) offOk = heartbeatOff_();
    return offOk;
}

bool JogSession::active() const {
    return running_.load();
}

bool JogSession::failed() const {
    return failed_.load();
}

void JogSession::loop() {
    // 1. 立即写一次心跳 ON（点动方向 ON 前，心跳已开始刷新）。
    if (!heartbeat_()) {
        failed_ = true;
        running_ = false;
        if (onFailure_) onFailure_();
        return;
    }
    // 2. 按周期持续写心跳 ON；任一次失败即退出会话（不重放、不重试点动）。
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.heartbeatPeriodMs));
        if (!running_.load()) break;
        if (!heartbeat_()) {
            failed_ = true;
            running_ = false;
            if (onFailure_) onFailure_();
            break;
        }
    }
}

}  // namespace application_vnext::commissioning
