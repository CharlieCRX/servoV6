#pragma once

/**
 * @brief 系统安全状态 — 五态工业安全状态机
 *
 * 设计依据：
 *   1. PLC 拥有独立寄存器：设备急停（命令） / 设备急停中（状态反馈）
 *   2. 上位机状态必须与 PLC 物理状态保持最终一致
 *   3. 只表达"系统运行许可"，不表达轴使能状态（轴生命周期由 AxisState 负责）
 *   4. 启动同步：程序启动时 PLC 可能已处于急停中，必须通过 synchronize() 获取真相
 *
 * 状态流转：
 *   ┌─────────────────────────────────────────────────────────────────┐
 *   │  NotSynchronized ──synchronize(plcEmergencyStopped)─────────────│
 *   │       │                                                         │
 *   │       ├── plcEmergencyStopped == false ──> Running              │
 *   │       │        │                                                │
 *   │       │        ├── requestEmergencyStop() ──> EmergencyStopping │
 *   │       │        │         │                                      │
 *   │       │        │         └── feedback(true) ──> EmergencyStopped│
 *   │       │        │                                     │          │
 *   │       │        │    requestReleaseEmergencyStop()    │          │
 *   │       │        │         │                           │          │
 *   │       │        │         ▼                           │          │
 *   │       │        │  ReleasingEmergencyStop             │          │
 *   │       │        │         │                           │          │
 *   │       │        │         └── feedback(false) ──> Running         │
 *   │       │        │                                                │
 *   │       │        └── feedback(true) ──> EmergencyStopped          │
 *   │       │                                                         │
 *   │       └── plcEmergencyStopped == true ──> EmergencyStopped      │
 *   │                                                                 │
 *   └─────────────────────────────────────────────────────────────────┘
 *
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  核心原则：                                                  ║
 * ║  1. 上位机状态由 PLC 反馈确认后变更，不由请求立即变更         ║
 * ║  2. 安全状态只负责"系统是否允许运动"，不负责"轴是否已使能"   ║
 * ║  3. servoV6 默认 Disabled + EnsureEnabled，轴生命周期独立     ║
 * ║  4. Controller 永远相信 PLC Feedback，不与物理真相为敌        ║
 * ║  5. 启动时绝不假设系统安全，必须先同步 PLC 真实状态            ║
 * ╚══════════════════════════════════════════════════════════════╝
 */
enum class SafetyState {
    // ==========================================
    // 状态 0：NotSynchronized — 初始启动态，尚未同步 PLC 真实安全状态
    // ==========================================
    // • 程序刚启动，软件不知道 PLC 是否处于急停中
    // • 禁止一切运动和使能（真相未知，宁可保守）
    // • 必须调用 synchronize(plcEmergencyStopped) 才能退出此状态
    // • 退出路径：
    //     synchronize(false) → Running（PLC 未急停）
    //     synchronize(true)  → EmergencyStopped（PLC 已急停）
    // • 拒绝所有 requestEmergencyStop() / requestReleaseEmergencyStop()
    NotSynchronized,

    // ==========================================
    // 状态 1：Running — 系统运行许可有效
    // ==========================================
    // • 所有轴可运动、可使能
    // • 轴默认 Disabled，运动时由 enable() 自动使能
    // • PLC 寄存器：设备急停中 = FALSE
    Running,

    // ==========================================
    // 状态 2：EmergencyStopping — 急停命令已下发，等待 PLC 停机确认
    // ==========================================
    // • 禁止一切运动和使能
    // • UI 显示"急停处理中..."
    // • PLC 寄存器：设备急停 = TRUE，设备急停中 可能仍为 FALSE（尚未完成）
    EmergencyStopping,

    // ==========================================
    // 状态 3：EmergencyStopped — 系统已锁定
    // ==========================================
    // • 禁止一切运动和使能
    // • UI 显示"设备已急停"，运动控件全部置灰
    // • 状态信息（轴位置、PLC 连接状态等）仍可见
    // • PLC 寄存器：设备急停中 = TRUE
    EmergencyStopped,

    // ==========================================
    // 状态 4：ReleasingEmergencyStop — 解除命令已下发，等待 PLC 解锁确认
    // ==========================================
    // • 禁止一切运动
    // • UI 显示"急停解除中..."
    // • PLC 寄存器：设备急停 = FALSE，设备急停中 可能仍为 TRUE（尚未解除）
    ReleasingEmergencyStop
};
