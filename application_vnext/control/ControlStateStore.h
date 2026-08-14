// ============================================================================
// ControlStateStore.h —— Phase 0：统一状态存储 + 快照类型
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》§6.1。
//
// 给 UI/UDP 发布的统一状态存储（**纯 C++、线程安全、无 Qt 依赖**）。
// 只负责保存与查询快照，不做任何订阅回调——避免在 UDP/PLC 线程持锁时回调 UI。
// Qt 层用定时读取（QTimer 内 store().snapshot()）或 QueuedConnection 刷新界面。
//
// 纯头文件：不依赖 Qt / Modbus / 旧 domain/*。
// ============================================================================
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "application_vnext/control/ControlCommand.h"
#include "application_vnext/control/OperationState.h"
#include "domain_vnext/model/AxisFunction.h"
#include "infrastructure/plc_vnext/contracts/AxisRuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/ConnectionState.h"
#include "infrastructure/plc_vnext/contracts/GantryStatusSnapshot.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"
#include "infrastructure/plc_vnext/contracts/RuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/SafetySnapshot.h"

namespace application_vnext::control {

/// 单轴 UI 状态（由 PLC 反馈快照 + 拓扑 + 租约投影）。
/// 除 slot 外必须携带 AxisTarget/group/role/hmiVisible，UI 不应自行推导轴映射。
struct AxisUiState {
    int16_t slot = 0;                       // PLC 槽位下标
    plc_vnext::contracts::PlcGroupIndex group{0};       // 组
    domain_vnext::model::AxisFunction role =            // 功能角色 X/X1/X2/Y/Z/R
        domain_vnext::model::AxisFunction::X;
    bool hmiVisible = false;                // 拓扑是否允许 UI 显示
    bool bound = false;                     // 该功能是否注册为轴实体
    bool trusted = false;                   // 本次反馈是否可信
    float absPosition = 0.0f;
    float relPosition = 0.0f;
    float manualSpeed = 0.0f;
    float positioningSpeed = 0.0f;
    int16_t motionState = 0;                // 见 AxisMotionCommon 常量（0..6）
    int16_t motionLimit = 0;
    uint16_t alarmWord = 0;

    // ---- 操作占用（来自 OperationLease）----
    bool leased = false;
    std::string leaseOperationId;           // 空 = 空闲
    ControlSource leaseOwner = ControlSource::Ui;
    std::string leaseOwnerName;             // "UDP" / "Joystick" / "UI" / "Maintenance"
};

/// 单龙门组 UI 状态。
struct GantryUiState {
    bool trusted = false;
    int16_t state = 0;                  // 0未配置 1已解除 2建立中 3已联动 4解除中 5故障
    int16_t internalStep = 0;
    int16_t commandResult = 0;
    int16_t commandErrorCode = 0;
    bool logicalControlAllowed = false;
    bool memberControlAllowed = false;
    bool readyToCouple = false;
    bool readyToDecouple = false;
    bool x1InGear = false;
    bool x2InGear = false;
    float logicalPosition = 0.0f;
    float skew = 0.0f;
    bool fault = false;
    int16_t faultCode = 0;

    // ---- 操作占用 ----
    bool lifecycleLeased = false;
    std::string lifecycleOperationId;
};

/// 一次操作的状态条目（给 UDP 回包 / UI 展示）。
struct OperationEntry {
    std::string operationId;
    std::string parentOperationId;   // 重复点动并入父会话时记录父 operationId（空=自有会话）
    ControlSource source = ControlSource::Ui;
    std::string axis;                   // "A.Y" / "A.X" ...
    OperationKind kind = OperationKind::Positioning;
    OperationState state = OperationState::Queued;
    std::string diag;
    int16_t motionState = 0;
    float position = 0.0f;
    std::chrono::steady_clock::time_point updatedAt;
};

/// 统一状态快照（MotionControlService 每 tick 发布一份，不可变）。
struct ControlStateSnapshot {
    plc_vnext::contracts::ConnectionState connection;
    plc_vnext::contracts::SafetySnapshot  safety;

    std::array<AxisUiState, plc_vnext::contracts::kRuntimeAxisCount>   axes;
    std::array<GantryUiState, plc_vnext::contracts::kRuntimeGroupCount> gantries;

    std::vector<OperationEntry> operations;   // 进行中 + 最近历史（有容量上限）
};

/// 给 UI/UDP 发布的统一状态存储（**纯 C++、线程安全、无 Qt 依赖**）。
/// 只负责保存与查询快照，不做任何订阅回调——避免在 UDP/PLC 线程持锁时回调 UI。
class ControlStateStore {
public:
    static constexpr std::size_t kMaxOperations = 100;  // 历史容量上限，防内存增长

    /// 发布快照（内部裁剪 operations 至 kMaxOperations，最旧条目被移除）。
    void publish(const ControlStateSnapshot& s) {
        std::lock_guard<std::mutex> lock(mtx_);
        snap_ = s;
        if (snap_.operations.size() > kMaxOperations) {
            // 移除最旧的（头部）条目，保留最近 kMaxOperations 条
            snap_.operations.erase(snap_.operations.begin(),
                                   snap_.operations.begin()
                                       + (snap_.operations.size() - kMaxOperations));
        }
    }

    /// 拷贝当前快照（线程安全）。
    ControlStateSnapshot snapshot() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return snap_;
    }

    /// 按 operationId 查询单个操作条目。
    std::optional<OperationEntry> findOperation(const std::string& id) const {
        std::lock_guard<std::mutex> lock(mtx_);
        for (const auto& op : snap_.operations) {
            if (op.operationId == id) return op;
        }
        return std::nullopt;
    }

private:
    mutable std::mutex mtx_;
    ControlStateSnapshot snap_;
};

}  // namespace application_vnext::control
