#ifndef MOTION_DIRECTION_H
#define MOTION_DIRECTION_H
#pragma once

/*
 * 龙门轴逻辑运动方向值对象
 *
 * 定义了龙门系统唯一的逻辑方向语义，所有运动指令必须使用此方向，
 * 严禁直接使用物理方向（如 CW/CCW、正转/反转）。
 *
 *   - Forward（正向）：龙门整体远离操作者方向
 *   - Backward（负向）：龙门整体靠近操作者方向
 *
 * 设计意图：
 *   - 方向语义唯一性：系统中只存在这一个方向枚举，避免不同层各自定义造成混乱
 *   - 与物理解耦：方向与电机绕组接线、正反转定义无关，
 *     底层驱动负责将逻辑方向映射为具体电机的物理旋转方向
 *   - 命令统一：所有 Jog/Move 接口的参数类型均为 MotionDirection，
 *     调用方无需关心物理实现
 *
 * 这是一个纯值对象（Value Object），无行为副作用。
 */
enum class MotionDirection {
    Forward,   // 正向：龙门整体远离操作者
    Backward   // 负向：龙门整体靠近操作者
};

/*
 * 获取反方向
 *
 * 将 Forward 映射为 Backward，Backward 映射为 Forward。
 *
 * 典型用途：
 *   - 限位恢复：当某方向限位触发时，只允许朝反方向 Jog 以退出限位
 *   - 方向翻转：需要反转运动逻辑时的语义转换
 *
 * 示例：
 *   auto dir = MotionDirection::Forward;
 *   assert(opposite(dir) == MotionDirection::Backward);
 *   assert(opposite(opposite(dir)) == dir); // 两次反转回到原方向
 */
constexpr MotionDirection opposite(MotionDirection dir) {
    return (dir == MotionDirection::Forward) 
        ? MotionDirection::Backward 
        : MotionDirection::Forward;
}

/*
 * 判断是否为正向（远离操作者）
 *
 * 正向通常对应龙门向设备后部运动的方向。
 */
constexpr bool isForward(MotionDirection dir) {
    return dir == MotionDirection::Forward;
}

/*
 * 判断是否为负向（靠近操作者）
 *
 * 负向通常对应龙门向操作者方向运动的方向。
 */
constexpr bool isBackward(MotionDirection dir) {
    return dir == MotionDirection::Backward;
}

#endif // MOTION_DIRECTION_H
