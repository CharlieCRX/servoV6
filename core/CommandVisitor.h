#ifndef COMMANDVISITOR_H
#define COMMANDVISITOR_H

#include "IMotor.h" // 需要 IMotor 接口
#include "MovementCommand.h" // 需要所有命令结构体

// 定义一个访问者接口
class CommandVisitor {
public:
    virtual ~CommandVisitor() = default;

    // 为每个命令类型定义一个访问方法
    virtual bool visit(IMotor* motor, const SetPositionSpeed& cmd) = 0;
    virtual bool visit(IMotor* motor, const SetJogSpeed& cmd) = 0;
    virtual bool visit(IMotor* motor, const RelativeMove& cmd) = 0;
    virtual bool visit(IMotor* motor, const Wait& cmd) = 0;
    virtual bool visit(IMotor* motor, const GoHome& cmd) = 0;
    virtual bool visit(IMotor* motor, const AbsoluteMove& cmd) = 0;
    virtual bool visit(IMotor* motor, const StartPositiveJog& cmd) = 0;
    virtual bool visit(IMotor* motor, const StartNegativeJog& cmd) = 0;
    virtual bool visit(IMotor* motor, const StopJog& cmd) = 0;
    // ... 未来添加新命令时，这里也要新增对应的 visit 方法
};
#endif // COMMANDVISITOR_H
