// infrastructure/plc/protocol/validator/ProtocolConstraintValidator.cpp
#include "ProtocolConstraintValidator.h"
#include "infrastructure/plc/protocol/RegisterRegistry.h"
#include "infrastructure/plc/protocol/RegisterMetadata.h"
#include <algorithm>
#include <string>

namespace plc::protocol {

std::vector<ProtocolViolation> ProtocolConstraintValidator::validate(
  const RegisterRegistry& registry)
{
  std::vector<ProtocolViolation> violations;

  checkAddressOverlap(registry, violations);   // R01
  checkCoilType(registry, violations);      // R02
  checkFeedbackAccess(registry, violations);  // R03
  checkPulseWidth(registry, violations);    // R04

  return violations;
}

void ProtocolConstraintValidator::checkAddressOverlap(
  const RegisterRegistry& registry,
  std::vector<ProtocolViolation>& out)
{
  const auto& all = registry.all();
  for (size_t i = 0; i < all.size(); ++i) {
    for (size_t j = i + 1; j < all.size(); ++j) {
      const auto& a = all[i];
      const auto& b = all[j];

      // 只有同一 Area 才可能重叠
      if (a.area != b.area) continue;

      // a 占用的地址范围：[a.address, a.address + a.wordCount() - 1]
      // b 占用的地址范围：[b.address, b.address + b.wordCount() - 1]
      uint16_t aEnd = a.address + a.wordCount() - 1;
      uint16_t bEnd = b.address + b.wordCount() - 1;

      // 区间重叠判断
      bool overlaps = (a.address <= bEnd) && (b.address <= aEnd);

      if (overlaps) {
        out.push_back({
          ProtocolViolation::Severity::Error,
          "R01",
          "Address overlap: [" + std::string(a.description) +
          "] occupies [" + std::to_string(a.address) + "-" +
          std::to_string(aEnd) + "], conflicts with [" +
          std::string(b.description) + "] at [" +
          std::to_string(b.address) + "-" +
          std::to_string(bEnd) + "]",
          &a,
          &b
        });
      }
    }
  }
}

void ProtocolConstraintValidator::checkCoilType(
  const RegisterRegistry& registry,
  std::vector<ProtocolViolation>& out)
{
  for (const auto& reg : registry.all()) {
    if (reg.area == RegisterArea::Coil && reg.type != RegisterType::Bool) {
      out.push_back({
        ProtocolViolation::Severity::Error,
        "R02",
        "Coil register must be Bool type: [" +
        std::string(reg.description) + "] at address " +
        std::to_string(reg.address),
        &reg,
        nullptr
      });
    }
  }
}

void ProtocolConstraintValidator::checkFeedbackAccess(
  const RegisterRegistry& registry,
  std::vector<ProtocolViolation>& out)
{
  for (const auto& reg : registry.all()) {
    if (reg.group == RegisterGroup::Feedback &&
      reg.access != RegisterAccess::ReadOnly) {
      out.push_back({
        ProtocolViolation::Severity::Error,
        "R03",
        "Feedback group must be ReadOnly: [" +
        std::string(reg.description) + "] at address " +
        std::to_string(reg.address),
        &reg,
        nullptr
      });
    }
  }
}

void ProtocolConstraintValidator::checkPulseWidth(
  const RegisterRegistry& registry,
  std::vector<ProtocolViolation>& out)
{
  for (const auto& reg : registry.all()) {
    if (reg.behavior == RegisterBehavior::ManualResetEdgeTrigger &&
      reg.pulseWidthMs == 0) {
      out.push_back({
        ProtocolViolation::Severity::Error,
        "R04",
        "ManualResetEdgeTrigger requires pulseWidthMs > 0: [" +
        std::string(reg.description) + "] at address " +
        std::to_string(reg.address),
        &reg,
        nullptr
      });
    }
  }
}

} // namespace plc::protocol
