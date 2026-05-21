# 运动资源互斥缺失 —— Bug 诊断与重构方案

## 1. 问题现象

从日志中可以清晰看到以下事件序列：

| 时间戳 | 事件 |
|---|---|
| 11:13:15.836 | `AbsOrch` 发起 `Machine_A/Y` 绝对定位 `MoveAbsolute target=100.0` |
| 11:13:15.998 | 轴进入 `MovingAbsolute(4)` 状态，电机开始运动 |
| 11:13:17.340 | **用户按下 Jog Forward**，`JogOrch` 启动，进入 `EnsuringEnabled` |
| 11:13:17.348~20.997 | `JogOrch` 持续等待 "axis state=MovingAbsolute(4)"（约 3.6 秒的忙碌等待） |
| 11:13:18.196 | 用户释放 Jog 按钮，`JogOrch` 发送 Stop 命令 |
| 11:13:18.207 | `AbsOrch` 的 `WaitingMotionFinish` 检测到 target 未到达，发送 Disable → **ABORTED** |
| 11:13:20.997 | 轴状态 `MovingAbsolute(4)` → `Idle(2)` |
| 11:13:21.008 | `JogOrch` 进入 `EnsuringDisabled` → 发送 Disable → Done（Success） |

**结果：**
- 绝对定位被意外中止（`ABORTED`），仅到达 `pos=+100.0`（恰好是目标值，但因中途干扰导致 errs=312）
- 用户的 Jog 操作本应在 UI 层被拒绝，却穿透到了应用层
- 两个编排器**同时操作同一个物理轴**，产生竞态

---

## 2. 根因分析

### 2.1 物理约束与架构认知

> **电机只有一个，只能同一时间接收一个运动命令进行运动。**

这是物理硬约束。所有运动意图（绝对定位、相对定位、点动）都是互斥资源。当前架构中，**这一约束没有被任何一层强制实施**。

### 2.2 缺失的互斥场景矩阵

| 当前运动 \ 新请求 | AbsMove | RelMove | Jog | Stop |
|---|---|---|---|---|
| **AbsMove 进行中** | ❌ 应拒绝 | ❌ 应拒绝 | ❌ 应拒绝 | ✅ 允许 |
| **RelMove 进行中** | ❌ 应拒绝 | ❌ 应拒绝 | ❌ 应拒绝 | ✅ 允许 |
| **Jog 进行中** | ❌ 应拒绝 | ❌ 应拒绝 | ❌ 应拒绝（同向可合并？） | ✅ 允许 |
| **Idle** | ✅ 允许 | ✅ 允许 | ✅ 允许 | N/A |

当前只有 Stop 操作（安全操作）在任何状态下都应该被允许。其余运动操作在轴非 Idle 时都应被拒绝。

### 2.3 当前防御机制为什么失效

**第一道防线：Axis 领域层的状态准入检查**

```cpp
// Axis::moveAbsolute() — 有效
if (m_state != AxisState::Idle) {
    m_last_rejection = RejectionReason::AlreadyMoving;
    return false;
}

// Axis::jog() — 有效
if (m_state != AxisState::Idle) {
    m_last_rejection = RejectionReason::AlreadyMoving;
    return false;
}
```

**但这道防线被 JogOrchestrator 的 `EnsuringEnabled` 阶段绕过了！**

```cpp
// JogOrchestrator::tick() — EnsuringEnabled 阶段
case Step::EnsuringEnabled:
    if (axis->state() == AxisState::Disabled) {
        // 发送 Enable 命令
    }
    if (axis->state() == AxisState::Idle) {
        m_step = Step::IssuingJog;  // 等待 Idle 后进入点动下发
    }
    // 其他状态（Unknown/Moving...）：保持等待 ⚠️ 这里是问题！
    LOG_DEBUG(..., "EnsuringEnabled -- waiting, axis state=MovingAbsolute(4)");
    break;
```

**执行流程漏洞：**

