# Domain 层 TDD 架构设计文档  
## —— 双轴龙门同步系统

> **版本**：v1.0  
> **状态**：已实现 Phase 0–6，Phase 7+ 设计中  
> **业务约束来源**：[双轴龙门结构的业务语义约束（第7阶段）](./第7阶段：双轴同步龙门轴/双轴龙门结构的业务约束.md)

---

## 一、概述

### 1.1 设计目标

将“控制电机”升级为“定义运动语义系统”。Domain 层仅负责“是否允许操作”，不负责“如何执行”。所有物理差异（方向、坐标）由 HAL 层消化。

### 1.2 核心语义真理

1. **X 是唯一对外暴露的逻辑轴**  
2. **X1/X2 是实现细节，不参与业务决策**  
3. **所有操作基于统一逻辑方向（Forward/Backward）**  
4. **联动是状态，不是命令**  
5. **分动与联动互斥**  
6. **联动建立必须满足位置一致性**  
7. **运行过程中必须持续满足同步约束**  
8. **限位与报警优先级高于一切运动**  
9. **Domain 负责“是否允许操作”，不负责“如何执行”**  
10. **所有物理差异由 HAL 层消化**

### 1.3 分层架构

```
┌─────────────────────────────┐
│     Application / UI        │  UseCase 层，编排业务流程
├─────────────────────────────┤
│      Domain 层 (本层)        │  实体、值对象、领域服务、端口
├─────────────────────────────┤
│   Infrastructure / HAL      │  驱动实现、PLC 抽象
└─────────────────────────────┘
```

---

## 二、领域组件全景图

### 2.1 组件分类与文件清单

| 类型 | 组件名 | 文件路径 | 状态 |
|------|--------|----------|------|
| **实体** | `Axis` | `domain/entity/Axis.h` | ✅ 已实现 |
| **实体** | `GantryAxis` (聚合根) | `domain/entity/GantryAxis.h` | ⬜ 待实现 |
| **值对象** | `MotionDirection` | `domain/value/MotionDirection.h` | ✅ Phase 1 |
| **值对象** | `PositionConsistency` | `domain/value/PositionConsistency.h` | ✅ Phase 6 |
| **值对象** | `CouplingCondition` | `domain/value/CouplingCondition.h` | ✅ Phase 6 |
| **值对象** | `SafetyCheckResult` | `domain/value/SafetyCheckResult.h` | ✅ Phase 6 |
| **领域服务** | `GantryDomainService` | `domain/service/GantryDomainService.h` | ⬜ 待实现 |
| **端口接口** | `IGantryStatePort` | `domain/port/IGantryStatePort.h` | ⬜ 待实现 |

### 2.2 组件关系图（UML 风格）

```
                ┌─────────────┐
                │ GantryAxis  │  聚合根（实体）
                └──────┬──────┘
                       │ 持有
        ┌──────────────┼──────────────┐
        ▼              ▼              ▼
   MotionDirection  Axis (x2)   GantryMode
                               (Coupled/Decoupled)
                        │
        ┌───────────────┼───────────────┐
        ▼               ▼               ▼
  PositionConsistency  CouplingCondition  SafetyCheckResult
  (值对象)             (值对象)          (值对象)
        │               │               │
        └───────────────┴───────────────┘
                        │
                        ▼
               GantryDomainService
                 (领域服务)
                        │
                        ▼
               IGantryStatePort
                 (端口接口)
```

---

## 三、业务约束 → 代码 → 测试 全映射表

