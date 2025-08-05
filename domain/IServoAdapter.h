// domain/IServoAdapter.h (业务适配器接口)
#ifndef ISERVO_ADAPTER_H
#define ISERVO_ADAPTER_H

#include "IMotor.h" // 适配器内部需要操作 IMotor

// 基类 IServoAdapter，包含所有电机类型可能有的通用业务操作
class IServoAdapter {
public:
    virtual ~IServoAdapter() = default;

    // 通用操作，这些操作可能不直接涉及mm或度，或者在适配器内部可以统一处理
    virtual bool goHome() = 0;
    virtual void wait(int ms) = 0;
    virtual bool stopJog() = 0; // 停止点动，视为通用，因为停止是停止所有运动
    virtual bool setCurrentPositionAsZero() = 0;
    virtual bool emergencyStop() = 0;
    virtual bool initEnvironment() = 0;

    // 返回底层电机实例，供具体适配器实现调用
    virtual IMotor* getMotor() = 0;
};

// 线性电机适配器接口
class ILinearServoAdapter : public IServoAdapter {
public:
    virtual ~ILinearServoAdapter() = default;

    virtual bool setPositionSpeed(double mmPerSec) = 0;
    virtual bool setJogSpeed(double mmPerSec) = 0;
    virtual bool relativeMove(double mm) = 0;
    virtual bool absoluteMove(double targetMm) = 0;
    virtual bool startPositiveJog() = 0;
    virtual bool startNegativeJog() = 0;
    virtual double getCurrentPositionMm() const = 0;
};

// 旋转电机适配器接口
class IRotaryServoAdapter : public IServoAdapter {
public:
    virtual ~IRotaryServoAdapter() = default;

    virtual bool setAngularPositionSpeed(double degreesPerSec) = 0;
    virtual bool setAngularJogSpeed(double degreesPerSec) = 0;
    virtual bool relativeAngularMove(double degrees) = 0;
    virtual bool absoluteAngularMove(double targetDegrees) = 0;
    virtual bool startPositiveAngularJog() = 0;
    virtual bool startNegativeAngularJog() = 0;
    virtual double getCurrentAngleDegrees() const = 0;
};

#endif // ISERVO_ADAPTER_H
