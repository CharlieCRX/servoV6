#pragma once

#include "domain/port/IGantryCommandPort.h"
#include "infrastructure/FakePLC.h"
#include <algorithm>

/**
 * @brief IGantryCommandPort 的 Fake 实现
 *
 * 将龙门命令委托给 FakePLC 的单轴命令接口。
 *
 * Coupled 模式下：
 *   - Gantry 命令自动拆分为 X1/X2 的镜像指令
 *   - X2 方向取反（镜像），X2 位置取负（镜像）
 */
class FakeGantryCommandPort : public IGantryCommandPort {
public:
    explicit FakeGantryCommandPort(FakePLC& plc)
        : m_plc(plc) {}

    // ═══════════════════════════════════
    // 单轴命令 (Decoupled 模式)
    // ═══════════════════════════════════

    bool jogAxis(int axis, MotionDirection direction) override {
        if (axis != 1 && axis != 2) return false;
        AxisId aid = toAxisId(axis);
        Direction dir = (direction == MotionDirection::Forward)
                            ? Direction::Forward
                            : Direction::Backward;
        m_plc.onCommand(aid, JogCommand{dir, true});
        return true;
    }

    bool moveAbsoluteAxis(int axis, double position) override {
        if (axis != 1 && axis != 2) return false;
        AxisId aid = toAxisId(axis);
        m_plc.onCommand(aid, MoveCommand{MoveType::Absolute, position});
        return true;
    }

    bool moveRelativeAxis(int axis, double delta) override {
        if (axis != 1 && axis != 2) return false;
        AxisId aid = toAxisId(axis);
        m_plc.onCommand(aid, MoveCommand{MoveType::Relative, delta});
        return true;
    }

    bool stopAxis(int axis) override {
        if (axis != 1 && axis != 2) return false;
        AxisId aid = toAxisId(axis);
        m_plc.onCommand(aid, StopCommand{});
        return true;
    }

    // ═══════════════════════════════════
    // 龙门命令 (Coupled 模式)
    // ═══════════════════════════════════

    bool jogGantry(MotionDirection direction) override {
        // X1: 原方向, X2: 镜像方向
        Direction x1Dir = (direction == MotionDirection::Forward)
                              ? Direction::Forward
                              : Direction::Backward;
        Direction x2Dir = (direction == MotionDirection::Forward)
                              ? Direction::Backward
                              : Direction::Forward;
        m_plc.onCommand(AxisId::X1, JogCommand{x1Dir, true});
        m_plc.onCommand(AxisId::X2, JogCommand{x2Dir, true});
        return true;
    }

    bool moveAbsoluteGantry(double position) override {
        // X1: +position, X2: -position (镜像)
        m_plc.onCommand(AxisId::X1, MoveCommand{MoveType::Absolute, position});
        m_plc.onCommand(AxisId::X2, MoveCommand{MoveType::Absolute, -position});
        return true;
    }

    bool moveRelativeGantry(double delta) override {
        // X1: +delta, X2: -delta (镜像)
        m_plc.onCommand(AxisId::X1, MoveCommand{MoveType::Relative, delta});
        m_plc.onCommand(AxisId::X2, MoveCommand{MoveType::Relative, -delta});
        return true;
    }

    bool stopGantry() override {
        m_plc.onCommand(AxisId::X1, StopCommand{});
        m_plc.onCommand(AxisId::X2, StopCommand{});
        return true;
    }

    // ═══════════════════════════════════
    // 通用
    // ═══════════════════════════════════

    bool isAxisSlotFree(int axis) const override {
        if (axis != 1 && axis != 2) return false;
        AxisId aid = toAxisId(axis);
        auto fb = m_plc.getFeedback(aid);
        // 槽空闲 = 轴不在运动中
        return fb.state != AxisState::Jogging &&
               fb.state != AxisState::MovingAbsolute &&
               fb.state != AxisState::MovingRelative;
    }

private:
    FakePLC& m_plc;

    static AxisId toAxisId(int axis) {
        return (axis == 1) ? AxisId::X1 : AxisId::X2;
    }
};
