// infrastructure/plc/protocol/validator/ProtocolViolation.h
#pragma once
#include <string>
#include <cstdint>

namespace plc::protocol {

struct RegisterInfo; // 前向声明

struct ProtocolViolation {
  enum class Severity { Error, Warning };

  Severity severity;
  std::string ruleId;               // 规则 ID，如 "R01"
  std::string description;          // 人类可读描述
  const RegisterInfo* regA;        // 涉及的寄存器 A（主）
  const RegisterInfo* regB;        // 涉及的寄存器 B（冲突场景，可为 nullptr）
};

} // namespace plc::protocol
