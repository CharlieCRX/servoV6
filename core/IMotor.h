// core/IMotor.h
#ifndef IMOTOR_H
#define IMOTOR_H

class IMotor {
public:
    virtual ~IMotor() = default; // Virtual destructor to ensure proper cleanup of derived objects

    virtual bool setSpeed(double mmPerSec) = 0;
    virtual bool relativeMove(double mm) = 0;
    virtual bool absoluteMove(double targetMm) = 0;
    virtual bool startJog(double speedMmPerSec, bool positiveDirection) = 0;
    virtual bool stopJog() = 0;
    virtual void wait(int ms) = 0;
    virtual bool goHome() = 0;
};

#endif // IMOTOR_H