1. `AbsOrch` 发起 MoveAbsolute → 轴状态 `MovingAbsolute`
2. `JogOrch.startJog()` 被调用 → 进入 `EnsuringEnabled`
3. `JogOrch.tick()` 中看到轴是 `MovingAbsolute` → **不拒绝，而是等待！**
4. 当 `AbsOrch` 的运动完成 → 轴变 `Idle`
5. `JogOrch` 立刻从 `EnsuringEnabled` → `IssuingJog` → 下发 Jog 命令
6. 在 `AbsOrch` 还没来得及完成 `Done` 清理流程（发送 Disable）之前，`JogOrch` 已经接管了轴

**问题本质：**
- `Axis::jog()` 的状态准入检查只在 `Axis::jog()` 被调用时生效
- `JogOrchestrator` 在 `EnsuringEnabled` 阶段并没有调用 `Axis::jog()`，而是**调用了 `EnableUseCase`（使能）并等待 Idle**
- 等到 Idle 时，再调用 `Axis::jog()` 自然就通过了准入检查
- **编排器层面没有"运动所有权"的概念，导致一个编排器可以"埋伏"等待另一个编排器的运动结束后再抢走轴**

### 2.4 问题分层定位

```
┌──────────────────────────────────────────┐
│  UI Layer (QML)                          │
│  - 应该在按钮状态上禁用 Jog              │ ← ViewModel 层缺少互斥感知
│  - 当前：无论什么状态都可以触发 Jog       │
├──────────────────────────────────────────┤
│  Application Layer (Orchestrators)       │
│  - AbsOrch 不知道 JogOrch 在等待         │ ← 编排器之间零通信
│  - JogOrch 的 EnsuringEnabled 不做互斥检查│ ← ★ 核心漏洞
│  - 每个编排器独立 tick，独立状态机        │
├──────────────────────────────────────────┤
│  Domain Layer (Axis / SystemContext)      │
│  - Axis::jog() 有状态准入检查             │ ← 有效但可被编排器绕过
│  - SystemContext 没有运动所有权概念        │ ← ★ 架构缺失
│  - 没有"谁在占用电机"的互斥锁             │
├──────────────────────────────────────────┤
│  HAL / PLC                               │
│  - 物理层可以接收任何命令                  │
│  - 硬件层面可能有冲突，但无上报           │
└──────────────────────────────────────────┘
```

---

## 3. 架构设计：引入运动资源互斥机制

### 3.1 设计原则

1. **单写者原则**：每个轴（Axis）在同一时刻最多只能有一个运动编排器持有"运动所有权"
2. **所有权生命周期**：从运动编排器 `start` 到 `Done/Error` 的整个生命周期内，所有权持续有效
3. **Stop 豁免**：停止操作（Stop）是安全操作，不受所有权限制
4. **所有权归属**：互斥判断应放在 `SystemContext` 层，因为它是聚合根，管理所有轴资源

### 3.2 方案选择：在 SystemContext 中加入运动所有权

为什么不放在 Axis 中？
- Axis 是领域实体，负责自身状态一致性和语义校验
- Axis 已经通过 `state()` 表达了"是否在运动"，但不知道"谁在运动"
- 让 Axis 知道"编排器类型"会引入应用层概念到领域层，违反分层依赖方向

为什么放在 SystemContext 中？
- SystemContext 是聚合根，管理一组轴的资源和访问控制
- 已经有 `tryGetAxis()` 的多层拦截机制（Safety Lock → Gantry → Container）
- 运动所有权本质是**资源访问控制**，与现有拦截机制同属一层关注点
- 不需要修改 Axis 领域模型，改动影响面最小

### 3.3 具体设计

