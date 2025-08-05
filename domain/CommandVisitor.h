#ifndef COMMAND_VISITOR_H
#define COMMAND_VISITOR_H

#include "IServoAdapter.h"
#include "MovementCommand.h"
// 此文件负责定义访问者接口，将命令类型与适配器类型绑定
class CommandVisitor {
public:
    virtual ~CommandVisitor() = default;

    // 线性运动命令，接收 ILinearServoAdapter
    virtual bool visit(ILinearServoAdapter* adapter, const SetPositionSpeed& cmd) = 0;
    virtual bool visit(ILinearServoAdapter* adapter, const SetJogSpeed& cmd) = 0;
    virtual bool visit(ILinearServoAdapter* adapter, const RelativeMove& cmd) = 0;
    virtual bool visit(ILinearServoAdapter* adapter, const AbsoluteMove& cmd) = 0;
    virtual bool visit(ILinearServoAdapter* adapter, const StartPositiveJog& cmd) = 0;
    virtual bool visit(ILinearServoAdapter* adapter, const StartNegativeJog& cmd) = 0;

    // 旋转运动命令，接收 IRotaryServoAdapter
    virtual bool visit(IRotaryServoAdapter* adapter, const SetAngularPositionSpeed& cmd) = 0;
    virtual bool visit(IRotaryServoAdapter* adapter, const SetAngularJogSpeed& cmd) = 0;
    virtual bool visit(IRotaryServoAdapter* adapter, const RelativeAngularMove& cmd) = 0; // 修改
    virtual bool visit(IRotaryServoAdapter* adapter, const AbsoluteAngularMove& cmd) = 0; // 修改
    virtual bool visit(IRotaryServoAdapter* adapter, const StartPositiveAngularJog& cmd) = 0;
    virtual bool visit(IRotaryServoAdapter* adapter, const StartNegativeAngularJog& cmd) = 0;

    // 通用命令，接收 IServoAdapter 基类
    virtual bool visit(IServoAdapter* adapter, const Wait& cmd) = 0;
    virtual bool visit(IServoAdapter* adapter, const GoHome& cmd) = 0;
    virtual bool visit(IServoAdapter* adapter, const StopJog& cmd) = 0;
    virtual bool visit(IServoAdapter* adapter, const InitEnvironment& cmd) = 0;    // 新增
    virtual bool visit(IServoAdapter* adapter, const EmergencyStop& cmd) = 0; // 新增
};

#endif // COMMAND_VISITOR_H
