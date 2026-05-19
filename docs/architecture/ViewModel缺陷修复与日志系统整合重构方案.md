# ViewModel 缺陷修复与日志系统整合重构方案

> **文档版本:** v1.0  
> **分析日期:** 2026-05-17  
> **目标范围:** `presentation/viewmodel/` + `infrastructure/logger/`  
>
> **本文档是基于《ViewModel架构与TDD测试全分析》第 9 章"存在缺陷与改进建议"的深化重构方案，将缺陷修复与日志系统整合为一次系统性的重构计划。**

---

## 目录

1. [缺陷总览与根因分析](#1-缺陷总览与根因分析)
2. [日志系统现状评估](#2-日志系统现状评估)
3. [重构目标与原则](#3-重构目标与原则)
4. [重构方案一：错误收集模型升级](#4-重构方案一错误收集模型升级)
5. [重构方案二：Q_PROPERTY 补齐与 ErrorCategory 映射](#5-重构方案二q_property-补齐与-errorcategory-映射)
6. [重构方案三：日志系统深度整合](#6-重构方案三日志系统深度整合)
7. [重构方案四：零位/速度操作路径统一](#7-重构方案四零位速度操作路径统一)
8. [重构方案五：测试补充计划](#8-重构方案五测试补充计划)
9. [删除清单](#9-删除清单)
10. [实施路线图](#10-实施路线图)
11. [附录：重构后的完整文件清单](#11-附录重构后的完整文件清单)

---

## 1. 缺陷总览与根因分析

### 1.1 缺陷分级汇总

从分析文档中提取的 12 个缺陷，按照严重度和领域分组：

```
🔴 严重缺陷 (4)
├─ #1 Q_PROPERTY 缺少 isEnabled          -> QtAxisViewModel.h
├─ #2 错误优先级策略丢失错误               -> AxisViewModelCore::tick()
├─ #3 零位/速度命令无错误保护              -> AxisViewModelCore (5 处)
└─ #4 disable() 无条件重置错误             -> AxisViewModelCore::enable(false)

🟡 中等缺陷 (5)
├─ #5 QML 无 ErrorCategory 映射           -> QtAxisViewModel.h
├─ #6 缺少状态文本描述                     -> QtAxisViewModel.h / QML
├─ #7 velocity 无信号节流                  -> QtAxisViewModel::tick()
├─ #8 posLimit/negLimit 无变化检测         -> QtAxisViewModel::tick()
└─ #9 错误接口测试薄弱                     -> test_axis_viewmodel_core.cpp

🔵 轻微缺陷 (3)
├─ #10 硬编码日志上下文                    -> AxisViewModelCore.cpp (历史问题)
├─ #11 tick() 中 driver->send() 重复调用   -> AxisViewModelCore::tick()
└─ #12 ErrorTranslator 测试代码冗余        -> test_error_translator.cpp
```

### 1.2 根因聚类分析

| 根因类别 | 涉及缺陷 | 说明 |
|---------|---------|------|
| **错误模型过于简单** | #2, #3, #4, #9 | 单值覆盖模型导致信息丢失，操作路径不一致导致保护缺失 |
| **QML 集成不完整** | #1, #5, #6, #7, #8 | Q_PROPERTY 映射不全面，QML 侧需要额外逻辑 |
| **日志缺失/硬编码** | #10, #11 | 操作路径无日志追踪，上下文硬编码 |
| **测试覆盖不完整** | #9, #12 | 缺少错误触发场景，测试代码结构可优化 |

### 1.3 缺陷修复的依赖关系

```
┌─────────────────────────────────────────────────────────────────┐
│  Phase 1: 错误模型升级（阻塞性）                                   │
│  ┌─────────────────────────────────────────────────────────────┐ │
│  │  #2 错误收集改为列表    ->  m_errors: vector<ViewModelError> │ │
│  │  #3 零位/速度加保护     ->  检查 Axis::lastRejection()       │ │
│  │  #4 disable()修复       ->  跳过错误收集 (enable(false))     │ │
│  └─────────────────────────────────────────────────────────────┘ │
│                                    │                              │
│                                    ▼                              │
│  Phase 2: Q_PROPERTY 补齐 + 日志整合（并行）                      │
│  ┌─────────────────────────────┐  ┌─────────────────────────────┐│
│  │  #1 isEnabled 属性          │  │  #10 结构化日志注入          ││
│  │  #5 ErrorCategory 属性      │  │  #11 操作日志追踪            ││
│  │  #6 状态文本属性            │  │   Logger + TraceScope 整合   ││
│  │  #7/#8 信号节流补齐         │  │                              ││
│  └─────────────────────────────┘  └─────────────────────────────┘│
│                                    │                              │
│                                    ▼                              │
│  Phase 3: 测试补充                                               │
│  ┌─────────────────────────────────────────────────────────────┐ │
│  │  #9 错误场景测试 + #12 ErrorTranslator 测试优化              │ │
│  └─────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. 日志系统现状评估

### 2.1 现有日志基础设施能力矩阵

| 组件 | 能力 | 当前使用情况 |
|------|------|-------------|
| **Logger** | 异步双缓冲队列写入控制台/文件 | ✅ 全局可用，但 ViewModel 层未使用 |
| **TraceScope** | 线程局部栈，自动追踪 group/axis/traceId | ✅ RAII 管理，但 ViewModel 未创建 Scope |
| **CommandFormatter** | AxisCommand 格式化（含所有命令类型） | ✅ 可用，但 ViewModel 层未调用 |
| **日志宏** | `LOG_INFO`, `LOG_WARN`, `LOG_ERROR` 等 | ✅ 可用，但 ViewModel 中仅注释中存在 |

### 2.2 当前 ViewModel 层日志状态

```cpp
// AxisViewModelCore.cpp 当前无任何日志输出
// 对比：重构前的代码有 TraceScope 和 LOG_INFO 埋点，重构后丢失了

// QtAxisViewModel.cpp 当前无任何日志输出
// tick() 中仅有缓存比较逻辑，无日志

// ErrorTranslator.cpp 当前无任何日志输出
// 大量错误翻译无调试记录
```

**结论：** 重构后的 ViewModel 层**完全缺失日志追踪**，这是一个严重的可观测性缺口。所有操作路径、错误翻译、编排器执行过程都无法从日志中追踪。

### 2.3 日志系统与 ViewModel 的集成机会

```
操作入口 (enable / jog / moveAbsolute / ...)
    │
    ├─-> [缺少] TraceScope 创建 (group + axis + traceId)
    ├─-> [缺少] LOG_INFO 记录操作意图
    │
    ▼
UseCase / Orchestrator 执行
    │
    ├─-> [缺少] LOG_TRACE 记录步骤推进
    ├─-> [缺少] CommandFormatter 记录下发的命令内容
    │
    ▼
错误收集 / 翻译
    │
    ├─-> [缺少] LOG_WARN / LOG_ERROR 记录错误发生
    ├─-> [缺少] 调试信息结构化为 debugMessage
    │
    ▼
tick() 驱动
    │
    ├─-> [缺少] LOG_TRACE_EVERY_N 周期性状态摘要
    └─-> [缺少] LOG_SUMMARY 阶段总结
```

---

## 3. 重构目标与原则

### 3.1 核心目标

```
┌────────────────────────────────────────────────────────────┐
│ 🎯 目标 1: 缺陷修复                                        │
│     修复 #1-#12 所有已识别缺陷                             │
│                                                             │
│ 🎯 目标 2: 日志可观测性                                     │
│     让 ViewModel 层每条操作路径都有日志记录                  │
│     TraceScope 覆盖所有入口函数                              │
│                                                             │
│ 🎯 目标 3: 错误模型升级                                      │
│     从"单值覆盖"升级为"列表收集+去重"                        │
│     ErrorCategory 正确映射到 QML                             │
│                                                             │
│ 🎯 目标 4: 命令路径统一                                      │
│     零位/速度操作与运动操作采用一致的错误保护模式              │
└────────────────────────────────────────────────────────────┘
```

### 3.2 设计原则

| 原则 | 描述 | 影响文件 |
|------|------|---------|
| **P1: 无硬编码** | 所有 group/axis/traceId 从 ViewModel 成员变量获取 | AxisViewModelCore |
| **P2: 日志即文档** | 每个操作入口记录意图，每个错误记录上下文 | AxisViewModelCore, QtAxisViewModel |
| **P3: 错误不丢失** | 错误收集改为列表，按发生时间排序 | AxisViewModelCore |
| **P4: 单一路径** | 零位/速度操作与 UseCase 操作采用相同错误处理模式 | AxisViewModelCore |
| **P5: QML 友好** | Q_PROPERTY 映射完整，QML 无需额外逻辑 | QtAxisViewModel |

---

## 4. 重构方案一：错误收集模型升级

### 4.1 当前问题

```cpp
// 当前：单值覆盖模式（缺陷 #2, #4）
ViewModelError m_lastError = {};
bool m_hasError = false;

// tick() 中依次覆盖：
if (m_jogOrch->hasError()) { m_lastError = translate(...); }
if (m_absOrch->hasError()) { m_lastError = translate(...); } // 覆盖前一个
if (m_relOrch->hasError()) { m_lastError = translate(...); } // 覆盖前两个
```

**问题 1：** `enable(false)` 调用 `executeAndTranslate()` 会无条件覆盖之前的错误。  
**问题 2：** 三个 Orchestrator 的错误只有最后一个被保留，前序错误丢失。

### 4.2 目标设计

```cpp
// 目标：列表收集模式
struct ErrorEntry {
    ViewModelError error;
    std::chrono::steady_clock::time_point timestamp;
    std::string source;  // "JogOrch" / "AbsOrch" / "RelOrch" / "EnableUC" / "StopUC"
};

// AxisViewModelCore 新增成员
std::vector<ErrorEntry> m_errorHistory;   // 按时间排序的错误历史
size_t m_currentErrorIndex = 0;           // 当前指向的错误索引
```

### 4.3 核心 API 变更

```cpp
// AxisViewModelCore.h -- 错误接口替换

// ==== 替换前 ====
bool hasError() const;
ViewModelError lastError() const;
void clearError();

// ==== 替换后 ====
bool hasError() const;                           // m_errorHistory 非空
ViewModelError lastError() const;                // 返回 m_errorHistory 中最新一条
std::vector<ViewModelError> allErrors() const;   // 返回全部未确认错误（新增）
void acknowledgeError(size_t index);             // 确认单条错误（新增）
void clearAllErrors();                           // 确认所有错误（替换 clearError）
```

### 4.4 tick() 错误收集实现

```cpp
void AxisViewModelCore::tick() {
    // Step 1: 驱动所有编排器
    m_jogOrch->tick();
    m_absOrch->tick();
    m_relOrch->tick();

    // Step 2: 收集错误（追加模式，不再覆盖）
    collectOrchError(*m_jogOrch, "JogOrch");
    collectOrchError(*m_absOrch, "AbsOrch");
    collectOrchError(*m_relOrch, "RelOrch");

    // Step 3: 消费零位/速度类 pending command
    consumePendingCommands();

    // Step 4: 日志摘要（每 100 帧输出一次）
    LOG_TRACE_EVERY_N(100, LogLayer::UI, "AxisVM", 
        m_groupName + "/" + axisIdToString(m_axisId) + 
        " tick: state=" + std::to_string(static_cast<int>(state())) +
        " errors=" + std::to_string(m_errorHistory.size()));
}

template<typename Orch>
void AxisViewModelCore::collectOrchError(Orch& orch, const std::string& source) {
    if (orch.hasError()) {
        auto vmError = translate(orch.lastError());
        if (vmError.isValid()) {  // 跳过 Silent 错误
            m_errorHistory.push_back({
                .error = vmError,
                .timestamp = std::chrono::steady_clock::now(),
                .source = source
            });
            LOG_WARN(LogLayer::UI, "AxisVM", 
                m_groupName + "/" + axisIdToString(m_axisId) +
                " error from " + source + ": " + vmError.code +
                " - " + vmError.debugMessage);
        }
    }
}
```

### 4.5 enable/disable 错误修复

```cpp
void AxisViewModelCore::enable(bool active) {
    // 修复 #4: disable 时不覆盖已有错误
    if (!active) {
        // disable 操作：直接执行，不检查错误
        m_enableUc->execute(m_manager, m_groupName, m_axisId, false);
        LOG_INFO(LogLayer::UI, "AxisVM",
            m_groupName + "/" + axisIdToString(m_axisId) + " disable requested");
        return;
    }
    
    // enable 操作：正常收集错误
    auto result = m_enableUc->execute(m_manager, m_groupName, m_axisId, true);
    if (!std::holds_alternative<std::monostate>(result)) {
        auto vmError = translate(result);
        if (vmError.isValid()) {
            m_errorHistory.push_back({
                .error = vmError,
                .timestamp = std::chrono::steady_clock::now(),
                .source = "EnableUC"
            });
            LOG_ERROR(LogLayer::UI, "AxisVM",
                m_groupName + "/" + axisIdToString(m_axisId) +
                " enable failed: " + vmError.code);
        }
    } else {
        LOG_INFO(LogLayer::UI, "AxisVM",
            m_groupName + "/" + axisIdToString(m_axisId) + " enabled");
    }
}

void AxisViewModelCore::disable() {
    enable(false);  // 直接转发，不再产生错误
}
```

### 4.6 零位/速度操作错误保护修复

```cpp
// 修复 #3: 所有零位/速度操作加错误保护

void AxisViewModelCore::zeroAbsolutePosition() {
    TraceScope scope(m_groupName, axisIdToString(m_axisId), generateTraceId());
    LOG_INFO(LogLayer::UI, "AxisVM", "User requested zeroAbsolutePosition");
    
    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (!axis) {
        auto error = ViewModelError{
            "CTX_AXIS_NOT_REGISTERED",
            "轴未注册",
            groupName() + "/" + axisIdToString(m_axisId) + " not found in system context",
            ErrorCategory::Modal
        };
        pushError(error, "ZeroAbsOp");
        return;
    }
    
    if (!axis->zeroAbsolutePosition()) {
        auto vmError = translate(UseCaseError{axis->lastRejection()});
        pushError(vmError, "ZeroAbsOp");
        LOG_WARN(LogLayer::UI, "AxisVM",
            m_groupName + "/" + axisIdToString(m_axisId) +
            " zeroAbsolutePosition rejected: " + vmError.code);
    } else {
        LOG_DEBUG(LogLayer::UI, "AxisVM",
            m_groupName + "/" + axisIdToString(m_axisId) +
            " zeroAbsolutePosition accepted, pending command queued");
    }
}

void AxisViewModelCore::setJogVelocity(double v) {
    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (!axis) return;  // 静默失败：速度设置不产生错误

    if (!axis->setJogVelocity(v)) {
        auto vmError = translate(UseCaseError{axis->lastRejection()});
        pushError(vmError, "SetJogVel");
        LOG_WARN(LogLayer::UI, "AxisVM",
            m_groupName + "/" + axisIdToString(m_axisId) +
            " setJogVelocity(" + std::to_string(v) + ") rejected: " + vmError.code);
    } else {
        LOG_DEBUG(LogLayer::UI, "AxisVM",
            m_groupName + "/" + axisIdToString(m_axisId) +
            " jogVelocity set to " + std::to_string(v));
    }
}

// ===== 辅助函数 =====
void AxisViewModelCore::pushError(const ViewModelError& error, const std::string& source) {
    if (!error.isValid()) return;
    m_errorHistory.push_back({
        .error = error,
        .timestamp = std::chrono::steady_clock::now(),
        .source = source
    });
}
```

---

## 5. 重构方案二：Q_PROPERTY 补齐与 ErrorCategory 映射

### 5.1 新增 Q_PROPERTY（修复 #1, #5, #6）

```cpp
// QtAxisViewModel.h -- 新增属性

class QtAxisViewModel : public QObject {
    Q_OBJECT

    // ==== 现有属性（保留） ====
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(double absPos READ absPos NOTIFY absPosChanged)
    Q_PROPERTY(double relPos READ relPos NOTIFY relPosChanged)
    Q_PROPERTY(double posLimit READ posLimit NOTIFY limitsChanged)
    Q_PROPERTY(double negLimit READ negLimit NOTIFY limitsChanged)
    Q_PROPERTY(double jogVelocity READ jogVelocity WRITE setJogVelocity NOTIFY velocityChanged)
    Q_PROPERTY(double moveVelocity READ moveVelocity WRITE setMoveVelocity NOTIFY velocityChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY errorChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY errorChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)

    // ⭐ 新增属性（修复 #1, #5, #6）
    Q_PROPERTY(bool isEnabled READ isEnabled NOTIFY stateChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    Q_PROPERTY(QString errorCategory READ errorCategory NOTIFY errorChanged)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY errorChanged)

public:
    // ⭐ 新增 getter
    bool isEnabled() const;
    QString stateText() const;
    QString errorCategory() const;
    int errorCount() const;

    // ⭐ 新增 Q_INVOKABLE（支持错误确认）
    Q_INVOKABLE QVariantList getAllErrors() const;      // 返回全部未确认错误
    Q_INVOKABLE void acknowledgeError(int index);       // 确认单条错误
    Q_INVOKABLE void acknowledgeAllErrors();            // 确认所有错误

signals:
    // ⭐ 新增 signal
    void errorCountChanged();
};
```

### 5.2 stateText 映射实现

```cpp
// QtAxisViewModel.cpp -- 状态文本映射

QString QtAxisViewModel::stateText() const {
    switch (m_core->state()) {
    case AxisState::Unknown:        return "未知";
    case AxisState::Disabled:       return "未使能";
    case AxisState::Idle:           return "就绪";
    case AxisState::Jogging:        return "点动中";
    case AxisState::MovingAbsolute: return "绝对定位";
    case AxisState::MovingRelative: return "相对定位";
    case AxisState::Error:          return "报警";
    default:                        return "未知";
    }
}

QString QtAxisViewModel::errorCategory() const {
    if (!m_core->hasError()) return {};
    switch (m_core->lastError().category) {
    case ErrorCategory::Inline: return "Inline";
    case ErrorCategory::Modal:  return "Modal";
    case ErrorCategory::Silent: return "Silent";
    default:                    return "Unknown";
    }
}

int QtAxisViewModel::errorCount() const {
    return static_cast<int>(m_core->errorCount());
}
```

### 5.3 信号节流补齐（修复 #7, #8）

```cpp
// QtAxisViewModel.h -- 新增缓存成员
class QtAxisViewModel : public QObject {
private:
    // ... 现有缓存成员 ...
    double m_lastJogVelocity = 0.0;   // ⭐ 新增
    double m_lastMoveVelocity = 0.0;  // ⭐ 新增
    int m_lastErrorCount = 0;         // ⭐ 新增
};

// QtAxisViewModel.cpp -- tick() 节流补齐

void QtAxisViewModel::tick() {
    m_core->tick();

    // stateChanged（现有）
    AxisState currentState = m_core->state();
    if (currentState != m_lastState) {
        m_lastState = currentState;
        emit stateChanged();  // 同时带动 isEnabled / stateText
    }

    // absPosChanged（现有）
    double currentAbsPos = m_core->absPos();
    if (std::abs(currentAbsPos - m_lastAbsPos) > EPSILON) {
        m_lastAbsPos = currentAbsPos;
        emit absPosChanged();
    }

    // relPosChanged（现有）
    double currentRelPos = m_core->relPos();
    if (std::abs(currentRelPos - m_lastRelPos) > EPSILON) {
        m_lastRelPos = currentRelPos;
        emit relPosChanged();
    }

    // ⭐ 新增：velocityChanged 节流
    double currentJogV = m_core->jogVelocity();
    double currentMoveV = m_core->moveVelocity();
    if (std::abs(currentJogV - m_lastJogVelocity) > EPSILON ||
        std::abs(currentMoveV - m_lastMoveVelocity) > EPSILON) {
        m_lastJogVelocity = currentJogV;
        m_lastMoveVelocity = currentMoveV;
        emit velocityChanged();
    }

    // errorChanged（增强版）
    bool currentHasError = m_core->hasError();
    int currentErrorCount = static_cast<int>(m_core->errorCount());
    if (currentHasError != m_lastHasError || currentErrorCount != m_lastErrorCount) {
        m_lastHasError = currentHasError;
        m_lastErrorCount = currentErrorCount;
        if (currentHasError) {
            auto e = m_core->lastError();
            m_lastErrorCode    = QString::fromStdString(e.code);
            m_lastErrorMessage = QString::fromStdString(e.userMessage);
        } else {
            m_lastErrorCode.clear();
            m_lastErrorMessage.clear();
        }
        emit errorChanged();
        emit errorCountChanged();
    }

    // ⭐ 新增：limitsChanged（Periodic check -- 每 100 帧检查一次）
    LOG_TRACE_EVERY_N(100, LogLayer::UI, "QtAxisVM",
        "tick end: state=" + QString::number(static_cast<int>(currentState)).toStdString() +
        " absPos=" + std::to_string(currentAbsPos) +
        " errors=" + std::to_string(currentErrorCount));
}
```

### 5.4 QML 侧使用示例

```qml
// QML 使用示例 -- ActionControlBlock.qml（重构后）

// 状态灯: 通过 stateText 直接显示
Label {
    text: viewModel ? viewModel.stateText : "未连接"
    color: viewModel && viewModel.isEnabled ? Theme.colorIdle : Theme.colorDisabled
}

// 错误显示: 区分 Inline / Modal
Rectangle {
    visible: viewModel ? viewModel.hasError : false
    color: viewModel && viewModel.errorCategory === "Modal" 
           ? Theme.colorError   // 弹窗级错误: 红色背景
           : Theme.colorWarning  // 内联级错误: 黄色背景
    
    Text {
        text: viewModel ? viewModel.errorMessage : ""
    }
    
    // Modal 错误显示确认按钮
    Button {
        visible: viewModel && viewModel.errorCategory === "Modal"
        text: "确认"
        onClicked: viewModel.acknowledgeAllErrors()
    }
}

// 使能状态: 直接绑定 isEnabled
Button {
    enabled: viewModel ? viewModel.isEnabled : false
    text: "执行"
}
```

---

## 6. 重构方案三：日志系统深度整合

### 6.1 ViewModel 日志埋点完整清单

| 操作 | 日志级别 | 日志内容 | TraceScope |
|------|---------|---------|-----------|
| `enable(true)` | INFO | "enable requested for [group]/[axis]" | ✅ 创建 |
| `enable(false)` | INFO | "disable requested for [group]/[axis]" | ✅ 创建 |
| `jog(dir)` | INFO | "jog [Forward/Backward] pressed" | ✅ 创建 |
| `jogStop(dir)` | INFO | "jog [Forward/Backward] released" | ✅ 创建 |
| `moveAbsolute(target)` | INFO | "moveAbsolute [target]" | ✅ 创建 |
| `moveRelative(distance)` | INFO | "moveRelative [distance]" | ✅ 创建 |
| `stop()` | INFO | "stop pressed (may interrupt active motion)" | ✅ 创建 |
| `zeroAbsolutePosition()` | INFO | "zeroAbsolutePosition" | ✅ 创建 |
| `setRelativeZero()` | INFO | "setRelativeZero" | ✅ 创建 |
| `clearRelativeZero()` | INFO | "clearRelativeZero" | ✅ 创建 |
| `setJogVelocity(v)` | DEBUG | "jogVelocity set to [v]" | ❌ 不创建 |
| `setMoveVelocity(v)` | DEBUG | "moveVelocity set to [v]" | ❌ 不创建 |
| `tick()` | TRACE_EVERY_N(100) | "tick summary: state, errors count" | ❌ 不创建 |
| 错误产生 | WARN/ERROR | "error from [source]: [code] - [debugMessage]" | ✅ 复用 |
| 命令下发 | DEBUG | "sending command: [CommandFormatter::format()]" | ✅ 复用 |

### 6.2 TraceScope 注入模式

所有控制入口统一使用以下模式：

```cpp
// ===== 标准模式：操作入口 =====
void AxisViewModelCore::moveAbsolute(double targetPos) {
    // 1. 创建追踪上下文
    TraceScope scope(m_groupName, axisIdToString(m_axisId), generateTraceId());
    
    // 2. 记录操作意图
    LOG_INFO(LogLayer::UI, "AxisVM",
        m_groupName + "/" + axisIdToString(m_axisId) +
        " moveAbsolute target=" + std::to_string(targetPos));
    
    // 3. 执行操作
    m_absOrch->startAbs(m_axisId, targetPos);
    
    // 4. 记录执行结果（如果有同步错误）
    if (m_absOrch->hasError()) {
        auto vmError = translate(m_absOrch->lastError());
        LOG_WARN(LogLayer::UI, "AxisVM",
            m_groupName + "/" + axisIdToString(m_axisId) +
            " moveAbsolute rejected at start: " + vmError.code);
    }
    // scope 自动销毁，TraceContext 自动弹出
}

// ===== 简化模式：无参操作 =====
void AxisViewModelCore::stop() {
    TraceScope scope(m_groupName, axisIdToString(m_axisId), generateTraceId());
    LOG_INFO(LogLayer::UI, "AxisVM", "stop pressed");

    auto result = m_stopUc->execute(m_manager, m_groupName, m_axisId);
    if (!std::holds_alternative<std::monostate>(result)) {
        auto vmError = translate(result);
        pushError(vmError, "StopUC");
        LOG_ERROR(LogLayer::UI, "AxisVM",
            m_groupName + "/" + axisIdToString(m_axisId) +
            " stop command failed: " + vmError.code);
    }

    if (/* Jog in progress */) {
        m_jogOrch->stopJog(m_axisId, Direction::Forward);
        LOG_DEBUG(LogLayer::UI, "AxisVM",
            m_groupName + "/" + axisIdToString(m_axisId) +
            " jog orchestrator interrupted by stop");
    }
}
```

### 6.3 TraceScope 辅助方法

```cpp
// AxisViewModelCore.h -- 新增辅助方法

private:
    // 生成唯一追踪 ID（基于时间戳 + 序列号）
    static std::string generateTraceId() {
        static std::atomic<uint64_t> counter{0};
        auto now = std::chrono::steady_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
        return std::to_string(ns) + "_" + std::to_string(++counter);
    }

    // AxisId -> 字符串（避免硬编码 "Y" / "Z" 等）
    static std::string axisIdToString(AxisId id) {
        switch (id) {
        case AxisId::X1: return "X1";
        case AxisId::X2: return "X2";
        case AxisId::Y:  return "Y";
        case AxisId::Z:  return "Z";
        default:         return "Unknown";
        }
    }
```

### 6.4 命令下发日志

```cpp
// AxisViewModelCore.cpp -- tick() 中命令消费增加日志

void AxisViewModelCore::consumePendingCommands() {
    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (!axis || !axis->hasPendingCommand()) return;

    const AxisCommand& cmd = axis->getPendingCommand();
    bool isZeroOrVelocity =
        std::holds_alternative<ZeroAbsoluteCommand>(cmd) ||
        std::holds_alternative<SetRelativeZeroCommand>(cmd) ||
        std::holds_alternative<ClearRelativeZeroCommand>(cmd) ||
        std::holds_alternative<SetJogVelocityCommand>(cmd) ||
        std::holds_alternative<SetMoveVelocityCommand>(cmd);

    if (!isZeroOrVelocity) return;  // 运动类命令由 Orchestrator 处理

    // 获取驱动
    SystemContext* group = nullptr;
    ContextRejection reason;
    if (!m_manager.tryGetGroup(m_groupName, group, reason)) {
        LOG_ERROR(LogLayer::UI, "AxisVM",
            m_groupName + "/" + axisIdToString(m_axisId) +
            " cannot send command: group not found");
        return;
    }

    auto* drv = group->driver();
    if (!drv) {
        LOG_ERROR(LogLayer::UI, "AxisVM",
            m_groupName + "/" + axisIdToString(m_axisId) +
            " cannot send command: driver not ready");
        return;
    }

    // ⭐ 使用 CommandFormatter 记录下发的命令详情
    LOG_DEBUG(LogLayer::UI, "AxisVM",
        m_groupName + "/" + axisIdToString(m_axisId) +
        " sending: " + utils::format(cmd));

    auto commResult = drv->send(AxisCommandWithId{m_axisId, cmd});
    if (!commResult.ok()) {
        auto vmError = ViewModelError{
            "ZERO_CMD_FAILED",
            "零位/速度命令下发失败",
            commResult.diagnostic,
            ErrorCategory::Modal
        };
        pushError(vmError, "CmdDelivery");
        LOG_ERROR(LogLayer::UI, "AxisVM",
            m_groupName + "/" + axisIdToString(m_axisId) +
            " command delivery failed: " + commResult.diagnostic);
    } else {
        LOG_TRACE(LogLayer::UI, "AxisVM",
            m_groupName + "/" + axisIdToString(m_axisId) +
            " command delivered successfully");
    }

    // 修复 #11: 消费后清除 pending command，防止重复下发
    // 注意：Axis::hasPendingCommand() 应在 send 成功后内部清除
    // 如果 Axis 未自动清除，需要增加清除逻辑
}
```

---

## 7. 重构方案四：零位/速度操作路径统一

### 7.1 当前路径对比

```
运动操作路径（正确）:
  jog(Direction) ──-> JogOrchestrator::startJog()
                      └── Orchestrator 内部: check state -> UseCase -> send
                    错误 -> tick() -> collectOrchError()

零位操作路径（缺陷）:
  zeroAbsolutePosition() ──-> axis->zeroAbsolutePosition()
                              └── 直接 Axis 领域操作
                            命令 -> tick() -> driver->send()
                            错误 -> 无检查（缺陷 #3）

速度设置路径（缺陷）:
  setJogVelocity(v) ──-> axis->setJogVelocity(v)
                          └── 直接 Axis 领域操作
                        命令 -> tick() -> driver->send()
                        错误 -> 无检查（缺陷 #3）
```

### 7.2 统一后的路径

```
所有操作采用统一模式:

  enable(bool) ──-> UseCase::execute() -> 立即检查错误 -> LOG_INFO/WARN
  jog(Direction) ──-> Orchestrator::startJog() -> tick() 收集错误 -> LOG_WARN
  moveAbsolute(t) ──-> Orchestrator::startAbs() -> tick() 收集错误 -> LOG_WARN
  stop() ──-> UseCase::execute() + Orch 中断 -> 立即检查错误 -> LOG_ERROR
  
  ⭐ 零位操作: zeroAbsolutePosition()
     尝试 Axis 操作 -> 检查 lastRejection() -> 有错误则 pushError + LOG_WARN
     命令留在 pending -> tick() 中 consumePendingCommands() 下发 -> 检查 send 结果
  
  ⭐ 速度设置: setJogVelocity(v)
     尝试 Axis 操作 -> 检查 lastRejection() -> 有错误则 pushError + LOG_WARN
     命令留在 pending -> tick() 中 consumePendingCommands() 下发 -> 检查 send 结果
```

### 7.3 操作分类规范化

| 类别 | 操作 | 执行模式 | 错误检查时机 |
|------|------|---------|------------|
| **UseCase 同步** | `enable()`, `disable()`, `stop()` | 调用后立即检查 UseCaseError | 同步 |
| **Orchestrator 异步** | `jog()`, `jogStop()`, `moveAbsolute()`, `moveRelative()` | 触发编排器后返回，tick 中收集 | 异步 |
| **Axis 同步 + 异步下发** | `zeroAbsolutePosition()`, `setRelativeZero()`, `clearRelativeZero()`, `setJogVelocity()`, `setMoveVelocity()` | Axis 操作时检查领域拒绝，下发时检查通讯错误 | 同步+异步 |

---

## 8. 重构方案五：测试补充计划

### 8.1 新增测试用例（修复 #9）

```cpp
// tests/presentation/viewmodel/test_axis_viewmodel_core.cpp -- 新增

// =========================================================
// 新增测试 14: 限位触发错误
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldReportErrorWhenAtPositiveLimit) {
    // 准备：设置轴在正限位
    plc.forceLimit(AxisId::Y, LimitType::Positive, true);
    driver.pollFeedback(*ctx);
    vm->tick();  // 让 ViewModel 识别限位状态
    
    vm->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Idle; }));

    // Act: 尝试正向点动
    vm->jog(Direction::Forward);

    // Assert: 应有错误
    bool hasError = waitUntil([this]() { return vm->hasError(); });
    ASSERT_TRUE(hasError);
    EXPECT_EQ(vm->lastError().code, "AXIS_AT_POSITIVE_LIMIT");
    EXPECT_EQ(vm->lastError().category, ErrorCategory::Inline);
}

// =========================================================
// 新增测试 15: 错误列表收集（多 Orchestrator 并发错误）
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldCollectMultipleErrors) {
    vm->moveAbsolute(100.0);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::MovingAbsolute; }));

    // 在运动中尝试第二个操作
    vm->moveRelative(50.0);  // 会产生 AlreadyMoving 或类似错误

    // Assert: 错误计数应 > 0
    bool errorCollected = waitUntil([this]() { 
        return vm->errorCount() > 0;
    });
    ASSERT_TRUE(errorCollected);

    // 确认错误可遍历
    auto allErrors = vm->allErrors();
    EXPECT_GE(allErrors.size(), 1);
}

// =========================================================
// 新增测试 16: 错误确认功能
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldAcknowledgeErrors) {
    // ... 触发错误 ...
    ASSERT_TRUE(vm->hasError());
    size_t initialCount = vm->errorCount();

    vm->acknowledgeError(0);  // 确认第一条错误
    EXPECT_EQ(vm->errorCount(), initialCount - 1);
}

// =========================================================
// 新增测试 17: disable 不重置错误
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldNotResetErrorsOnDisable) {
    // 触发错误
    // ...
    ASSERT_TRUE(vm->hasError());
    
    // disable 不应清除错误
    vm->disable();
    EXPECT_TRUE(vm->hasError()) << "Disable should not clear existing errors";
}

// =========================================================
// 新增测试 18: zeroAbsolutePosition 错误保护
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldReportErrorOnZeroOpWhenAxisMissing) {
    // 使用一个不存在的 axisId 创建 ViewModel
    AxisViewModelCore badVm(manager, GROUP_NAME, AxisId::Z);  // Z 未注册
    
    badVm.zeroAbsolutePosition();
    badVm.tick();  // 驱动错误收集
    
    EXPECT_TRUE(badVm.hasError());
    EXPECT_EQ(badVm.lastError().code, "CTX_AXIS_NOT_REGISTERED");
}
```

### 8.2 QtAxisViewModel 新增测试

```cpp
// tests/presentation/viewmodel/test_qt_axis_viewmodel.cpp（新增文件）

