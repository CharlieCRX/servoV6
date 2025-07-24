// core/IMotor.h
#ifndef IMOTOR_H
#define IMOTOR_H

class IMotor {
public:
    virtual ~IMotor() = default; // Virtual destructor to ensure proper cleanup of derived objects

    virtual bool setSpeed(double mmPerSec) = 0;   // Pure virtual function: Set speed, returns true on success
    virtual bool relativeMove(double mm) = 0;     // Pure virtual function: Relative move, returns true on success
    virtual void wait(int ms) = 0;                // Pure virtual function: Pause (wait)
    virtual bool goHome() = 0;                    // Pure virtual function: Go to home position, returns true on success
};

#endif // IMOTOR_H
