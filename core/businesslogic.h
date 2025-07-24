// core/BusinessLogic.h
#ifndef BUSINESS_LOGIC_H
#define BUSINESS_LOGIC_H

#include "IMotor.h"
#include "MovementCommand.h"
#include <map>
#include <string>
#include <vector>
#include <memory> // For std::unique_ptr

class BusinessLogic {
public:
    // 构造函数接收一个电机ID到IMotor实例的映射
    BusinessLogic(std::map<std::string, std::unique_ptr<IMotor>> motors);
    ~BusinessLogic();

    // 核心业务方法：执行指定电机的命令序列
    bool executeCommandSequence(const std::string& motorId, const CommandSequence& commands);

private:
    std::map<std::string, std::unique_ptr<IMotor>> motorMap;
};

#endif // BUSINESS_LOGIC_H
