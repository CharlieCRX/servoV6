// ============================================================================
// FakeControlRuntime.h —— Phase 0 fake: IControlRuntime 的纯内存假实现
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》Phase 0 测试文件清单。
//
// 实现 application_vnext::control::IControlRuntime 的纯内存假实现，供
// MotionControlService（Phase 1+）与后续阶段 TDD 复用。直接产出已解码的
// contracts DTO，测试无需构造 Modbus 寄存器。
//
// 用法（显式脚本优先，与 FakePlcRuntimeGateway 同风格）：
//   - setRuntimeSnapshot / setSafetySnapshot：readRuntime()/readSafety() 默认返回脚本快照；
//     未脚本化时返回 Transport 失败（强制测试显式声明预期）。
//   - scriptRuntimeReadFailure / scriptSafetyReadFailure / clear*：覆盖快照，模拟通讯故障。
//   - setConnected()：驱动 connectionState()。
//   - emergencyStopCallCount() / emergencyStopReleaseCallCount()：断言急停写动作。
//
// 纯头文件：只依赖 contracts 纯 DTO 与 IControlRuntime 接口；不依赖 Qt / Domain。
// 线程安全（内部互斥锁），可与并发测试配合。
// ============================================================================
#pragma once

#include <mutex>
#include <optional>
#include <string>

#include "application_vnext/control/IControlRuntime.h"
#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/contracts/ConnectionState.h"
#include "infrastructure/plc_vnext/contracts/ReadResult.h"
#include "infrastructure/plc_vnext/contracts/RuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/SafetySnapshot.h"

namespace application_vnext::control {

class FakeControlRuntime : public IControlRuntime {
public:
    // ============ 脚本化：状态 ============
    /// 设置 readRuntime() 默认返回的快照（覆盖之前脚本）。
    void setRuntimeSnapshot(plc_vnext::contracts::RuntimeSnapshot snap) {
        std::lock_guard<std::mutex> lock(mtx_);
        runtime_ = std::move(snap);
        runtimeFailure_.reset();
    }
    /// 设置 readSafety() 默认返回的快照（覆盖之前脚本）。
    void setSafetySnapshot(plc_vnext::contracts::SafetySnapshot snap) {
        std::lock_guard<std::mutex> lock(mtx_);
        safety_ = std::move(snap);
        safetyFailure_.reset();
    }
    /// 驱动 connectionState() 的连接位。
    void setConnected(bool connected, std::string diagnostic = {}) {
        std::lock_guard<std::mutex> lock(mtx_);
        conn_ = connected ? plc_vnext::contracts::ConnectionState::connectedState(diagnostic)
                          : plc_vnext::contracts::ConnectionState::disconnectedState(diagnostic);
    }

    // ============ 脚本化：故障（sticky，直到清除）============
    void scriptRuntimeReadFailure(
        plc_vnext::contracts::ReadResult<plc_vnext::contracts::RuntimeSnapshot>::FailureKind kind,
        std::string diagnostic) {
        std::lock_guard<std::mutex> lock(mtx_);
        runtimeFailure_ = kind;
        runtimeDiag_ = std::move(diagnostic);
    }
    void clearRuntimeReadFailure() {
        std::lock_guard<std::mutex> lock(mtx_);
        runtimeFailure_.reset();
    }
    void scriptSafetyReadFailure(
        plc_vnext::contracts::ReadResult<plc_vnext::contracts::SafetySnapshot>::FailureKind kind,
        std::string diagnostic) {
        std::lock_guard<std::mutex> lock(mtx_);
        safetyFailure_ = kind;
        safetyDiag_ = std::move(diagnostic);
    }
    void clearSafetyReadFailure() {
        std::lock_guard<std::mutex> lock(mtx_);
        safetyFailure_.reset();
    }

