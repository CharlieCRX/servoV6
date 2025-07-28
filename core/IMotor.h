// core/IMotor.h
#ifndef IMOTOR_H
#define IMOTOR_H

class IMotor {
public:
    virtual ~IMotor() = default; // Virtual destructor to ensure proper cleanup of derived objects

    // 设定底层转速 (RPM)
    virtual bool setRPM(double rpm) = 0;
    // 相对移动指定圈数
    virtual bool relativeMoveRevolutions(double revolutions) = 0;
    // 绝对移动到指定圈数位置
    virtual bool absoluteMoveRevolutions(double targetRevolutions) = 0;
    // 启动正向点动（以当前设定RPM）
    virtual bool startPositiveRPMJog() = 0;
    // 启动负向点动（以当前设定RPM）
    virtual bool startNegativeRPMJog() = 0;
    // 停止点动
    virtual bool stopRPMJog() = 0;

    // 通用电机操作（例如归零，等待，通常也通过转速和圈数实现）
    virtual bool goHome() = 0;
    virtual void wait(int ms) = 0;

    // TODO: 可能需要一个获取当前位置（圈数）的方法
    // virtual double getCurrentRevolutions() = 0;
};

#endif // IMOTOR_H