#include <gtest/gtest.h>
#include <QSignalSpy>
#include "presentation/viewmodel/QtAxisViewModel.h"

class QtAxisViewModelTest : public ::testing::Test {
protected:
    // ... Setup 同 AxisViewModelCoreTest ...
    std::unique_ptr<QtAxisViewModel> qtVm;

    void SetUp() override {
        // ... 标准 setup ...
        auto core = std::make_unique<AxisViewModelCore>(manager, GROUP_NAME, AxisId::Y);
        qtVm = std::make_unique<QtAxisViewModel>(core.get());
        // 注意：core 生命周期需长于 qtVm
    }
};

// =========================================================
// 测试: 信号发射节流
// =========================================================
TEST_F(QtAxisViewModelTest, ShouldEmitStateChangedOnlyOnTransition) {
    QSignalSpy spy(qtVm.get(), &QtAxisViewModel::stateChanged);
    
    // 两次连续 tick 不改变状态
    qtVm->tick();
    qtVm->tick();
    EXPECT_EQ(spy.count(), 0) << "State unchanged should not emit";
}

// =========================================================
// 测试: isEnabled 正确映射
// =========================================================
TEST_F(QtAxisViewModelTest, ShouldReportIsEnabled) {
    EXPECT_FALSE(qtVm->isEnabled());
    
    vm->enable(true);
    // ... 等待 Idle ...
    EXPECT_TRUE(qtVm->isEnabled());
}

