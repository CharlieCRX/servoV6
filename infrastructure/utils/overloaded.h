#pragma once

/**
 * @brief std::visit 的辅助模板 —— 简化 variant 访问
 *
 * 使用方法:
 *   std::visit(overloaded{
 *       [](int i) { ... },
 *       [](double d) { ... },
 *       [](auto&) { ... }  // 默认兜底
 *   }, my_variant);
 */
template<class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

// CTAD 指引 (C++17)
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;
