// core/BusinessLogic.h (管理 IServoAdapter)
#ifndef BUSINESS_LOGIC_H
#define BUSINESS_LOGIC_H

#include "IServoAdapter.h" // 引入适配器接口
#include "MovementCommand.h"
#include <map>
#include <string>
#include <vector>
#include <memory>

class BusinessLogic {
public:
    // 现在接收 IServoAdapter 的 map
    BusinessLogic(std::map<std::string, std::unique_ptr<IServoAdapter>> adapters);
    ~BusinessLogic();

    bool executeCommandSequence(const std::string& motorId, const CommandSequence& commands);

private:
    std::map<std::string, std::unique_ptr<IServoAdapter>> adapterMap; // <-- 关键改变
};

#endif // BUSINESS_LOGIC_H
