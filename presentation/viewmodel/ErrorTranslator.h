#pragma once

#include "ViewModelError.h"
#include "application/UseCaseError.h"

/**
 * @brief UseCaseError -> ViewModelError 翻译函数
 *
 * 使用 std::visit 一次性覆盖所有 variant 分支。
 * 编译器保证所有分支覆盖，无遗漏。
 *
 * @param err 应用层用例错误（variant）
 * @return 翻译后的 ViewModel 层错误结构体
 */
ViewModelError translate(const UseCaseError& err);
