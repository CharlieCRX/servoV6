#ifndef IMOTOR_H
#define IMOTOR_H

class IMotor {
public:
    virtual ~IMotor() = default;
    // --- 电机使能 ---
    virtual bool enable() = 0;
    virtual bool disable() = 0;
    virtual bool isEnabled() = 0;

    // --- 点动模式相关 ---
    virtual bool setJogRPM(int rpm) = 0;             // 设置点动转速（RPM）
    virtual int getJogRPM() const = 0;               // 获取点动转速
    virtual bool startPositiveRPMJog() = 0;          // 启动正向点动
    virtual bool startNegativeRPMJog() = 0;          // 启动反向点动
    virtual bool stopRPMJog() = 0;                   // 停止点动

    // --- 位置移动速度设置 ---
    virtual bool setMoveRPM(int rpm) = 0;            // 设置移动速度（RPM）
    virtual int getMoveRPM() const = 0;              // 获取移动速度

    // --- 位置设置（不触发移动） ---
    virtual bool setAbsoluteTargetRevolutions(double rev) = 0;   // 设置绝对目标位置（圈数）
    virtual bool setRelativeTargetRevolutions(double deltaRev) = 0; // 设置相对目标位置（增量）

    // --- 触发内部位置移动 ---
    virtual bool triggerMove() = 0;                  // 触发内部位置移动（相当于 DI 上升沿）

    // --- 移动完成判断 ---
    virtual bool waitMoveDone(int timeoutMs = 3000) = 0;  // 阻塞等待移动完成（可配置超时）
    virtual bool isMoveDone() const = 0;             // 移动是否完成（CMDOK = 1）
    virtual bool isInPosition() const = 0;           // 是否到达目标位置（COIN = 1）

    // --- 位置控制辅助功能 ---
    virtual bool setCurrentPositionAsZero() = 0;     // 设置当前位置为原点（零位）
    virtual double getCurrentRevolutions() const = 0; // 获取当前位置（单位：圈数）

    // --- 通用功能 ---
    virtual bool goHome() = 0;                       // 归零操作（可选）
    virtual void wait(int ms) = 0;                   // 延时等待
};

#endif // IMOTOR_H
