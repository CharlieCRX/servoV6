#pragma once

#include <variant>
#include <cstdint>
#include <string>

namespace plc::protocol {

/**
 * @brief 协议运行时的标准数据载体 (ProtocolRuntime Data Carrier)
 *
 * @details
 * 在 PLC 物理层，数据仅存在 "Bit (位)" 和 "Word (字)" 两种形态。
 * PlcValue 作为所有编解码管线（Codec Pipeline）的统一产出容器，
 * 负责将底层无类型的二进制裸数据，映射为业务层可直接消费的强类型语义数据。
 *
 * @note 领域模型与数据类型映射规约：
 * - bool    : 对应 Coil 或 Discrete Input 的布尔状态
 * (例: MOVE_DONE, HAS_ALARM, 传感器启停)
 * - int16_t   : 对应单寄存器 (16-bit) 的离散整型或枚举态
 * (例: 设备状态字 STATE_CODE, 报警码)
 * - float     : 对应双寄存器拼接的 IEEE 754 单精度浮点物理量
 * (例: 绝对位置 150.5 mm, 实时转速)
 * - std::string : 对应多寄存器 ASCII 解码的字符串信息预留
 * (例: 设备型号、固件版本号)
 */
using PlcValue = std::variant<
  bool,
  int16_t,
  float,
  std::string
>;

/**
 * @brief 类型安全的显式值提取器
 *
 * @details 
 * 提供便捷的泛型接口以提取 PlcValue 中的具体数据。
 * 在调用此函数前，强烈建议先通过 isXXX() 系列函数或 std::holds_alternative 验证当前持有的类型，
 * 以避免在工控运行环境下触发未捕获的异常导致程序崩溃。
 *
 * @tparam T 期望提取的目标类型 (必须是 bool, int16_t, float, std::string 之一)
 * @param value 待提取的 PlcValue 实例
 * @return 提取出的目标类型实值
 * @throws std::bad_variant_access 当期望类型 T 与 variant 当前实际持有的类型不符时抛出
 */
template<typename T>
T getValue(const PlcValue& value) {
  return std::get<T>(value);
}

/**
 * @name 类型判别辅助函数组
 * @brief 提供语义化的 API 替代原生 std::holds_alternative，提升业务层调用代码的可读性。
 * @{
 */

/// @brief 检查当前是否持有 bool (线圈/开关状态)
inline bool isBool(const PlcValue& v) {
  return std::holds_alternative<bool>(v);
}

/// @brief 检查当前是否持有 int16_t (整型状态字)
inline bool isInt16(const PlcValue& v) {
  return std::holds_alternative<int16_t>(v);
}

/// @brief 检查当前是否持有 float (浮点物理量)
inline bool isFloat(const PlcValue& v) {
  return std::holds_alternative<float>(v);
}

/// @brief 检查当前是否持有 std::string (字符串数据)
inline bool isString(const PlcValue& v) {
  return std::holds_alternative<std::string>(v);
}

/// @} // 结束类型判别辅助函数组

} // namespace plc::protocol