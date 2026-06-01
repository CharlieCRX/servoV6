// infrastructure/plc/protocol/RegisterRegistry.h
#pragma once
#include <vector>
#include <span>
#include <algorithm>
#include <initializer_list> // 新增头文件
#include "RegisterMetadata.h"

namespace plc::protocol {

class RegisterRegistry {
public:
  void add(const RegisterInfo& reg) {
    m_registers.push_back(reg);
  }

  // 支持传入 std::vector 或 std::array
  void addAll(std::span<const RegisterInfo> regs) {
    m_registers.insert(m_registers.end(), regs.begin(), regs.end());
  }

  // 重载：完美支持 registry.addAll({ A, B, C }) 这种语法
  void addAll(std::initializer_list<RegisterInfo> regs) {
    m_registers.insert(m_registers.end(), regs.begin(), regs.end());
  }

  const std::vector<RegisterInfo>& all() const {
    return m_registers;
  }

  std::vector<RegisterInfo> findByGroup(RegisterGroup group) const {
    std::vector<RegisterInfo> result;
    std::copy_if(m_registers.begin(), m_registers.end(), std::back_inserter(result),
           [group](const RegisterInfo& reg) { return reg.group == group; });
    return result;
  }

  std::vector<RegisterInfo> findByArea(RegisterArea area) const {
    std::vector<RegisterInfo> result;
    std::copy_if(m_registers.begin(), m_registers.end(), std::back_inserter(result),
           [area](const RegisterInfo& reg) { return reg.area == area; });
    return result;
  }

  const RegisterInfo* findByAddress(RegisterArea area, uint16_t address) const {
    auto it = std::find_if(m_registers.begin(), m_registers.end(),
                 [area, address](const RegisterInfo& reg) {
                   return reg.area == area && reg.address == address;
                 });
    if (it != m_registers.end()) {
      return &(*it);
    }
    return nullptr;
  }

private:
  std::vector<RegisterInfo> m_registers;
};

} // namespace plc::protocol