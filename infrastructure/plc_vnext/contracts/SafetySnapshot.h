// ============================================================================
// SafetySnapshot.h —— contracts: 急停只读快照（M224/M225）
// ============================================================================
// 只表达一次只读得到的设备急停线圈反馈（方案 §4.4）：
//   - M224 = 设备急停（Emergency Stop）已触发
//   - M225 = 设备急停解除请求（PLC 自复位，上位机只写 ON；M225=ON 时 PLC 处理
//            解除后 M224/M225 自动变 OFF，不自动恢复使能/运动）
// 本类型只读，不含任何写接口、不表达使能/运动命令状态。纯 DTO：不依赖 Modbus /
// Qt / Domain。读取失败以 trusted=false + diagnostic 表达，不得把字段置 0 冒充
// “正常”（上层据此决定是否锁定普通控制）。
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace plc_vnext::contracts {

/// 一次急停只读轮询得到的反馈快照。
struct SafetySnapshot {
    /// 本次读取是否成功（读到完整 M224/M225 且通讯正常）。
    bool trusted = false;
    /// M224 —— 设备急停已触发。
    bool emergencyStop = false;
    /// M225 —— 设备急停解除请求已写入（PLC 自复位，只读侧通常为瞬态）。
    bool releaseRequest = false;
    /// 采样时刻（steady clock，毫秒）。
    int64_t sampledAtMs = 0;
    /// 失败时的诊断文本（仅用于日志/UI，不参与控制流）。
    std::string diagnostic;
};

}  // namespace plc_vnext::contracts
