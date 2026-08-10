// ============================================================================
// DecodeError.h —— Step 2 codec: 解码失败描述
// ============================================================================
// 统一替代旧 RegisterCodec 的"向上抛未分类异常"做法：
//   解码/越界/长度不足等失败一律返回 DecodeError，携带原因枚举与越界索引，
//   由调用方作为可诊断问题处理，绝不抛 C++ 异常。
//
// 纯基础类型：无地址、无业务名称、无 Modbus/Qt/Domain 依赖。
// ============================================================================
#pragma once

#include <string>

namespace plc_vnext::codec {

enum class DecodeErrorKind {
    /// 提供的寄存器/比特数量不足以完成解码
    TooFewRegisters,
    /// 请求的地址/索引越界（超出 RawRegisterBlock 缓冲边界）
    OutOfRange,
    /// 读取到非法枚举/保留值，无法解释
    InvalidEnum,
    /// 其它解码失败
    Other
};

struct DecodeError {
    DecodeErrorKind kind = DecodeErrorKind::Other;
    /// 越界/长度相关的索引（无意义时置 0），用于日志与诊断
    int index = 0;
    std::string diagnostic;

    [[nodiscard]] bool ok() const { return false; }  // 本类型总是代表失败
};

}  // namespace plc_vnext::codec