| 约束编号 | 约束内容 | 实现组件 | 核心 API / 逻辑 | 测试文件 | 测试数量 |
|----------|---------|---------|----------------|---------|---------|
| 1–4 | 模式定义与互斥 (Coupled ⊕ Decoupled) | `GantryMode` | 枚举值、GantryAxis 状态切换 | `test_gantry_mode.cpp` | 18 |
| 5–7 | 方向唯一性、逻辑方向禁止物理方向 | `MotionDirection` | `isForward()`, `isBackward()`, `oppositeDirection()` | `test_gantry_direction.cpp` | 12 |
| 8 | 统一位置定义 X.position | `PositionConsistency` | `computeLogicalPosition(x1Pos)` | `test_gantry_sync.cpp` | 3 |
| 9 | 镜像关系 X1.pos ≈ -X2.pos | `PositionConsistency` | `computeDeviation()`, `expectedX1FromX2()` | `test_gantry_sync.cpp` | 8 |
| 10 | 逻辑位置计算 X.position = X1.pos | `PositionConsistency` | `computeLogicalPosition()` | `test_gantry_sync.cpp` | 3 |
| 11 | 位置一致性失效判定 | `PositionConsistency` | `isConsistent()`, `describeDeviation()` | `test_gantry_sync.cpp` | 5 |
| 12–13 | 联动建立条件（使能、无报警、无限位、位置一致） | `CouplingCondition` | `checkAll()` 完整检查 / `checkPositionOnly()` 持续检查 | `test_gantry_sync.cpp` | 12 |
| 14 | 联动期间持续同步约束 | `CouplingCondition` | `checkPositionOnly()` | `test_gantry_sync.cpp` | 3 |
| 15–16 | 限位优先级最高 + 限位后方向限制 | `SafetyCheckResult` | `checkMotionSafety()` 方向安全检查矩阵 | `test_gantry_sync.cpp` | 20 |
| 17 | 报警状态下禁止所有运动 | `SafetyCheckResult` | `checkMotionSafety()` 报警优先检查 | `test_gantry_sync.cpp` | 3 |
| 18 | 操作对象互斥 (Couped→X, Decoupled→X1/X2) | `GantryAxis` (待实现) | 模式 + 对象绑定逻辑 | ⬜ | 0 |
| 19 | 运动互斥 (Jog ⊕ MoveAbsolute ⊕ MoveRelative) | `GantryAxis` (待实现) | 命令槽互斥逻辑 | ⬜ | 0 |
| 20 | 状态一致性 (聚合语义) | `GantryAxis` (待实现) | 状态合并规则 | ⬜ | 0 |

> **当前覆盖率**：已实现 120+ 测试用例，覆盖约束 1–17，待实现 18–20。

---

## 四、实体详解

### 4.1 Axis（单轴实体）

**职责**：封装单个物理轴的全部运行时状态与命令槽。

**特点**：
- 持有状态：`AxisState`（Unknown → Disabled → Idle → Jogging → MovingAbsolute → MovingRelative → Error）
- 持有反馈镜像：位置、限位状态、限位值
- 持有统一命令槽：`variant<JogCommand, MoveCommand, ...>`
- **不感知龙门语义**，完全按单轴逻辑运作

**状态转换图**：
```
        Disabled ←──→ Idle
           │           │
           └───────────┘
           enable(true/false)
```

**关键 API**：
```cpp
bool jog(Direction dir);
bool moveAbsolute(double target);
void applyFeedback(const AxisFeedback& feedback);
```

**当前状态**：✅ 已实现，19 个测试全部通过（`tests/domain/test_axis.cpp`）

---

### 4.2 GantryAxis（龙门聚合根）—— 待实现

**职责**：
- 将两个 `Axis` 实例（X1、X2）聚合为一个逻辑轴 `X`
- 以 `X` 作为唯一对外操作对象
- 管理 `Coupled` / `Decoupled` 模式状态机
- 在 Coupled 模式下，所有操作命令同时下发给 X1 和 X2
- 在 Decoupled 模式下，仅暴露 X1 或 X2 的操作能力

**状态机草案**：
```
    Decoupled ──(耦合申请通过)──▶ Coupled
    Coupled    ──(偏差超限/报警/退出)──▶ Decoupled
    Coupled    ──(分动命令)──▶ Decoupled
```

**关键 API 草案**：
```cpp
GantryMode mode() const;
Result requestCouple(const CouplingCondition::Input& conditions);
Result requestDecouple();
const GantryPosition& position() const;
Result executeCommand(const MotionCommand& cmd, MotionDirection dir);
```

**待实现约束**：约束 12–14（联动建立/维持）、约束 18（操作对象互斥）

---

## 五、值对象详解

### 5.1 MotionDirection（方向值对象）

**文件**：`domain/value/MotionDirection.h`  
**测试**：`tests/domain/gantry/test_gantry_direction.cpp`（19 个用例）

```cpp
enum class MotionDirection { Forward, Backward };

inline bool isForward(MotionDirection d);
inline bool isBackward(MotionDirection d);
inline MotionDirection oppositeDirection(MotionDirection d);
```

**设计要点**：
- 系统中唯一的方向语义定义
- 禁止任何地方使用 CW/CCW 或电机正反转
- `Forward` = 远离操作者，`Backward` = 靠近操作者

---

### 5.2 PositionConsistency（位置一致性值对象）

**文件**：`domain/value/PositionConsistency.h`  
**测试**：`tests/domain/gantry/test_gantry_sync.cpp`（19 个用例）

