// ============================================================================
// ReadResult.h —— Step 1 contracts: 读取结果
// ============================================================================
// 用于"读操作未能得到快照"（读取/解码失败），与写通道的 CommunicationResult
// 语义区分。C++20 不假定存在 std::expected，故采用自定义实现。
//
// 注意：PLC 已成功读出的 ConfigValid=false 属于 TopologySnapshot 的内容，
//       不属于本类型的失败。
// ============================================================================
#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace plc_vnext::contracts {

template <typename T>
class ReadResult {
public:
    enum class FailureKind { Transport, Decode, RevisionChanged };

    static ReadResult success(T value) {
        return ReadResult(std::move(value));
    }
    static ReadResult failure(FailureKind kind, std::string diagnostic) {
        return ReadResult(kind, std::move(diagnostic));
    }

    [[nodiscard]] bool hasValue() const { return val_.has_value(); }
    [[nodiscard]] explicit operator bool() const { return hasValue(); }

    /// 仅在失败结果上调用；成功结果返回默认 FailureKind::Transport（无意义）
    [[nodiscard]] FailureKind failureKind() const { return kind_; }
    [[nodiscard]] const std::string& diagnostic() const { return diagnostic_; }

    /// 成功结果：返回不可变快照值（const&）；失败结果调用将抛 logic_error，
    /// 避免调用方误取默认值。
    [[nodiscard]] const T& value() const& {
        if (!val_) throw std::logic_error("ReadResult::value() on failure result");
        return *val_;
    }
    [[nodiscard]] T&& value() && {
        if (!val_) throw std::logic_error("ReadResult::value() on failure result");
        return std::move(*val_);
    }

    [[nodiscard]] const T& operator*() const& { return value(); }
    [[nodiscard]] T&& operator*() && { return std::move(value()); }

private:
    explicit ReadResult(T value) : val_(std::move(value)) {}
    ReadResult(FailureKind kind, std::string diagnostic)
        : val_(std::nullopt), kind_(kind), diagnostic_(std::move(diagnostic)) {}

    std::optional<T> val_;
    FailureKind kind_ = FailureKind::Transport;
    std::string diagnostic_;
};

}  // namespace plc_vnext::contracts
