#pragma once

#include <string>

/**
 * @brief UI 友好的错误分类
 *
 * Inline  -- 在轴控件旁内联显示（如"轴已到达正向限位"），不打断用户操作
 * Modal   -- 弹窗警告（如"通讯中断"），需要用户确认
 * Silent  -- 不显示，仅记录日志（如"点动停止时轴已空闲"）
 */
enum class ErrorCategory {
    Inline,
    Modal,
    Silent
};

/**
 * @brief ViewModel 层翻译后的错误结构
 *
 * 设计原则：
 *   - code:        机器可读，QML 用于图标/颜色/行为分发
 *   - userMessage: 用户可读，QML 直接绑定到 Label.text
 *   - debugMessage: 调试信息，用于日志，不暴露给最终用户
 *   - category:    决定 UI 展示策略
 */
struct ViewModelError {
    std::string code;
    std::string userMessage;
    std::string debugMessage;
    ErrorCategory category = ErrorCategory::Inline;

    bool isValid() const { return !code.empty(); }
};
