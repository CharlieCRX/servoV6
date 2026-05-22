// infrastructure/plc/protocol/ProtocolProfile.h
#pragma once
#include <string_view>
#include "EndianPolicy.h"

namespace plc::protocol {

  struct ProtocolProfile {
    std::string_view name;
    EndianPolicy defaultEndian;
    uint16_t maxReadRegisters;
    uint16_t maxWriteRegisters;
    bool coilUsesFF00;
    bool supportsMixedEndian;
  };

  // 汇川预设
  constexpr ProtocolProfile INOVANCE_PROFILE {
    "Inovance_H5U&Easy",
    { ByteOrder::BigEndian, WordOrder::LowWordFirst },
    120,
    120,
    true,
    false
  };
}