```cpp
// domain/entity/SystemContext.h 新增

/// @brief 运动所有权类型：标识当前占用轴运动资源的编排器
enum class MotionOwnership {
    None,           // 无运动占用
    AutoAbsMove,    // 绝对定位编排器占用
    AutoRelMove,    // 相对定位编排器占用
    Jog,            // 点动编排器占用
    Gantry          // 龙门编排器占用
};

class SystemContext {
public:
    // ... existing code ...

    /**
     * @brief 尝试获取轴的运动所有权（在 tryGetAxis 之后调用）
     *
     * 互斥规则：
     *   - 当前无所有权（None） → 允许获取，设置所有权
     *   - 当前所有权与请求者相同   → 允许（幂等/重入）
     *   - 当前所有权与请求者不同   → 拒绝，返回 MotionResourceBusy
     *
     * @param id 目标轴ID
     * @param owner 请求所有权的编排器类型
     * @param reason [输出] 失败时的拒绝原因
     * @return true 获取成功，false 资源被占用
     */
    bool tryAcquireMotion(AxisId id, MotionOwnership owner, ContextRejection& reason);

    /**
     * @brief 释放轴的运动所有权
     * @param id 目标轴ID
     * @param owner 释放者的编排器类型（安全校验：释放者必须与持有者一致）
     */
    void releaseMotion(AxisId id, MotionOwnership owner);

    /**
     * @brief 查询轴的当前运动所有权
     */
    MotionOwnership motionOwner(AxisId id) const;

private:
    std::unordered_map<AxisId, MotionOwnership> m_motionOwners;  // 新增
};
```

**ContextRejection 新增枚举值：**

```cpp
// domain/entity/ContextRejection.h 新增
enum class ContextRejection {
    // ... existing ...
    MotionResourceBusy,     // ★ 新增：轴的运动资源已被其他编排器占用
};
```

### 3.4 编排器侧的改动模式

每个运动编排器的 `startXxx()` 入口和 `tick()` 都需要遵循统一模式：

```cpp
// ========== 模式：AutoAbsMoveOrchestrator ==========

void startAbs(AxisId id, double target) {
    // ... existing code ...
    m_motionAcquired = false;  // 标记所有权未获取
}

void tick() {
    // ... tryGetGroup / tryGetAxis ...

    // ★ 新增：运动所有权拦截（Layer 介于 tryGetAxis 与业务逻辑之间）
    if (!m_motionAcquired && m_step == Step::EnsuringEnabled) {
        ContextRejection reason;
        if (!group->tryAcquireMotion(m_targetId, MotionOwnership::AutoAbsMove, reason)) {
            m_step = Step::Error;
            m_lastError = reason;
            return;  // 资源被占用，直接拒绝
        }
        m_motionAcquired = true;
    }

    // ... 原有的 switch (m_step) 逻辑 ...

    // ★ 在 Done 和 Error 的 case 中释放所有权
    case Step::Done:
    case Step::Error:
        if (m_motionAcquired) {
            group->releaseMotion(m_targetId, MotionOwnership::AutoAbsMove);
            m_motionAcquired = false;
        }
        break;
}
```

**JogOrchestrator 的 EnsuringEnabled 不再"等待"，改为"拒绝"：**

```cpp
// JogOrchestrator::tick() — EnsuringEnabled 阶段 ★ 修改
case Step::EnsuringEnabled:
    // ★ 旧逻辑（有问题）：等待任意状态变为 Idle
    // if (axis->state() == AxisState::Idle) { m_step = Step::IssuingJog; }
    // else { wait... }

    // ★ 新逻辑：只能从 Disabled 或 Idle 开始，其他状态直接拒绝
    if (axis->state() == AxisState::Disabled) {
        // 发送 Enable 命令
    } else if (axis->state() == AxisState::Idle) {
        m_step = Step::IssuingJog;
    } else {
        // 未知状态或运动中 → 拒绝（不应该在获取所有权后出现）
        m_step = Step::Error;
        m_lastError = RejectionReason::InvalidState;
    }
    break;
```

---

## 4. 重构步骤

### 第一阶段：领域层基础设施（SystemContext + ContextRejection）

**步骤 1：扩展 ContextRejection 枚举**