```cpp
class PositionConsistency {
public:
    static constexpr double kDefaultEpsilon = 0.01;  // 0.01 mm
    static constexpr double computeDeviation(double x1Pos, double x2Pos);
    static constexpr bool isConsistent(double x1Pos, double x2Pos, double epsilon = kDefaultEpsilon);
    static constexpr double computeLogicalPosition(double x1Pos);   // X.position = X1.pos
    static constexpr double expectedX1FromX2(double x2Pos);
    static std::string describeDeviation(double x1Pos, double x2Pos, double epsilon = kDefaultEpsilon);
};
```

**数学公式**：
- 偏差 `deviation = |X1.pos + X2.pos|`
- 一致性条件 `deviation ≤ epsilon`
- 逻辑位置 `X.position = X1.pos`

**测试覆盖**：
- 完美镜像偏差为 0
- 小/大失同步值正确
- epsilon 边界测试（含、恰好等于、超过）
- NaN 输入处理
- 极大值处理
- 诊断字符串生成

---

### 5.3 CouplingCondition（联动条件值对象）

**文件**：`domain/value/CouplingCondition.h`  
**测试**：`tests/domain/gantry/test_gantry_sync.cpp`（12 个用例）

```cpp
class CouplingCondition {
public:
    struct Result {
        bool allowed;
        std::string failReason;
        explicit operator bool() const;
    };

    static Result checkAll(
        bool x1Enabled, bool x2Enabled,
        bool anyAlarm, bool anyLimit,
        double x1Pos, double x2Pos,
        double epsilon = kDefaultEpsilon
    );

    static Result checkPositionOnly(
        double x1Pos, double x2Pos,
        double epsilon = kDefaultEpsilon
    );
};
```

**检查顺序**（短路逻辑）：
1. X1 未使能 → 立即拒绝
2. X2 未使能 → 立即拒绝（但 X1 优先）
3. 有报警 → 拒绝
4. 触发限位 → 拒绝
5. 位置不一致 → 拒绝

**诊断输出**：失败时 `failReason` 明确给出第一个失败原因

---

### 5.4 SafetyCheckResult（安全检查结果值对象）

**文件**：`domain/value/SafetyCheckResult.h`  
**测试**：`tests/domain/gantry/test_gantry_sync.cpp`（20 个用例）

```cpp
class SafetyCheckResult {
public:
    enum class Verdict {
        Allowed,
        Rejected_Alarm,
        Rejected_Limit,
        Rejected_LimitForward,
        Rejected_LimitBackward
    };
    
    static SafetyCheckResult allowed();
    static SafetyCheckResult rejectedDueToAlarm();
    static SafetyCheckResult rejectedDueToForwardLimit();
    static SafetyCheckResult rejectedDueToBackwardLimit();
    static SafetyCheckResult rejectedDueToLimit();  // 方向不明确时的兜底

    bool isAllowed() const;
    bool isRejected() const;
    Verdict verdict() const;
    explicit operator bool() const;  // true = Allowed
};

inline SafetyCheckResult checkMotionSafety(
    bool isAlarm,
    bool fwdLimitActive,
    bool bwdLimitActive,
    MotionDirection direction
);
```

**安全判断链**（优先级从高到低）：
1. 报警 → 全部拒绝（Alarm 覆盖所有限位判断）
2. 双向限位同时触发 → 按方向分别拒绝
3. 单向限位 → 仅拒绝朝向限位的方向，允许远离方向
4. 无报警无限位 → 全部允许

**方向安全矩阵**（12 种组合 12 个测试全部通过）：

| alarm | fwdLim | bwdLim | Forward | Backward |
|-------|--------|--------|---------|----------|
| false | false  | false  | ✅ Allowed | ✅ Allowed |
| true  | false  | false  | ❌ Alarm | ❌ Alarm |
| false | true   | false  | ❌ LimitForward | ✅ Allowed |
| false | false  | true   | ✅ Allowed | ❌ LimitBackward |
| false | true   | true   | ❌ LimitForward | ❌ LimitBackward |
| true  | true   | false  | ❌ Alarm | ❌ Alarm |
| true  | false  | true   | ❌ Alarm | ❌ Alarm |

---

## 六、领域服务设计（待实现）

### 6.1 GantryDomainService

**职责**：
- 接收来自 Application 层的运动命令，执行完整的安全与语义校验链
- 校验通过后，拆分命令到 X1/X2（或直接转发给单轴）
- 持续监控同步偏差（约束14），触发 `DeviationFault`

**命令处理流水线草案**：

