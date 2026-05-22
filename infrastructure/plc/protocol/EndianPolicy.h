#pragma once

namespace plc::protocol {
  enum class ByteOrder {
    BigEndian,    // 字内高字节在前 (AB)
    LittleEndian  // 字内低字节在前 (BA)
  };
  
  enum class WordOrder {
    HighWordFirst, // 多寄存器中，高位字在前
    LowWordFirst   // 多寄存器中，低位字在前
  };
  
  struct EndianPolicy {
    ByteOrder byteOrder;
    WordOrder wordOrder;
  };
}