文件：`domain/entity/ContextRejection.h`
- 新增 `MotionResourceBusy` 枚举值

**步骤 2：在 SystemContext 中加入运动所有权管理**

文件：`domain/entity/SystemContext.h`
- 新增 `MotionOwnership` 枚举
- 新增 `std::unordered_map<AxisId, MotionOwnership> m_motionOwners` 成员
- 实现 `tryAcquireMotion(AxisId, MotionOwnership, ContextRejection&)`
- 实现 `releaseMotion(AxisId, MotionOwnership)`
- 实现 `motionOwner(AxisId)` 查询方法
- 构造函数中初始化所有轴的 `m_motionOwners` 为 `None`

**步骤 3：单元测试**

文件：`tests/domain/test_system_context.cpp`
- 测试 `tryAcquireMotion` 成功获取
- 测试 `tryAcquireMotion` 因资源已被占用而拒绝
- 测试 `releaseMotion` 后可以重新获取
- 测试错误的释放者不会影响所有权（安全校验）
- 测试同一个编排器重复获取（幂等/重入）

### 第二阶段：编排器改造

**步骤 4：改造 AutoAbsMoveOrchestrator**

文件：`application/policy/AutoAbsMoveOrchestrator.h`
- 新增 `bool m_motionAcquired = false` 成员
- `startAbs()` 中重置 `m_motionAcquired = false`
- `tick()` 中在 `EnsuringEnabled` 阶段首次调用 `tryAcquireMotion`
- `Done` 和 `Error` 分支中调用 `releaseMotion`
- **核心改动**：资源被占用时直接失败（不再等待）

**步骤 5：改造 AutoRelMoveOrchestrator**

文件：`application/policy/AutoRelMoveOrchestrator.h`
- 同步骤 4 的改动模式

**步骤 6：改造 JogOrchestrator**

文件：`application/policy/JogOrchestrator.h`
- 新增 `bool m_motionAcquired = false` 成员
- `startJog()` 中重置 `m_motionAcquired = false`
- `tick()` 中在 `EnsuringEnabled` 阶段首次调用 `tryAcquireMotion`
- ★ **核心改动**：移除 `EnsuringEnabled` 中的"等待任意状态"逻辑
- `EnsuringEnabled` 只处理 `Disabled → 发送 Enable` 和 `Idle → 进入 IssuingJog`
- 其他状态（如 MovingAbsolute）不会出现，因为已经在 `tryAcquireMotion` 层被拦截
- `Done` 和 `Error` 分支中调用 `releaseMotion`

**步骤 7：改造 GantryOrchestrator（如果涉及运动编排）**

文件：`application/policy/GantryOrchestrator.h`
- 同步骤 4 的改动模式（如果该编排器也进行运动控制）

### 第三阶段：单元测试与集成验证

**步骤 8：编排器单元测试**

文件：`tests/application/policy/test_auto_abs_move_orchestrator.cpp`
- 测试资源被占用时 `startAbs` → `tick` 应返回 `Error`（`MotionResourceBusy`）
- 测试正常流程获取和释放所有权

文件：`tests/application/policy/test_jog_orchestrator.cpp`
- 测试在 AbsMove 进行中调用 `startJog` 应失败
- 测试所有权在 Jog 完成后正确释放
- **回归测试**：正常点动流程仍然通过

**步骤 9：集成测试**

文件：`tests/infrastructure/test_system_integration.cpp`
- 测试场景：AbsMove 进行中发起 Jog → Jog 应被拒绝
- 测试场景：Jog 进行中发起 AbsMove → AbsMove 应被拒绝
- 测试场景：运动完成后所有权释放 → 下一个运动可以正常启动

### 第四阶段：ViewModel / UI 层增强（锦上添花）

**步骤 10：在 AxisViewModelCore 中暴露运动所有权状态**

