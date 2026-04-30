#ifndef GANTRY_MODE_H
#define GANTRY_MODE_H
#pragma once

/*
 * 龙门轴操作模式值对象
 *
 * 定义了双轴龙门系统的两种工作模式，二者在任何时刻互斥：
 *
 *   - Coupled（联动模式）：
 *       双轴视为一个逻辑整体 X，所有运动指令（Jog/Move/Stop）只能对 X 整体下发。
 *       X1 和 X2 由底层同步控制，应用层不可单独操作。
 *       适用于正常加工、同步定位等场景。
 *
 *   - Decoupled（分动模式）：
 *       双轴各自独立，逻辑轴 X 不可操作，必须直接控制物理轴 X1 或 X2。
 *       此时允许两轴异步运动，龙门同步保护解除。
 *       适用于设备调试、回零校准、单轴维护等场景。
 *
 * 模式互斥：
 *   - 任意时刻系统只能处于其中一种模式
 *   - 模式切换通过 SystemContext 统一管理，需校验状态一致性
 *
 * 这是一个纯值对象（Value Object），无行为副作用，可直接比较。
 */
enum class GantryMode {
    Coupled,    // 联动模式：X1 和 X2 同步运动，应用层只操作逻辑轴 X
    Decoupled   // 分动模式：X1 和 X2 可独立运动，逻辑轴 X 被禁用
};

/*
 * 检查两个模式是否互斥（即不相同）
 *
 * 龙门系统同一时刻只能处于一种模式，此函数用于校验状态转换的合法性。
 * 当两个模式相同时返回 false（「相同模式不互斥」便于防御性断言）；
 * 当两个模式不同时返回 true（互斥成立）。
 *
 * 示例：
 *   assert(areMutuallyExclusive(GantryMode::Coupled, GantryMode::Decoupled)); // true
 *   assert(!areMutuallyExclusive(GantryMode::Coupled, GantryMode::Coupled));  // false
 */
constexpr bool areMutuallyExclusive(GantryMode a, GantryMode b) {
    return a != b;
}

/*
 * 判断当前是否处于联动模式
 *
 * 联动模式下，X1/X2 同步跟随逻辑轴 X 运动，
 * 应用层下发指令到 X 即可，底层保证双轴一致性。
 */
constexpr bool isCoupled(GantryMode mode) {
    return mode == GantryMode::Coupled;
}

/*
 * 判断当前是否处于分动模式
 *
 * 分动模式下，X1/X2 各自独立，应用层需分别下发指令。
 * 此时龙门同步保护已解除，需注意避免碰撞。
 */
constexpr bool isDecoupled(GantryMode mode) {
    return mode == GantryMode::Decoupled;
}

#endif // GANTRY_MODE_H
