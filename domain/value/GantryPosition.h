#ifndef GANTRY_POSITION_H
#define GANTRY_POSITION_H
#pragma once

/*
 * 龙门位置值对象（逻辑坐标，单位 mm）
 *
 * 表示龙门系统在运动轴上的逻辑位置。在联动模式下，
 * GantryPosition 等价于 X1 的物理位置，代表龙门整体的空间位置。
 *
 * 这是一个纯值对象（Value Object）：
 *   - 不可变（immutable）：构造后内部值不可修改，所有算术运算返回新对象
 *   - 值相等（value equality）：同值即相等，无身份概念
 *   - 自描述：语义明确携带单位为毫米（mm）
 *   - 无副作用：纯数据载体，不依赖外部状态
 *
 * 典型用途：
 *   - 表示目标位置（MoveAbsolute 指令参数）
 *   - 表示当前位置（编码器反馈）
 *   - 区间判断（软限位检查、行程范围校验）
 *   - 距离计算（两个位置之间的位移量）
 */
class GantryPosition {
public:
    /*
     * 从毫米值构造龙门位置
     *
     * 使用 explicit 防止 double → GantryPosition 的隐式转换，
     * 强制调用方明确表达"这是一个位置值"的语义。
     *
     * @param value 位置值，单位为 mm
     */
    explicit constexpr GantryPosition(double value) 
        : m_value(value) {}

    /*
     * 获取位置的原始毫米值
     *
     * 返回内部存储的双精度浮点数，单位为 mm。
     * 通常在需要数值计算或传递给底层驱动时使用。
     */
    constexpr double value() const { return m_value; }

    // 相等比较：两个 GantryPosition 的 mm 值相同则相等
    constexpr bool operator==(const GantryPosition& other) const {
        return m_value == other.m_value;
    }

    // 不等比较：两个 GantryPosition 的 mm 值不同则不等
    constexpr bool operator!=(const GantryPosition& other) const {
        return m_value != other.m_value;
    }

    // 小于比较（用于软限位校验等区间判断）
    constexpr bool operator<(const GantryPosition& other) const {
        return m_value < other.m_value;
    }

    // 小于等于比较
    constexpr bool operator<=(const GantryPosition& other) const {
        return m_value <= other.m_value;
    }

    // 大于比较
    constexpr bool operator>(const GantryPosition& other) const {
        return m_value > other.m_value;
    }

    // 大于等于比较
    constexpr bool operator>=(const GantryPosition& other) const {
        return m_value >= other.m_value;
    }

    /*
     * 位置偏移加法：当前位置 + 毫米偏移量 → 新位置
     *
     * 常用于计算目标位置 = 当前位置 + 相对位移。
     *
     * @param offset 偏移量，单位 mm（正数向正方向，负数向负方向）
     * @return 偏移后的新 GantryPosition
     */
    constexpr GantryPosition operator+(double offset) const {
        return GantryPosition(m_value + offset);
    }

    /*
     * 位置偏移减法：当前位置 - 毫米偏移量 → 新位置
     *
     * 常用于计算目标位置 = 当前位置 - 相对位移。
     *
     * @param offset 偏移量，单位 mm
     * @return 偏移后的新 GantryPosition
     */
    constexpr GantryPosition operator-(double offset) const {
        return GantryPosition(m_value - offset);
    }

    /*
     * 位置差值：两个龙门位置之间的距离（有符号，单位 mm）
     *
     * 结果 = 当前位置 - 另一位置。正值表示当前位置在参数位置的 Forward 方向。
     * 常用于判断两点间距离是否在允许范围内。
     *
     * @param other 另一个龙门位置
     * @return 两点间的有符号距离，单位 mm
     */
    constexpr double operator-(const GantryPosition& other) const {
        return m_value - other.m_value;
    }

    /*
     * 返回零位置（0.0 mm）
     *
     * 用于表示机床零点（回零完成后的参考位置）。
     * 典型用途：
     *   - 初始化默认位置
     *   - 作为绝对坐标系的基准
     */
    static constexpr GantryPosition zero() {
        return GantryPosition(0.0);
    }

private:
    double m_value;  // 位置值，单位：毫米（mm）
};

#endif // GANTRY_POSITION_H
