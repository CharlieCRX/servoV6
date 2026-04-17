#ifndef AXIS_H
#define AXIS_H
#pragma once
#include <optional>
#include <variant>
enum class Direction {
    Forward,
    Backward
};

enum class MoveType {
    None,
    Absolute,
    Relative
};

enum class AxisState {
    Unknown,        // 对应 0: 未知状态
    Disabled,       // 对应 1: 未使能

    // 以下状态都是使能状态的子状态
    Idle,           // 对应 2: 空闲
    Jogging,        // 对应 3: 正在 Jog
    MovingAbsolute, // 对应 4: 正在绝对定位
    MovingRelative, // 对应 5: 正在相对定位
    Error           // 对应 6: 报警
};

enum class RejectionReason {
    None,               // 无拒绝（成功）
    InvalidState,       // 轴状态非法
    AlreadyMoving,      // 已经在运动中（针对 Move 指令）

    // ⭐ 场景 1：针对 Move 指令的目标预检
    TargetOutOfPositiveLimit, // 目标位置超出了正向软限位
    TargetOutOfNegativeLimit, // 目标位置超出了负向软限位

    // ⭐ 场景 2：针对当前已经处于边界的状态拦截
    AtPositiveLimit,          // 当前已处于正向限位点（禁正向点动/定位）
    AtNegativeLimit,          // 当前已处于负向限位点（禁负向点动/定位）

    InvalidArgument
};


struct AxisFeedback {
    AxisState state;
    double absPos;
    double relPos;
    double relZeroAbsPos;

    bool posLimit;        // 正限位状态位
    bool negLimit;        // 负限位状态位
    double posLimitValue; // 正限位数值
    double negLimitValue; // 负限位数值

    double getjogVelocity;
    double getMoveVelocity;
};


struct JogCommand {
    Direction dir;
    bool active = true; // true: 开始 Jog，false: 停止 Jog
};

struct MoveCommand {
    MoveType type;   // Absolute 或 Relative
    double target;   // 目标位置或距离
    double startAbs;   // ⭐ 新增：记录发起指令那一刻的绝对位置快照
};

struct StopCommand {};

struct EnableCommand { 
    bool active; // true: 使能(上电), false: 掉电 
};

struct SetJogVelocityCommand {
    double velocity;
};

struct SetMoveVelocityCommand {
    double velocity;
};

// 坐标控制
struct ZeroAbsoluteCommand {};

struct SetRelativeZeroCommand {};
struct ClearRelativeZeroCommand {};

// 2. 更新统一意图槽位
using AxisCommand = std::variant<
    std::monostate, 
    JogCommand, 
    MoveCommand, 
    StopCommand,
    ZeroAbsoluteCommand,
    SetRelativeZeroCommand,
    ClearRelativeZeroCommand,
    EnableCommand,
    SetJogVelocityCommand,
    SetMoveVelocityCommand
>;

class Axis {
public:
    Axis();

    AxisState state() const;

    void applyFeedback(const AxisFeedback& feedback);

    bool enable(bool active);
    
    bool jog(Direction dir);
    bool stopJog(Direction dir);
    
    bool moveAbsolute(double target);
    bool moveRelative(double distance);

    bool stop();

    bool zeroAbsolutePosition();

    bool setRelativeZero();
    bool clearRelativeZero();

    // 状态查询接口
    double currentAbsolutePosition() const;
    double currentRelativePosition() const;
    double relativeZeroAbsolutePosition() const;

    // 位置移动流程查询接口
    bool isMoveInProgress() const;
    bool isMoveCompleted() const;

    // 软限位查询接口
    double positiveSoftLimit() const;
    double negativeSoftLimit() const;

    // 设置速度
    bool setJogVelocity(double v);
    bool setMoveVelocity(double v);

    // 获取系统速度设置
    double getjogVelocity() const;
    double getMoveVelocity() const;


    bool hasPendingCommand() const;
    RejectionReason lastRejection() const;
    const AxisCommand& getPendingCommand() const;

    bool hasPendingStop() const;
    

private:
    AxisState m_state;
    // 唯一的命令意图
    AxisCommand m_pending_intent = std::monostate{};

    double m_current_abs_pos = 0.0;
    double m_current_rel_pos = 0.0;

    double m_rel_zero_abs_pos = 0.0;    // PLC 记录的相对零点对应的绝对位置
    double m_expected_zero_base = 0.0;  // 记录发起指令那一刻的绝对位置，用于闭环比对

    // 镜像 PLC 的限位状态
    bool m_pos_limit_active = false;
    bool m_neg_limit_active = false;
    
    // 镜像 PLC 的限位数值
    double m_pos_limit_value = 0.0;
    double m_neg_limit_value = 0.0;

    // 速度
    double m_jog_velocity = 0.0;
    double m_move_velocity = 0.0;

    static constexpr double POSITION_EPSILON = 0.01;
    RejectionReason m_last_rejection = RejectionReason::None;
};
#endif // AXIS_H
