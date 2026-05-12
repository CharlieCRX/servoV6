#pragma once

/**
 * @brief 龙门联动状态机 (四态全闭环模型)
 * * 职责：维护意图与物理反馈之间的异步转换逻辑
 * 路径：
 * Decoupled -> [requestCouple] -> CouplingRequested
 * CouplingRequested -> [applyCoupledFeedback] -> Coupled
 * Coupled -> [requestDecouple] -> DecouplingRequested
 * DecouplingRequested -> [applyDecoupledFeedback] -> Decoupled
 */
class GantryCouplingState {
public:
    enum class Status {
        Decoupled,
        CouplingRequested,
        Coupled,
        DecouplingRequested  // ⭐ 新增：解耦等待物理确认的中间态
    };

    Status status() const { return m_status; }

    // --- 状态查询 ---
    bool isCoupled() const { return m_status == Status::Coupled; }
    bool isCouplingRequested() const { return m_status == Status::CouplingRequested; }
    bool isDecouplingRequested() const { return m_status == Status::DecouplingRequested; } // ⭐ 新增

    // --- 意图控制 (Intent) ---
    void requestCouple() { 
        m_status = Status::CouplingRequested; 
    }

    void requestDecouple() { 
        m_status = Status::DecouplingRequested; // ⭐ 修改：不再直接 Decoupled，而是等待物理确认
    }

    // --- 物理反馈接收 (Feedback) ---
    void applyCoupledFeedback() { 
        m_status = Status::Coupled; 
    }

    void applyDecoupledFeedback() { 
        m_status = Status::Decoupled; 
    }

private:
    Status m_status = Status::Decoupled;
};