#pragma once

#include "AxisId.h"
#include <string>

/**
 * @file PhysicalAxis.h
 * @brief 物理轴实体 (X1 / X2)
 *
 * 职责：
 *   - 封装单个物理执行单元的运行时状态镜像
 *   - 持有身份标记 AxisId (X1 或 X2)
 *   - 为 GantrySystem 提供原始数据供安全检查与位置一致性计算
 *
 * 不负责：
 *   - 不持有命令槽（命令由 LogicalAxis + GantrySystem 统一管理）
 *   - 不执行耦合/解耦逻辑
 *   - 不判断操作合法性
 *
 * 约束映射：
 *   position()              → 约束 9  (镜像关系数据源)
 *   syncState()             → 约束 9, 11 (位置一致性数据入口)
 *   isEnabled() / isAlarmed() → 约束 13 (联动建立条件)
 */

/**
 * @brief 物理轴状态快照
 *
 * 用于一次性导出全部状态，避免多次调用时状态不一致。
 */
struct PhysicalAxisState {
    bool enabled = false;
    bool alarmed = false;
    bool posLimitActive = false;
    bool negLimitActive = false;
    double position = 0.0;  // 物理位置 (mm)，正方向与逻辑 X 轴同向
};

class PhysicalAxis {
public:
    /**
     * @brief 构造物理轴
     * @param id 轴标识，必须是 AxisId::X1 或 AxisId::X2
     */
    explicit PhysicalAxis(AxisId id) : m_id(id) {}

    /// 轴标识符 (X1 或 X2)
    AxisId id() const { return m_id; }

    // ═══════════════════════════════════
    // 状态查询（只读）
    // ═══════════════════════════════════

    /// 轴是否已使能
    bool isEnabled() const { return m_state.enabled; }

    /// 轴是否处于报警状态
    bool isAlarmed() const { return m_state.alarmed; }

    /// 正限位是否激活
    bool isPosLimitActive() const { return m_state.posLimitActive; }

    /// 负限位是否激活
    bool isNegLimitActive() const { return m_state.negLimitActive; }

    /// 任一侧限位是否激活
    bool isAnyLimitActive() const {
        return m_state.posLimitActive || m_state.negLimitActive;
    }

    /// 物理位置 (mm)
    double position() const { return m_state.position; }

    // ═══════════════════════════════════
    // 快照导出
    // ═══════════════════════════════════

    /**
     * @brief 一次性导出全部状态快照
     *
     * 确保调用者在一次调用中获得一致的状态视图。
     */
    PhysicalAxisState snapshot() const { return m_state; }

    // ═══════════════════════════════════
    // 状态同步（由 HAL 反馈定期调用）
    // ═══════════════════════════════════

    /**
     * @brief 从外部反馈同步物理轴状态
     * @param state HAL 层提供的物理轴状态快照
     */
    void syncState(const PhysicalAxisState& state) { m_state = state; }

    // ═══════════════════════════════════
    // 便捷状态字段更新
    // ═══════════════════════════════════

    void setEnabled(bool v) { m_state.enabled = v; }
    void setAlarmed(bool v) { m_state.alarmed = v; }
    void setPosLimitActive(bool v) { m_state.posLimitActive = v; }
    void setNegLimitActive(bool v) { m_state.negLimitActive = v; }
    void setPosition(double v) { m_state.position = v; }

private:
    AxisId m_id;
    PhysicalAxisState m_state;
};