文件：`presentation/viewmodel/AxisViewModelCore.h` / `.cpp`
- 新增属性 `bool isMotionOwnedExternally` 或类似
- 在 UI 按钮的 `enabled` 绑定中使用此属性
- 当轴被别的编排器占用时，按钮视觉上置灰

**此步骤为可选项**——即使不修改 UI，领域层的拦截也能彻底解决互斥问题。UI 层改动仅提升用户体验。

---

## 5. 实现伪代码参考

### 5.1 SystemContext::tryAcquireMotion

```cpp
bool SystemContext::tryAcquireMotion(AxisId id, MotionOwnership owner, ContextRejection& reason) {
    auto it = m_motionOwners.find(id);
    if (it == m_motionOwners.end()) {
        reason = ContextRejection::AxisNotRegistered;
        return false;
    }

    MotionOwnership current = it->second;
    if (current == MotionOwnership::None) {
        it->second = owner;
        reason = ContextRejection::None;
        return true;
    }
    if (current == owner) {
        // 同一个编排器重复获取（幂等），允许
        reason = ContextRejection::None;
        return true;
    }
    // 被其他编排器占用
    reason = ContextRejection::MotionResourceBusy;
    return false;
}
```

### 5.2 SystemContext::releaseMotion

```cpp
void SystemContext::releaseMotion(AxisId id, MotionOwnership owner) {
    auto it = m_motionOwners.find(id);
    if (it == m_motionOwners.end()) return;

    // 安全校验：只有持有者才能释放
    if (it->second == owner || it->second == MotionOwnership::None) {
        it->second = MotionOwnership::None;
    }
    // 否则：不匹配的释放者，忽略（防御性编程）
}
```

---

## 6. 影响评估

| 维度 | 评估 |
|---|---|
| **安全性** | ✅ 彻底杜绝两个编排器同时操作同一轴的风险 |
| **复杂度** | 每个编排器新增约 10 行代码（获取/释放所有权），改动量小 |
| **性能** | 每次 `tick()` 增加一次 `unordered_map` 查找，O(1)，可忽略不计 |
| **向后兼容** | 编排器接口不变，仅内部行为从"等待"变为"拒绝" |
| **测试影响** | 需要更新现有测试中编排器对轴状态的预期行为 |
| **领域模型纯净度** | `MotionOwnership` 枚举引用编排器类型名，轻微耦合但可接受（属于资源管理层面的元数据） |

---

## 7. 备选方案讨论（已排除）

### 方案 B：在 Axis 中加 ownership 标记
- ❌ 会将应用层概念（编排器类型）引入领域实体
- ❌ Axis 的职责是状态语义，不应关心"谁在驱动"
- ❌ 未来新增编排器需要修改领域层

### 方案 C：在编排器间通过 SystemManager 做全局状态协调
- ❌ 编排器之间不应该直接通信
- ❌ 违背了编排器相互独立的架构设计
- ❌ 新增编排器时协调逻辑会爆炸式增长

### 方案 D：在 ViewModel 层禁用按钮
- ❌ 只是 UI 层面的软约束，无法防止编程调用
- ❌ 不解决根本问题，可以被绕过
- ✅ 但可作为增强用户体验的辅助手段（步骤 10）

---

## 8. 总结

**问题根源：** 系统缺少"运动资源互斥"的概念。每个编排器独立运作，通过"等待轴空闲"来协调，导致一个编排器可以"埋伏"在另一个编排器的运动尾部抢夺轴的控制权。

**解决方案：** 在 `SystemContext` 聚合根中引入`MotionOwnership`（运动所有权）机制，作为 `tryGetAxis` 之后、业务逻辑之前的资源准入检查。任何运动编排器在启动时必须先获取所有权，所有权被占用时直接拒绝，不再等待。

**重构收益：**
1. 从架构根上消除运动冲突的可能性
2. 编排器的行为从"隐式等待"变为"显式拒绝"，语义更清晰
3. 不修改 Axis 领域模型，保持分层纯净
4. UI 层可以基于所有权状态提供更好的用户体验
