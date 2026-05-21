#pragma once

/**
 * @brief 龙门反馈快照 DTO
 *
 * 对应 PLC 三个龙门相关寄存器的完整快照：
 *  - enable:    寄存器「轴X状态显示」（1=电机使能, 0=掉电）
 *  - isCoupled: 寄存器「轴X联动状态」（ON/OFF）
 *  - errorCode: 寄存器「Gantry_Error_Code」
 *
 * 由 PLC 轮询线程一次性读取、分别推送给 GantryPowerController 和 GantryGroup。
 */
struct GantryFeedback {
    bool enable;        // 电机使能状态
    bool isCoupled;     // 联动状态
    int  errorCode;     // PLC 错误码
};
