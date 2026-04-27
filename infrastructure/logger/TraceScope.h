#pragma once
#include "LogContext.h"
#include <vector>

class TraceScope {
public:
    // 构造时：将当前操作的上下文压入线程局部栈
    TraceScope(const std::string& group, const std::string& axis, const std::string& traceId) {
        currentStack().push_back({group, axis, traceId});
    }

    // 析构时：自动弹出上下文（确保操作结束时清理，防内存泄漏与上下文污染）
    ~TraceScope() {
        if (!currentStack().empty()) {
            currentStack().pop_back();
        }
    }

    // 获取当前线程最顶层的上下文
    static LogContext current() {
        auto& stack = currentStack();
        return stack.empty() ? LogContext{} : stack.back();
    }

private:
    // 线程局部存储，完美兼容未来可能的多线程架构
    static std::vector<LogContext>& currentStack() {
        thread_local std::vector<LogContext> stack;
        return stack;
    }
};