```
Application Command
       │
       ▼
┌─────────────────┐
│ 1. 模式校验     │  Coupled/Decoupled 下操作对象是否合法？
├─────────────────┤
│ 2. 安全校验     │  checkMotionSafety() → 报警？限位？
├─────────────────┤
│ 3. 命令校验     │  Jog / Move / Stop 在当前位置是否合法？
├─────────────────┤
│ 4. 命令拆分     │  Coupled 模式下，将命令镜像给 X1/X2
├─────────────────┤
│ 5. 命令下发     │  调用 HAL 端口（通过 CommandBus）
└─────────────────┘
```

**API 草案**：
```cpp
class GantryDomainService {
public:
    explicit GantryDomainService(IGantryStatePort& port);
    
    MotionResult executeJog(MotionDirection dir);
    MotionResult executeMoveAbsolute(double target);
    MotionResult executeMoveRelative(double delta);
    MotionResult executeStop();

    CouplingResult requestCoupling();
    CouplingResult requestDecoupling();
};
```

---

## 七、端口接口设计（待实现）

### 7.1 IGantryStatePort

**职责**：定义 Domain 层对底层状态查询的抽象接口，与 HAL 无关。

```cpp
struct GantryHardwareState {
    bool x1Enabled;
    bool x2Enabled;
    bool anyAlarm;
    bool forwardLimitActive;
    bool backwardLimitActive;
    double x1Position;
    double x2Position;
};

class IGantryStatePort {
public:
    virtual ~IGantryStatePort() = default;
    
    // 查询当前硬件状态快照
    virtual GantryHardwareState getCurrentState() const = 0;
    
    // 查询模式
    virtual GantryMode getCurrentMode() const = 0;
};
```

**设计原则**：
- 仅暴露只读查询，不暴露写操作
- 写操作由 CommandBus 通过 Application 层下发
- 所有物理单位已在 HAL 层统一为毫米级浮点数

---

## 八、TDD 实施策略总结

### 8.1 实施链路

```
1. 阅读业务约束文档
2. 定义测试用例（红）
3. 定义值对象 / 实体接口（头文件）
4. 实现逻辑使测试通过（绿）
5. 重构优化（维持绿）
6. 运行全量测试确认无回归
```

### 8.2 已实施阶段

| Phase | 阶段名称 | 交付物 | 测试数 | 状态 |
|-------|---------|-------|-------|------|
| 0 | 实体基础 | `Axis` 实体 + `AxisState` 状态机 | 19 | ✅ |
| 1 | 方向语义 | `MotionDirection` 值对象 | 12 | ✅ |
| 2 | 模式语义 | `GantryMode` 枚举 + 状态验证 | 18 | ✅ |
| 3 | 位置语义 | X 逻辑位置计算测试 | 19 | ✅ |
| 4–5 | (合并入 6) | | | |
| 6 | 同步与安全 | `PositionConsistency` + `CouplingCondition` + `SafetyCheckResult` | 60 | ✅ |
| **合计** | | | **128** | ✅ |

### 8.3 待实施阶段

| Phase | 阶段名称 | 预计测试数 |
|-------|---------|----------|
| 7 | `GantryAxis` 聚合根 | ~25 |
| 8 | `GantryDomainService` 领域服务 | ~30 |
| 9 | 端口接口 + Fake 驱动集成 | ~15 |
| **预计总测试数** | | **~200** |

### 8.4 测试目录结构

```
tests/domain/
├── test_axis.cpp                    # Axis 实体测试（19 个）
└── gantry/
    ├── test_gantry_mode.cpp         # 模式语义（18 个）
    ├── test_gantry_direction.cpp    # 方向语义（12 个）
    ├── test_gantry_position.cpp     # 位置语义（19 个）
    ├── test_gantry_sync.cpp         # 同步与安全（60 个）
    ├── test_gantry_axis.cpp         # ⬜ 聚合根（待创建）
    └── test_gantry_service.cpp      # ⬜ 领域服务（待创建）
```

---

## 九、后续规划

### 9.1 立即待办

1. **Phase 7**: 实现 `GantryAxis` 聚合根（TDD）
2. **Phase 8**: 实现 `GantryDomainService` 领域服务
3. **Phase 9**: 定义端口接口 + Fake 实现 + 集成测试

### 9.2 架构扩展方向

- **多龙门支持**：将 `GantryAxis` 扩展为 `GantryGroup`，管理 Y1/Y2/Z1/Z2 等多对轴
- **轨迹规划**：在 Application 层加入前瞻算法，Domain 层仅做合法性校验
- **持久化**：引入 Repository 模式，将轴配置、限位参数持久化到 EEPROM/文件

---

> 📌 **设计原则回顾**  
> *"Domain 层只回答 '能不能动'，不回答 '怎么动'。  
> 所有物理知识属于 HAL 层，所有业务规则属于 Domain 层。"*
