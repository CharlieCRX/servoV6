// ============================================================================
// AxisMotionCommon.h —— 新策略层的公共定义（motionState 语义 + 时序常量 + 结果）
// ============================================================================
// 依据《servoV6剩余迁移工作实施方案》§4.5 与真实 PLC_re 状态机，把 D128
// motionState 语义固定为（见 PLC 状态机 if/ELSIF 顺序）：
//   0 = 使能轴控 OFF
//   1 = 轴控 ON、电机 OFF（掉电态）
//   2 = 电机 ON、空闲（使能完成 / 运动完毕）★ 唯一判定"使能/空闲"的状态
//   3 = 点动正向 / 4 = 点动反向
//   5 = 轴MoveDo   （绝对定位执行中）
//   6 = 轴MoveXDDo （相对定位执行中；MoveXDPosition=目标相对距离，故相对）
// 纯 C++：只依赖自身 + plc_vnext contracts 的纯 DTO。
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace application_vnext::policy {

// D128 运动状态（真实 PLC_re 语义）
constexpr int16_t kMotionNotEnabled      = 0;  // 使能轴控 OFF
constexpr int16_t kMotionEnabledMotorOff = 1;  // 轴控 ON、电机 OFF（掉电态）
constexpr int16_t kMotionMotorIdle       = 2;  // 电机 ON、空闲（★使能完成/运动完毕）
constexpr int16_t kMotionJogForward      = 3;  // 点动正向
constexpr int16_t kMotionJogBackward     = 4;  // 点动反向
constexpr int16_t kMotionAbsMove         = 5;  // 轴MoveDo（绝对定位执行中）
constexpr int16_t kMotionRelMove         = 6;  // 轴MoveXDDo（相对定位执行中）

// 位置容差（EU），用于"位移发生"判定。
constexpr float kPositionEpsilon = 0.01f;

// 与旧 AbsMovePolicy / JogOrchestrator 对齐的时序参数（秒）
constexpr double kEnableTimeoutSeconds   = 2.0;  // 使能超时：等待 motionState==2
constexpr double kPostEnableDelaySeconds = 0.4;  // 使能后硬件稳定（抱闸释放/磁场建立）
constexpr double kPostStopDelaySeconds   = 0.5;  // 停稳后完全静止再掉电
// 触发后若始终未观察到"运动状态(5/6)"或位置变化，判定为"运动未启动"的超时（秒）。
// ★ 不再用短窗口兜底放行：真机从写触发到 ms 进入运动态有（轮询+PLC扫描）时延，
//   过短的"无运动兜底"会把尚未启动的运动误判为"目标≈当前位置"而提前终止并掉电。
//   超时走 Error，绝不冒充成功。
constexpr double kMotionStartTimeoutSeconds = 5.0;

// ---- 完成判定（用户最终方案）----
// 1) state==2（PLC 空闲）→ 无条件判定成功（信任 PLC，不做位置/容差判断）；
// 2) 兜底：state!=2 但位置已到达目标附近(±kTargetTolerance)并持续 kPositionReachedStableSeconds
//    → 也判定成功（写 OFF 掉电收尾）。
constexpr float kTargetTolerance = 0.1f;         // 兜底判定用的到位容差（±0.1）
constexpr double kPositionReachedStableSeconds = 3.0;  // 兜底：位置到位后需持续稳定的时长

/// motionState 的可读名称（日志/诊断）。
inline const char* motionStateName(int16_t s) {
    switch (s) {
        case kMotionNotEnabled:      return "NotEnabled(0)";
        case kMotionEnabledMotorOff: return "EnabledMotorOff(1)";
        case kMotionMotorIdle:       return "MotorIdle(2)";
        case kMotionJogForward:      return "JogForward(3)";
        case kMotionJogBackward:     return "JogBackward(4)";
        case kMotionAbsMove:         return "AbsMove(5)";
        case kMotionRelMove:         return "RelMove(6)";
        default:                     return "?";
    }
}

/// 定位触发策略的一次运行结果（API 阻塞便利版返回）。
struct MoveOutcome {
    bool ok = false;        ///< 策略进入 Done
    std::string step;       ///< 结束时的 Step 名
    std::string diag;       ///< 失败诊断
    float startPos = 0.f;   ///< 触发前位置
    float endPos = 0.f;     ///< 结束时位置
};

/// 点动策略的一次运行结果（API 阻塞便利版返回）。
struct JogOutcome {
    bool ok = false;
    std::string diag;
};

}  // namespace application_vnext::policy
