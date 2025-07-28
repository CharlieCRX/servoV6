// core/IServoAdapter.cpp (具体适配器实现)
#include "IServoAdapter.h"
#include "Motor.h" // 包含真实电机实现
#include "Logger.h" // 假设有日志

// LinearServoAdapter 实现
class LinearServoAdapter : public ILinearServoAdapter {
public:
    // 构造函数接收一个底层电机实例和线性转换参数
    // 例如：丝杆螺距 (mm/revolution)
    LinearServoAdapter(std::unique_ptr<IMotor> motor, double leadScrewPitchMmPerRev)
        : motor_(std::move(motor)), leadScrewPitchMmPerRev_(leadScrewPitchMmPerRev),
        currentPositionSpeedRPM_(0.0), currentJogSpeedRPM_(0.0) {
        if (!motor_) {
            LOG_ERROR("LinearServoAdapter: IMotor cannot be null.");
        }
        LOG_INFO("LinearServoAdapter initialized with pitch: {} mm/rev.", leadScrewPitchMmPerRev_);
    }

    IMotor* getMotor() override { return motor_.get(); }

    bool goHome() override { return motor_->goHome(); }
    void wait(int ms) override { motor_->wait(ms); }
    bool stopJog() override { return motor_->stopRPMJog(); }

    bool setPositionSpeed(double mmPerSec) override {
        double rpm = mmPerSec / leadScrewPitchMmPerRev_ * 60.0; // mm/s / (mm/rev) * 60s/min = rev/min (RPM)
        currentPositionSpeedRPM_ = rpm; // 保存RPM，供后续move使用
        LOG_DEBUG("Linear: Set position speed {} mm/s -> {} RPM.", mmPerSec, rpm);
        return motor_->setRPM(rpm);
    }
    bool setJogSpeed(double mmPerSec) override {
        double rpm = mmPerSec / leadScrewPitchMmPerRev_ * 60.0;
        currentJogSpeedRPM_ = rpm; // 保存RPM，供后续jog使用
        LOG_DEBUG("Linear: Set jog speed {} mm/s -> {} RPM.", mmPerSec, rpm);
        return true; // 不直接调用 setRPM，因为点动速度是独立的
    }
    bool relativeMove(double mm) override {
        double revolutions = mm / leadScrewPitchMmPerRev_;
        motor_->setRPM(currentPositionSpeedRPM_); // 确保速度已设置
        LOG_DEBUG("Linear: Relative move {} mm -> {} revolutions.", mm, revolutions);
        return motor_->relativeMoveRevolutions(revolutions);
    }
    bool absoluteMove(double targetMm) override {
        double targetRevolutions = targetMm / leadScrewPitchMmPerRev_;
        motor_->setRPM(currentPositionSpeedRPM_); // 确保速度已设置
        LOG_DEBUG("Linear: Absolute move {} mm -> {} revolutions.", targetMm, targetRevolutions);
        return motor_->absoluteMoveRevolutions(targetRevolutions);
    }
    bool startPositiveJog() override {
        motor_->setRPM(currentJogSpeedRPM_); // 设置点动速度
        LOG_DEBUG("Linear: Start positive jog.");
        return motor_->startPositiveRPMJog();
    }
    bool startNegativeJog() override {
        motor_->setRPM(currentJogSpeedRPM_); // 设置点动速度
        LOG_DEBUG("Linear: Start negative jog.");
        return motor_->startNegativeRPMJog();
    }

private:
    std::unique_ptr<IMotor> motor_;
    double leadScrewPitchMmPerRev_; // 丝杆螺距或转换系数
    double currentPositionSpeedRPM_; // 内部维护的速度状态
    double currentJogSpeedRPM_; // 内部维护的点动速度状态
};

// RotaryServoAdapter 实现
class RotaryServoAdapter : public IRotaryServoAdapter {
public:
    // 构造函数接收一个底层电机实例和角度转换参数
    // 例如：每圈多少度 (通常是 360.0)
    RotaryServoAdapter(std::unique_ptr<IMotor> motor, double degreesPerRevolution)
        : motor_(std::move(motor)), degreesPerRevolution_(degreesPerRevolution),
        currentAngularPositionSpeedRPM_(0.0), currentAngularJogSpeedRPM_(0.0) {
        if (!motor_) {
            LOG_ERROR("RotaryServoAdapter: IMotor cannot be null.");
        }
        LOG_INFO("RotaryServoAdapter initialized with degrees/rev: {}.", degreesPerRevolution_);
    }

    IMotor* getMotor() override { return motor_.get(); }

    bool goHome() override { return motor_->goHome(); }
    void wait(int ms) override { motor_->wait(ms); }
    bool stopJog() override { return motor_->stopRPMJog(); }

    bool setAngularPositionSpeed(double degreesPerSec) override {
        double rpm = degreesPerSec / degreesPerRevolution_ * 60.0; // deg/s / (deg/rev) * 60s/min = rev/min (RPM)
        currentAngularPositionSpeedRPM_ = rpm; // 保存RPM
        LOG_DEBUG("Rotary: Set angular position speed {} deg/s -> {} RPM.", degreesPerSec, rpm);
        return motor_->setRPM(rpm);
    }
    bool setAngularJogSpeed(double degreesPerSec) override {
        double rpm = degreesPerSec / degreesPerRevolution_ * 60.0;
        currentAngularJogSpeedRPM_ = rpm; // 保存RPM
        LOG_DEBUG("Rotary: Set angular jog speed {} deg/s -> {} RPM.", degreesPerSec, rpm);
        return true; // 不直接调用 setRPM
    }
    bool angularMove(double degrees) override {
        double revolutions = degrees / degreesPerRevolution_;
        motor_->setRPM(currentAngularPositionSpeedRPM_); // 确保速度已设置
        LOG_DEBUG("Rotary: Move {} degrees -> {} revolutions.", degrees, revolutions);
        return motor_->relativeMoveRevolutions(revolutions); // 角度移动通常是相对的
    }
    bool startPositiveAngularJog() override {
        motor_->setRPM(currentAngularJogSpeedRPM_); // 设置点动速度
        LOG_DEBUG("Rotary: Start positive angular jog.");
        return motor_->startPositiveRPMJog();
    }
    bool startNegativeAngularJog() override {
        motor_->setRPM(currentAngularJogSpeedRPM_); // 设置点动速度
        LOG_DEBUG("Rotary: Start negative angular jog.");
        return motor_->startNegativeRPMJog();
    }

private:
    std::unique_ptr<IMotor> motor_;
    double degreesPerRevolution_; // 每圈多少度，通常是 360.0
    double currentAngularPositionSpeedRPM_;
    double currentAngularJogSpeedRPM_;
};