// =========================================================
// 测试: stateText 映射
// =========================================================
TEST_F(QtAxisViewModelTest, ShouldMapStateText) {
    EXPECT_EQ(qtVm->stateText(), "未使能");
    // ... enable -> Idle ...
    EXPECT_EQ(qtVm->stateText(), "就绪");
}
```

### 8.3 ErrorTranslator 测试优化（修复 #12）

```cpp
// 使用参数化测试减少冗余代码
// tests/presentation/viewmodel/test_error_translator.cpp

class ErrorTranslatorParamTest : public ::testing::TestWithParam<
    std::tuple<UseCaseError, std::string, ErrorCategory>
> {};

TEST_P(ErrorTranslatorParamTest, ShouldTranslateCorrectly) {
    auto [input, expectedCode, expectedCategory] = GetParam();
    auto result = translate(input);
    EXPECT_EQ(result.code, expectedCode);
    EXPECT_EQ(result.category, expectedCategory);
}

// RejectionReason 参数化
INSTANTIATE_TEST_SUITE_P(
    RejectionReasonTests,
    ErrorTranslatorParamTest,
    ::testing::Values(
        std::make_tuple(UseCaseError{RejectionReason::InvalidState},
                        "AXIS_INVALID_STATE", ErrorCategory::Inline),
        std::make_tuple(UseCaseError{RejectionReason::AlreadyMoving},
                        "AXIS_ALREADY_MOVING", ErrorCategory::Inline),
        // ... 其他分支 ...
    )
);
```

---

## 9. 删除清单

| 文件/代码 | 处理方式 | 原因 |
|-----------|---------|------|
| `AxisViewModelCore::m_lastError` | 替换为 `m_errorHistory` | 错误模型升级 |
| `AxisViewModelCore::m_hasError` | 替换为 `!m_errorHistory.empty()` | 错误模型升级 |
| `executeAndTranslate()` 辅助函数 | 删除 | 新方案中分散到具体操作函数中 |
| `QtAxisViewModel::m_lastHasError` (bool) | 保留但扩展到 `m_lastErrorCount` | 信号节流增强 |
| `TEST_F` 冗余测试 (ErrorTranslator) | 替换为 `TEST_P` 参数化测试 | 减少代码重复 |

---

## 10. 实施路线图

### Phase 1: 错误模型升级（预计 1-2 天）

| 步骤 | 内容 | 产出 |
|------|------|------|
| 1.1 | AxisViewModelCore 新增 `m_errorHistory`、`ErrorEntry` 结构体 | `AxisViewModelCore.h` |
| 1.2 | 实现 `pushError()`, `allErrors()`, `acknowledgeError()`, `clearAllErrors()` | `AxisViewModelCore.cpp` |
| 1.3 | 重写 `tick()` 错误收集为追加模式 | `AxisViewModelCore.cpp` |
| 1.4 | 修复 `enable(false)` 不覆盖错误 | `AxisViewModelCore.cpp` |
| 1.5 | 为 5 个零位/速度操作添加错误保护 | `AxisViewModelCore.cpp` |
| 1.6 | 更新单元测试适配新接口 | `test_axis_viewmodel_core.cpp` |
| 1.7 | 构建和运行测试验证 | -- |

### Phase 2: Q_PROPERTY 补齐 + 日志整合（并行，预计 2-3 天）

| 步骤 | 内容 | 产出 |
|------|------|------|
| 2.1 | QtAxisViewModel 新增 `isEnabled`, `stateText`, `errorCategory`, `errorCount` | `QtAxisViewModel.h/.cpp` |
| 2.2 | QtAxisViewModel::tick() 增加 velocity/limit 节流 | `QtAxisViewModel.cpp` |
| 2.3 | AxisViewModelCore 所有操作入口注入 TraceScope + LOG_INFO | `AxisViewModelCore.cpp` |
| 2.4 | 实现 `generateTraceId()`, `axisIdToString()` 辅助方法 | `AxisViewModelCore.h/.cpp` |
| 2.5 | consumePendingCommands() 增加 CommandFormatter 日志 | `AxisViewModelCore.cpp` |
| 2.6 | ErrorTranslator 增加 DEBUG 日志（每次翻译记录） | `ErrorTranslator.cpp` |
| 2.7 | 新建 `test_qt_axis_viewmodel.cpp` | 测试文件 |
| 2.8 | 构建和运行测试验证 | -- |

### Phase 3: 测试补充 + 清理（预计 1 天）

| 步骤 | 内容 | 产出 |
|------|------|------|
| 3.1 | 新增限位触发错误测试 | `test_axis_viewmodel_core.cpp` |
| 3.2 | 新增错误列表/确认测试 | `test_axis_viewmodel_core.cpp` |
| 3.3 | 新增 disable 不重置错误测试 | `test_axis_viewmodel_core.cpp` |
| 3.4 | 将 ErrorTranslator 测试改为参数化 | `test_error_translator.cpp` |
| 3.5 | 删除 `executeAndTranslate()` 等废弃代码 | `AxisViewModelCore.cpp` |
| 3.6 | 全量构建跑测试 | -- |

---

## 11. 附录：重构后的完整文件清单

### 修改文件

| 文件 | 修改量 | 关键变更 |
|------|--------|---------|
| `presentation/viewmodel/AxisViewModelCore.h` | 🟡 中等 | 新增 ErrorEntry, m_errorHistory, 新增方法声明, TraceScope 辅助方法 |
| `presentation/viewmodel/AxisViewModelCore.cpp` | 🔴 大量 | 全部操作入口日志注入, 零位操作错误保护, tick() 错误收集重写, disable() 修复 |
| `presentation/viewmodel/QtAxisViewModel.h` | 🟡 中等 | 新增 4 个 Q_PROPERTY, 3 个 Q_INVOKABLE, 3 个缓存成员 |
| `presentation/viewmodel/QtAxisViewModel.cpp` | 🟡 中等 | 实现 stateText, errorCategory, tick() 节流补齐 |
| `presentation/viewmodel/ErrorTranslator.h` | 🔵 无变更 | -- |
| `presentation/viewmodel/ErrorTranslator.cpp` | 🔵 无变更 | -- |
| `presentation/viewmodel/ViewModelError.h` | 🔵 无变更 | -- |

### 新增文件

| 文件 | 内容 |
|------|------|
| `tests/presentation/viewmodel/test_qt_axis_viewmodel.cpp` | QtAxisViewModel 信号/节流/属性测试 |

### 无变更文件

| 文件 | 原因 |
|------|------|
| `infrastructure/logger/Logger.h` | 日志基础设施已完备，ViewModel 层只需消费 |
| `infrastructure/logger/LogContext.h` | 结构定义满足需求 |
| `infrastructure/logger/TraceScope.h` | RAII 实现可用 |
| `domain/entity/Axis.h` | 领域层无需为 ViewModel 日志修改 |
| `application/UseCaseError.h` | variant 定义保持不变 |

---

## 附录 A：重构前后对比速查

| 维度 | 重构前 | 重构后 |
|------|--------|--------|
| 错误收集 | 单值 `m_lastError` | 列表 `m_errorHistory` |
| 错误确认 | 全部清除 `clearError()` | 单条确认 `acknowledgeError(i)` + 全部清除 `clearAllErrors()` |
| 零位操作保护 | 无错误检查 | Axis::lastRejection() + pushError |
| disable 行为 | 可能覆盖错误 | 不产生/覆盖错误 |
| Q_PROPERTY 数量 | 10 个 | 14 个 |
| 日志行 | 0 行（全缺失） | ~40 行（全操作入口） |
| TraceScope | 未使用 | 所有 5+ 操作入口创建 |
| 命令日志 | 无 | CommandFormatter 格式化输出 |
| 测试数量 (Core) | 13 个 | 18 个 |
| 测试数量 (Qt) | 0 个 | 5+ 个 |
| ErrorTranslator 测试 | 31 个独立 TEST_F | 参数化 TEST_P + 保留边界测试 |

---

## 附录 B：关键决策记录

### 决策 1：错误列表 vs 错误队列

**问题：** 错误应该用 `std::vector` 还是 `std::queue`？

**决定：** 使用 `std::vector<ErrorEntry>`。

**理由：**
- 用户需要按索引确认单条错误（vector 支持随机访问）
- QML 需要遍历显示全部错误（vector 可转为 QVariantList）
- 错误数量有限（通常 < 10），性能无影响

### 决策 2：日志等级选择

**问题：** 不同操作应该用什么日志等级？

**决定：** 见下表

| 场景 | 等级 | 理由 |
|------|------|------|
| 用户操作入口 | INFO | 用户发起了什么操作，必须可追溯 |
| 操作正常完成 | DEBUG | 正常流程详细信息 |
| 操作被拒绝 (Inline) | WARN | 用户操作被软拒绝，但不严重 |
| 操作失败 (Modal) | ERROR | 严重错误，需要关注 |
| tick 周期性摘要 | TRACE | 高频调试信息，默认不输出 |

### 决策 3：TraceScope 创建时机

**问题：** 所有操作都创建 TraceScope 吗？速度设置这种高频微操作呢？

**决定：** 用户意图触发的操作创建 Scope，内部自动触发的不创建。

```cpp
// 创建 TraceScope:
enable, jog, jogStop, moveAbsolute, moveRelative, stop, 
zeroAbsolutePosition, setRelativeZero, clearRelativeZero

// 不创建 TraceScope:
setJogVelocity, setMoveVelocity, tick （低频，由其他操作带动）（低于操作频率）
```

### 决策 4：CommandFormatter 的使用位置

**问题：** CommandFormatter 在 ViewModel 层使用，还是 Orchestrator 层使用？

**决定：** 两者都用，但 ViewModel 仅记录零位/速度命令（因为运动命令由 Orchestrator 内部下发，其日志应在 Orchestrator 层记录）。

---

*本文档是基于重构后 AxisViewModel 的实际代码缺陷分析和日志基础设施评估制定的系统性重构方案。*
