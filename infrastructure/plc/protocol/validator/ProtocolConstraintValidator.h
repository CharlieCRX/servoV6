// infrastructure/plc/protocol/validator/ProtocolConstraintValidator.h
#pragma once
#include <vector>
#include "ProtocolViolation.h"

namespace plc::protocol {

class RegisterRegistry;

class ProtocolConstraintValidator {
public:
  /// @brief 验证整个寄存器注册表
  /// @return 违规列表（空 = 协议合法，可安全进入 Runtime）
  std::vector<ProtocolViolation> validate(const RegisterRegistry& registry);

private:
  // Rule 1: 地址重叠检查
  void checkAddressOverlap(
    const RegisterRegistry& registry,
    std::vector<ProtocolViolation>& out);

  // Rule 2: Coil 只能 Bool
  void checkCoilType(
    const RegisterRegistry& registry,
    std::vector<ProtocolViolation>& out);

  // Rule 3: Feedback 必须 ReadOnly
  void checkFeedbackAccess(
    const RegisterRegistry& registry,
    std::vector<ProtocolViolation>& out);

  // Rule 4: ManualResetEdgeTrigger 必须 pulseWidth > 0
  void checkPulseWidth(
    const RegisterRegistry& registry,
    std::vector<ProtocolViolation>& out);
};

} // namespace plc::protocol