    // ============ 记录（供断言）============
    /// readRuntime() 被调用次数（Phase 1 验证「每 tick runtime 恰好读取一次」）。
    unsigned readRuntimeCallCount() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return readRuntimeCount_;
    }
    /// readSafety() 被调用次数（Phase 1 验证每 tick safety 读取次数）。
    unsigned readSafetyCallCount() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return readSafetyCount_;
    }
    unsigned emergencyStopCallCount() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return estopTriggerCount_;
    }
    unsigned emergencyStopReleaseCallCount() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return estopReleaseCount_;
    }

    // ============ IControlRuntime ============
    plc_vnext::contracts::ReadResult<plc_vnext::contracts::RuntimeSnapshot> readRuntime() override {
        std::lock_guard<std::mutex> lock(mtx_);
        ++readRuntimeCount_;
        if (runtimeFailure_) {
            return plc_vnext::contracts::ReadResult<plc_vnext::contracts::RuntimeSnapshot>::failure(
                *runtimeFailure_, runtimeDiag_);
        }
        if (!runtime_) {
            return plc_vnext::contracts::ReadResult<plc_vnext::contracts::RuntimeSnapshot>::failure(
                plc_vnext::contracts::ReadResult<plc_vnext::contracts::RuntimeSnapshot>::FailureKind::Transport,
                "FakeControlRuntime: runtime snapshot not scripted");
        }
        return plc_vnext::contracts::ReadResult<plc_vnext::contracts::RuntimeSnapshot>::success(*runtime_);
    }

    plc_vnext::contracts::ReadResult<plc_vnext::contracts::SafetySnapshot> readSafety() override {
        std::lock_guard<std::mutex> lock(mtx_);
        ++readSafetyCount_;
        if (safetyFailure_) {
            return plc_vnext::contracts::ReadResult<plc_vnext::contracts::SafetySnapshot>::failure(
                *safetyFailure_, safetyDiag_);
        }
        if (!safety_) {
            return plc_vnext::contracts::ReadResult<plc_vnext::contracts::SafetySnapshot>::failure(
                plc_vnext::contracts::ReadResult<plc_vnext::contracts::SafetySnapshot>::FailureKind::Transport,
                "FakeControlRuntime: safety snapshot not scripted");
        }
        return plc_vnext::contracts::ReadResult<plc_vnext::contracts::SafetySnapshot>::success(*safety_);
    }

    plc_vnext::contracts::ConnectionState connectionState() const override {
        std::lock_guard<std::mutex> lock(mtx_);
        return conn_;
    }

    plc_vnext::contracts::CommunicationResult triggerEmergencyStop() override {
        std::lock_guard<std::mutex> lock(mtx_);
        ++estopTriggerCount_;
        return plc_vnext::contracts::CommunicationResult::sent();
    }

    plc_vnext::contracts::CommunicationResult requestEmergencyStopRelease() override {
        std::lock_guard<std::mutex> lock(mtx_);
        ++estopReleaseCount_;
        return plc_vnext::contracts::CommunicationResult::sent();
    }

private:
    mutable std::mutex mtx_;

    // 状态脚本
    std::optional<plc_vnext::contracts::RuntimeSnapshot> runtime_;
    std::optional<plc_vnext::contracts::SafetySnapshot> safety_;
    plc_vnext::contracts::ConnectionState conn_ =
        plc_vnext::contracts::ConnectionState::connectedState("up");

    // 故障脚本（sticky）
    std::optional<plc_vnext::contracts::ReadResult<plc_vnext::contracts::RuntimeSnapshot>::FailureKind>
        runtimeFailure_;
    std::string runtimeDiag_;
    std::optional<plc_vnext::contracts::ReadResult<plc_vnext::contracts::SafetySnapshot>::FailureKind>
        safetyFailure_;
    std::string safetyDiag_;

    // 记录
    unsigned readRuntimeCount_ = 0;
    unsigned readSafetyCount_ = 0;
    unsigned estopTriggerCount_ = 0;
    unsigned estopReleaseCount_ = 0;
};

}  // namespace application_vnext::control

