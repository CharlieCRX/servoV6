// infrastructure/plc/protocol/PlcPoller.h
// P3: PlcPoller — PLC 现场照片采集器
//
// 职责：
// 1. 将 RegisterRegistry 的 Coil/HoldingReg 地址表线性化
// 2. 生成 FC01 / FC03 的地址范围与请求指令
// 3. 传入 raw bits/words 响应数据，拼装成 PlcSnapshot
//
// 设计约束：
// - 无 I/O：不持有 socket/ModbusClient，只处理地址→指令、响应→快照的纯数据变换
// - 无状态：每帧调用返回新的 PlcSnapshot，不缓存历史
// - 高效连续地址打包：将离散寄存器按连续区间合并，减少 FC 调用次数

#pragma once
#include <cstdint>
#include <vector>
#include <functional>
#include <optional>
#include "RegisterMetadata.h"
#include "RegisterRegistry.h"
#include "MemorySnapshot.h"
#include "PlcSnapshot.h"

namespace plc::protocol {

/**
 * @brief 一个连续地址区间 (用于打包 FC01/FC03 请求)
 */
struct AddressRange {
  uint16_t startAddress;
  uint16_t count;
};

/**
 * @brief 一次读 Coil (FC01) 的请求参数
 */
struct CoilReadRequest {
  AddressRange range;
};

/**
 * @brief 一次读 HoldingReg (FC03) 的请求参数
 */
struct WordReadRequest {
  AddressRange range;
};

/**
 * @brief 完整的 PLC 轮询请求包
 */
struct PollRequest {
  std::vector<CoilReadRequest>  coilRequests;
  std::vector<WordReadRequest>  wordRequests;
};

/**
 * @brief 地址打包工具：将散列地址合并为连续读取区间
 *
 * @details
 * 例如寄存器 {100, 101, 102, 200} → [Range(100, 200)]
 * 降低 Modbus 网络交互次数。
 * 最大合并间隔由 maxGap 控制：连续寄存器间的空隔 <= maxGap 才会合并。
 */
class AddressPacker {
public:
  /**
   * @brief 将散列的寄存器地址打包为连续区间
   * @param addresses 离散地址列表（升序排列）
   * @param maxGap  允许的最大地址空隔（超过则新开一个区间）
   * @return 打包后的连续地址区间列表
   */
  static std::vector<AddressRange> pack(const std::vector<uint16_t>& addresses, uint16_t maxGap = 0);
};

/**
 * @brief PLC 现场照片采集器
 *
 * @details
 * 使用模式（由上层 Driver 驱动）：
 * @code
 *   PlcPoller poller(registry);
 *   auto req = poller.prepare();
 *   // driver 执行 req → 得到 bits, words
 *   auto snap = poller.assemble(bits, words, timestamp);
 * @endcode
 */
class PlcPoller {
public:
  /**
   * @param registry 寄存器注册表（决定完整采集列表）
   */
  explicit PlcPoller(const RegisterRegistry& registry);

  /**
   * @brief 从 Registry 生成下一轮所需的所有 FC 请求
   * @return PollRequest 包含打包后的 FC01/FC03 请求列表
   */
  PollRequest prepare() const;

  /**
   * @brief 将网络响应汇编为 PlcSnapshot
   * @param coilResponses  FC01 响应 payload（与 coilRequests 顺序一一对应）
   * @param wordResponses  FC03 响应 payload（与 wordRequests 顺序一一对应）
   * @param timestamp    采集时间戳
   * @return PlcSnapshot 完整照片
   */
  PlcSnapshot assemble(
    const std::vector<std::vector<uint8_t>>&  coilResponses,
    const std::vector<std::vector<uint16_t>>& wordResponses,
    uint64_t timestamp) const;

  /**
   * @brief 快速组装（成功场景）：创建受信快照
   * @param bitsPayload   合并后的 Coil 字节序 payload
   * @param wordsPayload  合并后的 Word 向量
   * @param bitsStart   合并 Coil 起始地址
   * @param wordsStart  合并 Word 起始地址
   * @param timestamp   时间戳
   * @return 受信 PlcSnapshot（complete=true）
   */
  static PlcSnapshot trust(
    std::vector<uint8_t>  bitsPayload,
    uint16_t        bitsStart,
    uint16_t        bitsCount,
    std::vector<uint16_t> wordsPayload,
    uint16_t        wordsStart,
    uint64_t        timestamp);

  /**
   * @brief 创建不受信快照（网络失败兜底）
   */
  static PlcSnapshot untrusted(uint64_t timestamp);

private:
  const RegisterRegistry& m_registry;

  std::vector<uint16_t> m_coilAddresses;
  std::vector<AddressRange> m_coilRanges;

  std::vector<uint16_t> m_wordAddresses;
  std::vector<AddressRange> m_wordRanges;
};

} // namespace plc::protocol
