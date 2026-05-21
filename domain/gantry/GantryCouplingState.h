#pragma once

/**
 * @brief 龙门联动状态机 (五态全闭环模型)
 * * 职责：维护意图与物理反馈之间的异步转换逻辑
 * * 路径：
 * NotSynchronized -> [applyCoupledFeedback] -> Coupled
 * NotSynchronized -> [applyDecoupledFeedback] -> Decoupled
 * Decoupled -> [requestCouple] -> CouplingRequested
 * CouplingRequested -> [applyCoupledFeedback] -> Coupled
 * Coupled -> [requestDecouple] -> DecouplingRequested
 * DecouplingRequested -> [applyDecoupledFeedback] -> Decoupled
 */
class GantryCouplingState {
public:
    enum class Status {
        NotSynchronized,       // 系统启动后尚未收到任何 PLC 快照，物理真相未知
        Decoupled,
        CouplingRequested,
        Coupled,
        DecouplingRequested    // 解耦等待物理确认的中间态
    };

    Status status() const { return m_status; }

    // --- 状态查询 ---
    bool isNotSynchronized() const { return m_status == Status::NotSynchronized; }
    bool isCoupled() const { return m_status == Status::Coupled; }
    bool isCouplingRequested() const { return m_status == Status::CouplingRequested; }
    bool isDecouplingRequested() const { return m_status == Status::DecouplingRequested; }

    // --- 意图控制 (Intent) ---
    void requestCouple() { 
        if (m_status == Status::NotSynchronized) return;  // 未同步时拒绝意图操作
        m_status = Status::CouplingRequested; 
    }

    void requestDecouple() { 
        if (m_status == Status::NotSynchronized) return;  // 未同步时拒绝意图操作
        m_status = Status::DecouplingRequested;
    }

    // --- 物理反馈接收 (Feedback) ---
    void applyCoupledFeedback() { 
        m_status = Status::Coupled; 
    }

    void applyDecoupledFeedback() { 
        m_status = Status::Decoupled; 
    }

private:
    Status m_status = Status::NotSynchronized;
};

/// @brief 将 GantryCouplingState::Status 枚举值转换为可读字符串（用于日志输出）
inline const char* gantryCouplingStatusToString(GantryCouplingState::Status s) {
    switch (s) {
        case GantryCouplingState::Status::NotSynchronized:    return "NotSynchronized";
        case GantryCouplingState::Status::Decoupled:           return "Decoupled";
        case GantryCouplingState::Status::CouplingRequested:   return "CouplingRequested";
        case GantryCouplingState::Status::Coupled:             return "Coupled";
        case GantryCouplingState::Status::DecouplingRequested: return "DecouplingRequested";
    }
    return "?";
}
