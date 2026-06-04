# servoV6 UDP 通讯层设计文档

## 1. 概述

### 1.1 文档目的

本文档描述 servoV6 系统对外部提供 UDP 网络控制能力的方案设计，包括 **UDP 服务层架构**和 **命令分发与处理逻辑**。

### 1.2 需求背景

外部系统需要通过 UDP（JSON over UDP）对伺服系统中的 **R 轴（旋转轴）** 进行远程运动控制，支持以下操作：

| 命令码 | 命令名 | 功能 |
|--------|--------|------|
| cmd=0 | MOVE_TO_REL_TARGET | 基于相对零点的绝对位置移动 |
| cmd=1 | MOVE_OFFSET | 相对偏移移动 |
| cmd=2 | GET_REL_POSITION | 获取当前相对位置 |
| cmd=3 | SET_MOVE_SPEED | 设置位置移动速度 |
| cmd=4 | GET_MOVE_SPEED | 获取位置移动速度 |
| cmd=5 | SET_REL_ZERO | 设置相对零点 |

### 1.3 设计约束

1. **仅操作 R 轴**：`motor` 字段必须映射到 `AxisId::R`（motor=2），其他轴拒绝
2. **不使用废弃策略**：不使用 `AutoAbsMoveOrchestrator` 和 `AutoRelMoveOrchestrator`，改用 `AbsMovePolicy` 和 `RelMovePolicy`
3. **遵循现有架构**：所有领域逻辑通过 `Axis` 实体 + `SystemManager` 分组机制完成
4. **JSON over UDP**：每个数据报一个完整 JSON 请求/响应，不分片
5. **分组校验**：`group` 字段对应的组必须在 `SystemManager` 中已注册

---

## 2. 协议格式

### 2.1 请求格式（客户端 → 本系统）

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `cmd` | int | ✅ | 命令码（0~5） |
| `group` | string | ✅ | 目标分组名（如 `"Machine_A"`） |
| `motor` | int | ✅ | 电机 ID（必须为 2，即 R 轴） |
| `target` | double | cmd=0 时必填 | 相对零点偏移目标位置（mm） |
| `offset` | double | cmd=1 时必填 | 相对偏移量（mm），正=正向，负=反向 |
| `speed` | double | cmd=3 时必填 | 位置移动速度（RPM） |

**motor 映射**：

| motor 值 | AxisId | 说明 |
|----------|--------|------|
| 0 | Y | 不支持，拒绝 |
| 1 | Z | 不支持，拒绝 |
| 2 | R | ✅ 唯一支持 |
| 3 | X | 不支持，拒绝 |
| 4 | X1 | 不支持，拒绝 |
| 5 | X2 | 不支持，拒绝 |

### 2.2 回复格式（本系统 → 客户端）

所有回复均为 JSON，**必须包含原始请求的全部字段**，并附加：

| 字段 | 类型 | 说明 |
|------|------|------|
| `result` | int | 1=成功，0=失败 |
| `msg` | string | 失败时的错误描述（成功时不出现或为空） |

不同命令的额外字段见 §3.4。

---

## 3. 架构设计

### 3.1 分层定位

UDP 通讯层由两个新组件构成，位于 infrastructure 层（网络 IO）和 application 层（命令分发）：

```
┌──────────────────────────────────────────────────────────────────┐
│                        presentation                              │
│              (UDP 命令可选由 UI 触发监控/日志)                    │
├──────────────────────────────────────────────────────────────────┤
│                      application                                 │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                   UdpCommandDispatcher                     │  │
│  │  职责：JSON 解析 → 校验(group/R轴) → 路由到对应处理函数    │  │
│  │  依赖：SystemManager, Axis, AbsMovePolicy, RelMovePolicy   │  │
│  └───────────────────────────────────────────────────────────┘  │
├──────────────────────────────────────────────────────────────────┤
│                      infrastructure                              │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                      UdpServer                             │  │
│  │  职责：QUdpSocket 监听 → 收包 → 调 UdpCommandDispatcher    │  │
│  │        → 结果序列化为 JSON → 回包                           │  │
│  │  依赖：UdpCommandDispatcher                                 │  │
│  └───────────────────────────────────────────────────────────┘  │
├──────────────────────────────────────────────────────────────────┤
│                        domain                                    │
│          Axis 实体 / AxisFeedback / 状态机 / 领域规则            │
└──────────────────────────────────────────────────────────────────┘
```

**依赖方向**：`UdpServer`(infrastructure) → `UdpCommandDispatcher`(application) → `SystemManager`/`Axis`/`Policy`(application/domain)

### 3.2 新增文件清单

```
servoV6/
├── infrastructure/
│   └── udp/
│       ├── UdpServer.h              # UDP 服务器（监听 + 收发 + 生命周期）
│       └── UdpServerConfig.h        # 配置（监听端口、绑定地址）
│
├── application/
│   └── udp/
│       ├── UdpCommandDispatcher.h   # 命令分发器（解析 + 校验 + 路由）
│       ├── UdpProtocol.h            # 协议常量定义（cmd 枚举、字段名）
│       └── UdpResponseBuilder.h     # 回复构建工具（JSON 组装）
│
├── tests/
│   └── application/
│       └── udp/
│           ├── test_udp_command_dispatcher.cpp   # 分发器单元测试
│           └── test_udp_server_integration.cpp   # UDP 集成测试
```

### 3.3 组件详细设计

#### 3.3.1 UdpServer（infrastructure/udp/UdpServer.h）

**职责**：UDP 网络 IO 的封装，不关心业务逻辑。

```cpp
class UdpServer {
public:
    struct Config {
        uint16_t listenPort = 9001;          // 监听端口
        std::string bindAddress = "0.0.0.0"; // 绑定地址
        int recvBufferSize = 4096;           // 接收缓冲区大小（字节）
    };

    explicit UdpServer(SystemManager& manager, const Config& cfg);

    /// 启动监听
    bool start();

    /// 停止监听
    void stop();

    /// 逐帧驱动：处理所有待处理数据报
    /// 调用 UdpCommandDispatcher 处理每个数据报并回包
    void tick();
};
```

**核心流程**：

```
UdpServer::tick()
  ├── while (socket.hasPendingDatagrams())
  │     ├── socket.readDatagram(data, &senderAddr, &senderPort)
  │     ├── dispatchAndReply(data, senderAddr, senderPort)
  │     │     ├── m_dispatcher.dispatch(jsonStr)
  │     │     │     → 返回 JSON 回复字符串
  │     │     └── socket.writeDatagram(replyJson, senderAddr, senderPort)
  │     └── end while
  └── return
```

**设计要点**：
- 使用 Qt 的 `QUdpSocket`，非阻塞模式
- 由外部 tick 循环驱动（与现有 FakePLC/AxisSyncService 的 tick 驱动模型一致）
- 不解析 JSON，仅透传原始字节给 Dispatcher
- 不记录日志（日志由 Dispatcher 层负责）

#### 3.3.2 UdpProtocol（application/udp/UdpProtocol.h）

**职责**：协议常量与字段名定义，避免字符串硬编码。

```cpp
// === 命令码枚举 ===
enum class UdpCmd : int {
    MOVE_TO_REL_TARGET = 0,  // 基于相对零点的绝对位置移动
    MOVE_OFFSET        = 1,  // 相对偏移移动
    GET_REL_POSITION   = 2,  // 获取当前相对位置
    SET_MOVE_SPEED     = 3,  // 设置位置移动速度
    GET_MOVE_SPEED     = 4,  // 获取位置移动速度
    SET_REL_ZERO       = 5,  // 设置相对零点
};

// === JSON 字段名常量 ===
namespace UdpField {
    constexpr const char* CMD    = "cmd";
    constexpr const char* GROUP  = "group";
    constexpr const char* MOTOR  = "motor";
    constexpr const char* TARGET = "target";
    constexpr const char* OFFSET = "offset";
    constexpr const char* SPEED  = "speed";
    constexpr const char* RESULT = "result";
    constexpr const char* MSG    = "msg";
    constexpr const char* CURR   = "curr";   // 当前相对位置
}

// === R 轴 motor 值 ===
constexpr int R_MOTOR_ID = 2;

/// motor 值 → AxisId 转换（仅 R 轴通过）
inline bool motorToAxisId(int motor, AxisId& outId) {
    if (motor == R_MOTOR_ID) {
        outId = AxisId::R;
        return true;
    }
    return false;
}
```

#### 3.3.3 UdpCommandDispatcher（application/udp/UdpCommandDispatcher.h）

**职责**：命令解析、校验、路由、执行和回复生成的编排中心。

```
请求生命周期：

  UDP 数据报到达
       │
       ▼
  ┌──────────────┐
  │ 1. JSON 解析  │ ← 失败的 JSON → 立即返回 {"result":0,"msg":"invalid json"}
  └──────┬───────┘
         │
         ▼
  ┌──────────────┐
  │ 2. 字段提取   │ ← 缺少 cmd/motor → 返回 {"result":0,"msg":"missing 'cmd'"}
  └──────┬───────┘
         │
         ▼
  ┌──────────────────┐
  │ 3. motor 校验     │ ← motor≠2 → 返回 {"result":0,"msg":"motor must be 2 (R axis)"}
  └──────┬───────────┘
         │
         ▼
  ┌──────────────────┐
  │ 4. group 校验     │ ← SystemManager::tryGetGroup 失败 → 返回错误
  └──────┬───────────┘
         │
         ▼
  ┌──────────────────┐
  │ 5. Axis 获取      │ ← group->tryGetAxis(R, ...) 失败 → 返回错误
  └──────┬───────────┘
         │
         ▼
  ┌──────────────────┐
  │ 6. 按 cmd 路由    │
  │   到具体处理函数  │
  └──────┬───────────┘
         │
         ▼
  ┌──────────────────┐
  │ 7. 构建 JSON 回复 │
  └──────┬───────────┘
         │
         ▼
    返回回复字符串
```

**类定义**：

```cpp
class UdpCommandDispatcher {
public:
    explicit UdpCommandDispatcher(SystemManager& manager);

    /// @brief 处理一条 UDP 命令并返回 JSON 回复
    /// @param rawJson  原始 JSON 字符串
    /// @return JSON 格式的回复字符串
    std::string dispatch(const std::string& rawJson);

private:
    SystemManager& m_manager;

    // 各命令处理函数
    std::string handleMoveToRelTarget(const QJsonObject& req, Axis& axis, SystemContext& group);
    std::string handleMoveOffset(const QJsonObject& req, Axis& axis, SystemContext& group);
    std::string handleGetRelPosition(const QJsonObject& req, Axis& axis);
    std::string handleSetMoveSpeed(const QJsonObject& req, Axis& axis, SystemContext& group);
    std::string handleGetMoveSpeed(const QJsonObject& req, Axis& axis);
    std::string handleSetRelZero(const QJsonObject& req, Axis& axis, SystemContext& group);
};
```

### 3.4 各命令处理逻辑详述

#### 3.4.0 通用前置校验（所有命令共用）

```
dispatch(rawJson)
  1. JSON 解析
     QJsonDocument::fromJson(rawJson) 解析失败
     → buildErrorReply(原始请求字段, "invalid JSON format")

  2. 必填字段检查
     缺少 "cmd" → buildErrorReply(..., "missing required field 'cmd'")
     缺少 "motor" → buildErrorReply(..., "missing required field 'motor'")
     缺少 "group" → buildErrorReply(..., "missing required field 'group'")

  3. motor 校验
     cmdInt = obj["motor"].toInt()
     if (cmdInt != R_MOTOR_ID)
         → buildErrorReply(..., "motor " + motor + " not supported, only R axis (motor=2)")

  4. group 校验
     groupName = obj["group"].toString()
     if (!m_manager.tryGetGroup(groupName, group, reason))
         → buildErrorReply(..., "group '" + groupName + "' not found")

  5. Axis 获取
     if (!group->tryGetAxis(AxisId::R, axis, reason))
         → buildErrorReply(..., "R axis not available: " + reason描述)
```

#### 3.4.1 MOVE_TO_REL_TARGET（cmd=0）

**触发条件**：`cmd=0`, `motor=2`, `target` 字段存在且为数值。

**业务逻辑**：

```cpp
std::string handleMoveToRelTarget(const QJsonObject& req, Axis& axis, SystemContext& group) {
    double target = req["target"].toDouble();

    // 1. 计算最终绝对目标位置
    //    finalAbsTarget = target + relZeroAbsPos
    //    含义：相对于当前相对零点的偏移 target → 映射为绝对空间中的坐标
    double relZeroAbsPos = axis.relativeZeroAbsolutePosition();
    double finalAbsTarget = target + relZeroAbsPos;

    // 2. 通过 Axis 实体写入 ABS_TARGET 到 PLC
    //    使用 setAbsTarget(finalAbsTarget) 写入 PLC D 寄存器
    if (!axis.setAbsTarget(finalAbsTarget)) {
        RejectionReason reason = axis.lastRejection();
        std::string errMsg = rejectionReasonToString(reason);
        LOG_WARN(LogLayer::APP, "UdpDispatcher",
                 "[MOVE_TO_REL_TARGET] setAbsTarget("
                 + std::to_string(finalAbsTarget) + ") rejected: " + errMsg);
        return buildErrorReply(req, "setAbsTarget failed: " + errMsg);
    }

    // 3. 通过 IAxisDriver 消费 pending command（将 SetAbsTargetCommand 下发到 PLC）
    if (axis.hasPendingCommand()) {
        if (auto* drv = group.driver()) {
            auto commResult = drv->send(AxisCommandWithId{AxisId::R, axis.getPendingCommand()});
            if (!commResult.ok()) {
                return buildErrorReply(req, "PLC communication failed when writing target");
            }
        }
    }

    // 4. 触发绝对位置移动（使用 AbsMovePolicy）
    AbsMovePolicy absPolicy(m_manager, groupName);
    absPolicy.startAbs(AxisId::R);

    // 5. 驱动 Policy 状态机直到完成或出错
    while (absPolicy.currentStep() != AbsMovePolicy::Step::Done &&
           absPolicy.currentStep() != AbsMovePolicy::Step::Error) {
        absPolicy.tick();
    }

    // 6. 获取结果
    if (absPolicy.hasError()) {
        auto err = absPolicy.lastError();
        return buildErrorReply(req, "AbsMovePolicy failed: " + useCaseErrorToString(err));
    }

    // 7. 构建成功回复
    double currPos = axis.currentRelativePosition();
    return buildSuccessReply(req, {{"curr", currPos}});
}
```

**地址计算示例**：

```
初始状态：
  AxisFeedback.absPos = 50
  AxisFeedback.relPos = 0
  AxisFeedback.relZeroAbsPos = 50

收到 cmd=0, target=100：
  finalAbsTarget = 100 + 50 = 150
  → setAbsTarget(150) → DMA 写入 ABS_TARGET D 寄存器
  → AbsMovePolicy.startAbs(R) → triggerAbsMove
  → 运动完成后：
    AxisFeedback.absPos = 150
    AxisFeedback.relPos = 100
    回复：{"cmd":0,"group":"Machine_A","motor":2,"target":100,"result":1,"curr":100}
```

**关键设计决策**：

- **阻塞等待 vs 异步执行**：此处 MOVE_TO_REL_TARGET 采用阻塞等待模式（`while tick`），因为：
  1. UDP 单包请求-响应模型，客户端发送一条命令后期望等运动完成再回包
  2. AbsMovePolicy 的 tick 是无阻塞的（每帧一次状态推进），阻塞时间取决于电机运动时间
  3. 运动期间不影响 UdpServer 的事件循环（tick 内无事件处理需要 yield）

- **备选方案**：若后续需要异步非阻塞 UDP 执行，可在 `UdpCommandDispatcher` 中维护活跃的 Policy 实例 map，由 `UdpServer::tick()` 每帧驱动，运动完成后回传结果。当前采用阻塞模式以匹配客户端同步调用语义。

#### 3.4.2 MOVE_OFFSET（cmd=1）

**触发条件**：`cmd=1`, `motor=2`, `offset` 字段存在且为数值。

**业务逻辑**：

```cpp
std::string handleMoveOffset(const QJsonObject& req, Axis& axis, SystemContext& group) {
    double offset = req["offset"].toDouble();

    // 1. 通过 Axis 实体设置相对移动距离
    if (!axis.setRelTarget(offset)) {
        RejectionReason reason = axis.lastRejection();
        return buildErrorReply(req, "setRelTarget(" + std::to_string(offset)
                               + ") rejected: " + rejectionReasonToString(reason));
    }

    // 2. 消费 pending command → 将 SetRelTargetCommand 下发到 PLC
    if (axis.hasPendingCommand()) {
        if (auto* drv = group.driver()) {
            auto commResult = drv->send(AxisCommandWithId{AxisId::R, axis.getPendingCommand()});
            if (!commResult.ok()) {
                return buildErrorReply(req, "PLC communication failed when writing rel target");
            }
        }
    }

    // 3. 触发相对位置移动（使用 RelMovePolicy）
    RelMovePolicy relPolicy(m_manager, groupName);
    relPolicy.startRel(AxisId::R);

    // 4. 驱动 Policy 直到完成
    while (relPolicy.currentStep() != RelMovePolicy::Step::Done &&
           relPolicy.currentStep() != RelMovePolicy::Step::Error) {
        relPolicy.tick();
    }

    // 5. 结果处理
    if (relPolicy.hasError()) {
        return buildErrorReply(req, "RelMovePolicy failed: "
                               + useCaseErrorToString(relPolicy.lastError()));
    }

    double currPos = axis.currentRelativePosition();
    return buildSuccessReply(req, {{"curr", currPos}});
}
```

#### 3.4.3 GET_REL_POSITION（cmd=2）

**触发条件**：`cmd=2`, `motor=2`。

**业务逻辑**：

```cpp
std::string handleGetRelPosition(const QJsonObject& req, Axis& axis) {
    // 1. 直接查询 Axis 实体的相对位置
    double currPos = axis.currentRelativePosition();

    // 2. 构建回复（无需等待，瞬时完成）
    return buildSuccessReply(req, {{"curr", currPos}});
}
```

**说明**：纯查询操作，不触发任何运动或状态变更。值来自 `AxisFeedback.relPos`（由 `applyFeedback` 持续更新）。

#### 3.4.4 SET_MOVE_SPEED（cmd=3）

**触发条件**：`cmd=3`, `motor=2`, `speed` 字段存在且为数值。

**业务逻辑**：

```cpp
std::string handleSetMoveSpeed(const QJsonObject& req, Axis& axis, SystemContext& group) {
    double speed = req["speed"].toDouble();

    // 1. 通过 Axis 实体设置速度
    if (!axis.setMoveVelocity(speed)) {
        return buildErrorReply(req, "setMoveVelocity(" + std::to_string(speed)
                               + ") rejected");
    }

    // 2. 消费 pending command → 将 SetMoveVelocityCommand 下发到 PLC
    if (axis.hasPendingCommand()) {
        if (auto* drv = group.driver()) {
            auto commResult = drv->send(AxisCommandWithId{AxisId::R, axis.getPendingCommand()});
            if (!commResult.ok()) {
                return buildErrorReply(req, "PLC communication failed when setting speed");
            }
        }
    }

    // 3. 成功回复（无额外字段）
    return buildSuccessReply(req, {});
}
```

**说明**：`setMoveVelocity()` 是对应于 Axis 的 `setMoveVelocity()` 调用，内部生成 `SetMoveVelocityCommand` 并通过 driver 下发。速度值写入后即时生效，影响后续所有绝对/相对定位移动。

#### 3.4.5 GET_MOVE_SPEED（cmd=4）

**触发条件**：`cmd=4`, `motor=2`。

**业务逻辑**：

```cpp
std::string handleGetMoveSpeed(const QJsonObject& req, Axis& axis) {
    // 1. 查询 Axis 实体当前速度设置
    double speed = axis.getMoveVelocity();

    // 2. 构建回复，附带 speed 字段
    return buildSuccessReply(req, {{"speed", speed}});
}
```

**说明**：`getMoveVelocity()` 从 `AxisFeedback.getMoveVelocity` 获取，该值为 PLC 反馈镜像。无需下发命令。

#### 3.4.6 SET_REL_ZERO（cmd=5）

**触发条件**：`cmd=5`, `motor=2`。

**业务逻辑**：

```cpp
std::string handleSetRelZero(const QJsonObject& req, Axis& axis, SystemContext& group) {
    // 1. 调用 setRelativeZero() 设置当前绝对位置为相对零点
    if (!axis.setRelativeZero()) {
        return buildErrorReply(req, "setRelativeZero rejected: "
                               + rejectionReasonToString(axis.lastRejection()));
    }

    // 2. 消费 pending command → 将 SetRelativeZeroCommand 下发到 PLC
    if (axis.hasPendingCommand()) {
        if (auto* drv = group.driver()) {
            auto commResult = drv->send(AxisCommandWithId{AxisId::R, axis.getPendingCommand()});
            if (!commResult.ok()) {
                return buildErrorReply(req, "PLC communication failed when setting rel zero");
            }
        }
    }

    // 3. 成功回复（无额外字段）
    return buildSuccessReply(req, {});
}
```

**说明**：`setRelativeZero()` 将当前轴绝对位置记录为 `relZeroAbsPos`，并将 `relPos` 清零。后续 `MOVE_TO_REL_TARGET` 命令基于此零点计算。

### 3.5 UdpResponseBuilder（application/udp/UdpResponseBuilder.h）

**职责**：统一构建 JSON 回复，保证格式一致性。

```cpp
class UdpResponseBuilder {
public:
    /// @brief 构建成功回复
    /// @param request 原始请求 JSON 对象（用于回显请求字段）
    /// @param extraFields 额外字段 map（如 {"curr": 498.2}）
    /// @return JSON 字符串
    static std::string buildSuccess(const QJsonObject& request,
                                     const std::map<std::string, QJsonValue>& extraFields = {});

    /// @brief 构建失败回复
    /// @param request 原始请求 JSON 对象
    /// @param errorMsg 错误描述
    /// @return JSON 字符串
    static std::string buildError(const QJsonObject& request,
                                   const std::string& errorMsg);
};
```

**实现逻辑**：

```cpp
static std::string buildSuccess(const QJsonObject& req,
                                 const std::map<std::string, QJsonValue>& extra) {
    QJsonObject reply = req;  // 复制所有请求字段（cmd, group, motor, target, ...）
    reply["result"] = 1;
    for (const auto& [key, val] : extra) {
        reply[QString::fromStdString(key)] = val;
    }
    return QJsonDocument(reply).toJson(QJsonDocument::Compact).toStdString();
}

static std::string buildError(const QJsonObject& req,
                               const std::string& errorMsg) {
    QJsonObject reply = req;
    reply["result"] = 0;
    reply["msg"] = QString::fromStdString(errorMsg);
    return QJsonDocument(reply).toJson(QJsonDocument::Compact).toStdString();
}
```

**注意**：当 JSON 解析失败无法获得 `QJsonObject` 时，需要回退到最小错误回复：

```cpp
// 无法解析原始请求字段时的最小错误回复
static std::string buildRawError(int cmd, int motor, const std::string& msg) {
    QJsonObject reply;
    reply["cmd"] = cmd;
    reply["motor"] = motor;
    reply["result"] = 0;
    reply["msg"] = QString::fromStdString(msg);
    return QJsonDocument(reply).toJson(QJsonDocument::Compact).toStdString();
}
```

### 3.6 reply 格式汇总

| cmd | 成功 reply 示例 | 失败 reply 示例 |
|-----|----------------|----------------|
| 0 | `{"cmd":0,"group":"Machine_A","motor":2,"target":500,"result":1,"curr":498.2}` | `{"cmd":0,"group":"...","motor":2,"target":500,"result":0,"msg":"AbsMovePolicy failed: ..."}` |
| 1 | `{"cmd":1,"group":"Machine_A","motor":2,"offset":-100,"result":1,"curr":398.2}` | 同上模式 |
| 2 | `{"cmd":2,"group":"Machine_A","motor":2,"result":1,"curr":498.2}` | 同上模式 |
| 3 | `{"cmd":3,"group":"Machine_A","motor":2,"speed":15.0,"result":1}` | 同上模式 |
| 4 | `{"cmd":4,"group":"Machine_A","motor":2,"result":1,"speed":15.0}` | 同上模式 |
| 5 | `{"cmd":5,"group":"Machine_A","motor":2,"result":1}` | 同上模式 |

---

## 4. 复用现有组件清单

| 现有组件 | 路径 | 复用方式 |
|----------|------|---------|
| `SystemManager` | `application/SystemManager.h` | 直接调用 `tryGetGroup()` |
| `SystemContext::tryGetAxis()` | `domain/entity/SystemContext.h` | 获取 R 轴引用 |
| `Axis::setAbsTarget()` | `domain/entity/Axis.h` | cmd=0 写入 ABS_TARGET D 寄存器 |
| `Axis::triggerAbsMove()` | `domain/entity/Axis.h` | cmd=0 → 通过 AbsMovePolicy 间接调用 |
| `Axis::setRelTarget()` | `domain/entity/Axis.h` | cmd=1 写入 REL_TARGET D 寄存器 |
| `Axis::triggerRelMove()` | `domain/entity/Axis.h` | cmd=1 → 通过 RelMovePolicy 间接调用 |
| `Axis::currentRelativePosition()` | `domain/entity/Axis.h` | cmd=2 查询 & cmd=0/1 回复 |
| `Axis::setMoveVelocity()` | `domain/entity/Axis.h` | cmd=3 设置速度 |
| `Axis::getMoveVelocity()` | `domain/entity/Axis.h` | cmd=4 查询速度 |
| `Axis::setRelativeZero()` | `domain/entity/Axis.h` | cmd=5 设置零点 |
| `Axis::relativeZeroAbsolutePosition()` | `domain/entity/Axis.h` | cmd=0 计算 finalAbsTarget |
| `AbsMovePolicy` | `application/policy/AbsMovePolicy.h` | cmd=0 触发绝对移动编排 |
| `RelMovePolicy` | `application/policy/RelMovePolicy.h` | cmd=1 触发相对移动编排 |
| `IAxisDriver::send()` | `application/axis/IAxisDriver.h` | 消费 pending command |
| `UseCaseError` | `application/UseCaseError.h` | 错误类型统一表示 |
| `TraceScope / Logger` | `infrastructure/logger/` | 日志记录 |

**不复用组件**：

| 废弃组件 | 原因 |
|----------|------|
| `AutoAbsMoveOrchestrator` | 已弃用，被 `AbsMovePolicy` 替代 |
| `AutoRelMoveOrchestrator` | 已弃用，被 `RelMovePolicy` 替代 |
| `MoveAbsoluteUseCase` | 旧式单体命令（target + trigger 合一），已演变为 setAbsTarget + TriggerAbsMoveUseCase |
| `MoveRelativeUseCase` | 同上，已演变为 setRelTarget + TriggerRelMoveUseCase |

---

## 5. 错误处理矩阵

| 错误场景 | 错误码 | 哪个步骤返回 | 示例 msg |
|----------|--------|-------------|---------|
| JSON 解析失败 | result=0 | §3.4.0 步骤1 | `"invalid JSON format"` |
| 缺少 cmd | result=0 | §3.4.0 步骤2 | `"missing required field 'cmd'"` |
| 缺少 motor | result=0 | §3.4.0 步骤2 | `"missing required field 'motor'"` |
| 缺少 group | result=0 | §3.4.0 步骤2 | `"missing required field 'group'"` |
| motor≠2 | result=0 | §3.4.0 步骤3 | `"motor 0 not supported, only R axis (motor=2)"` |
| group 不存在 | result=0 | §3.4.0 步骤4 | `"group 'NonExistent' not found"` |
| R 轴未在 group 注册 | result=0 | §3.4.0 步骤5 | `"R axis not available: AxisNotRegistered"` |
| 缺少 target（cmd=0） | result=0 | §3.4.1 | `"missing required field 'target' for cmd=0"` |
| 缺少 offset（cmd=1） | result=0 | §3.4.2 | `"missing required field 'offset' for cmd=1"` |
| 缺少 speed（cmd=3） | result=0 | §3.4.4 | `"missing required field 'speed' for cmd=3"` |
| setAbsTarget 被领域层拒绝（限位/状态） | result=0 | §3.4.1 步骤2 | `"setAbsTarget failed: TargetOutOfPositiveLimit"` |
| setRelTarget 被领域层拒绝 | result=0 | §3.4.2 步骤1 | `"setRelTarget failed: InvalidState"` |
| AbsMovePolicy 执行失败（超时/轴报错） | result=0 | §3.4.1 步骤6 | `"AbsMovePolicy failed: timeout in EnsuringEnabled"` |
| RelMovePolicy 执行失败 | result=0 | §3.4.2 步骤5 | `"RelMovePolicy failed: Axis Error state"` |
| PLC 通讯失败（driver.send 返回失败） | result=0 | §3.4.1 步骤3 | `"PLC communication failed when writing target"` |
| unknown cmd | result=0 | dispatch() | `"unknown cmd: 99"` |

---

## 6. 集成方式

### 6.1 在 main.cpp 中启动

```cpp
// main.cpp 中在 SystemManager 创建完毕后：

SystemManager manager;
// ... 创建分组、注册轴、绑定驱动 ...

UdpServer::Config udpCfg;
udpCfg.listenPort = 9001;
udpCfg.bindAddress = "0.0.0.0";

UdpServer udpServer(manager, udpCfg);
udpServer.start();

// 在主循环的 tick 驱动中（与 FakePLC tick、AxisSyncService tick 并行）：
while (running) {
    fakePlc.tick(16);           // 物理仿真
    axisSyncService.tick();     // 反馈同步
    udpServer.tick();           // UDP 消息处理
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
}
```

### 6.2 CMakeLists.txt 变更

```cmake
# application/CMakeLists.txt 追加：
add_library(udp_lib STATIC
    udp/UdpCommandDispatcher.h
    udp/UdpProtocol.h
    udp/UdpResponseBuilder.h
)
target_link_libraries(udp_lib PUBLIC domain_lib application_axis_lib)

# infrastructure/CMakeLists.txt 追加：
add_library(infra_udp_lib STATIC
    udp/UdpServer.h
)
target_link_libraries(infra_udp_lib PUBLIC udp_lib Qt6::Network)
```

---

## 7. 测试策略

### 7.1 单元测试：UdpCommandDispatcher

| 测试用例 | 输入 | 预期输出 |
|----------|------|---------|
| JSON 解析失败 | `"not json"` | result=0, msg="invalid JSON format" |
| 缺少 cmd | `{"motor":2}` | result=0, msg="missing required field 'cmd'" |
| 缺少 motor | `{"cmd":0}` | result=0, msg="missing required field 'motor'" |
| motor 不是 R | `{"cmd":0,"motor":0}` | result=0, msg="motor 0 not supported..." |
| group 不存在 | `{"cmd":2,"motor":2,"group":"X"}` | result=0, msg="group 'X' not found" |
| cmd=2 查询位置 | `{"cmd":2,"motor":2,"group":"Machine_A"}` | result=1, curr="axis.currentRelativePosition()" |
| cmd=4 查询速度 | `{"cmd":4,"motor":2,"group":"Machine_A"}` | result=1, speed="axis.getMoveVelocity()" |
| cmd=5 设置零点 | `{"cmd":5,"motor":2,"group":"Machine_A"}` | result=1 (若成功) |
| cmd=3 设置速度 | `{"cmd":3,"motor":2,"group":"Machine_A","speed":25.0}` | result=1 (若成功) |
| cmd=99 未知命令 | `{"cmd":99,"motor":2,"group":"Machine_A"}` | result=0, msg="unknown cmd: 99" |
| cmd=0 缺 target | `{"cmd":0,"motor":2,"group":"Machine_A"}` | result=0, msg="missing required field 'target'..." |
| cmd=1 缺 offset | `{"cmd":1,"motor":2,"group":"Machine_A"}` | result=0, msg="missing required field 'offset'..." |

### 7.2 集成测试：UdpServer + Dispatcher + FakePLC

- 启动 FakePLC + FakeAxisDriver + SystemManager（含 R 轴）
- 启动 UdpServer 在随机端口
- 通过 UDP socket 发送 cmd=2（GET_REL_POSITION）
- 验证回复 JSON 中 `curr` 值与 FakePLC 位置一致
- 发送 cmd=0 带 target → 验证 FakePLC 中轴运动到目标位置后回复

---

## 8. 工作量估算

| 阶段 | 工作项 | 预估工期 | 说明 |
|------|--------|---------|------|
| **阶段 1：基础框架** | | **1.0 人天** | |
| | `UdpProtocol.h` — 协议常量定义 | 0.15 天 | 枚举 + 字段名常量 + motor 映射，纯声明 |
| | `UdpResponseBuilder.h` — 回复构建工具 | 0.25 天 | JSON 组装逻辑，含 success/error 两类 |
| | `UdpServer.h` — UDP socket 封装 | 0.6 天 | QUdpSocket 监听 + tick 驱动 + 收发循环 |
| **阶段 2：命令分发** | | **2.0 人天** | |
| | `UdpCommandDispatcher.h` — 前置校验逻辑 | 0.5 天 | JSON 解析 + 字段校验 + motor/group/axis 校验 |
| | `UdpCommandDispatcher` — cmd=2,4,5（查询/设置类） | 0.5 天 | GET_REL_POSITION, GET_MOVE_SPEED, SET_REL_ZERO（无 Policy 依赖） |
| | `UdpCommandDispatcher` — cmd=3（SET_MOVE_SPEED） | 0.3 天 | setMoveVelocity + pending command 消费 |
| | `UdpCommandDispatcher` — cmd=1（MOVE_OFFSET） | 0.35 天 | setRelTarget + RelMovePolicy 编排 |
| | `UdpCommandDispatcher` — cmd=0（MOVE_TO_REL_TARGET） | 0.35 天 | relZeroAbsPos 计算 + setAbsTarget + AbsMovePolicy 编排 |
| **阶段 3：测试** | | **1.5 人天** | |
| | `test_udp_command_dispatcher.cpp` — 单元测试 | 0.8 天 | 覆盖所有错误分支 + 正常路径（~15 test cases） |
| | `test_udp_server_integration.cpp` — 集成测试 | 0.7 天 | UdpServer + FakePLC + FakeAxisDriver 端到端测试 |
| **阶段 4：集成与文档** | | **0.5 人天** | |
| | main.cpp 集成（UdpServer 启动 + tick 循环） | 0.25 天 | 在现有初始化流程中插入 UdpServer |
| | CMakeLists.txt 变更 | 0.1 天 | 新增 source_group + target_link |
| | 编译验证 + 冒烟测试 | 0.15 天 | 确保各平台编译通过 |
| **总计** | | **5.0 人天** | |

### 复杂度说明

- **UdpServer 和 UdpResponseBuilder** 相对独立，逻辑简单，属标准样板代码
- **UdpCommandDispatcher** 中 cmd=0 和 cmd=1 涉及与 AbsMovePolicy/RelMovePolicy 的编排对接，需理解 Policy tick 循环语义（阻塞等待时 tick 无其他副作用）
- **单元测试** 需要 mock SystemManager + SystemContext + Axis，需熟悉现有测试基础设施（FakePLC + FakeAxisDriver）
- 总计 **5 人天**，若需异步非阻塞模式（备选方案 §3.4.1），需额外 **+1.5 人天**（Policy 实例生命周期管理 + 异步回调）

---

## 9. 设计决策记录

| 决策点 | 选型 | 理由 |
|--------|------|------|
| UDP 处理模式 | 同步阻塞（while tick） | 匹配 UDP 单包请求-响应模型，简单可靠 |
| JSON 库 | QJsonDocument (Qt6) | 项目已依赖 Qt6，无需引入新库 |
| Policy 选择 | AbsMovePolicy / RelMovePolicy | 用户指定，放弃废弃的 Auto*Orchestrator |
| R 轴硬编码 | motor=2 → AxisId::R，其余拒绝 | 需求明确只操作 R 轴，简化安全校验 |
| 错误类型统一 | UseCaseError variant | 复用现有错误聚合，避免引入新错误类型 |
| 日志层 | LogLayer::APP | UDP 命令为应用层行为 |
| UDP 端口可配置 | 构造函数传 Config 结构体 | 避免端口硬编码，支持多实例 |

---

## 10. 扩展性考量

### 10.1 未来可能扩展

1. **多轴支持**：将 motor 校验从硬编码改为扩展映射表，支持 Y/Z/X 轴
2. **异步非阻塞模式**：为 UdpCommandDispatcher 添加 `tick()` 方法，内部维护活跃 Policy map，运动完成时自动回包
3. **命令队列**：若需要并发处理多条 UDP 命令，可改为命令队列 + Worker 线程模式
4. **速率限制**：添加 per-IP 或全局命令频率限制，防止 UDP 洪水攻击
5. **认证机制**：可选地添加 token/checksum 校验
6. **UDP 广播发现**：支持设备发现协议（如周期性广播设备信息）

### 10.2 扩展点设计

- `UdpCommandDispatcher` 的命令路由使用 `switch(cmd)`，新增命令只需添加 case 和处理函数
- `UdpResponseBuilder` 的 `extraFields` 支持任意扩展字段，无需修改基类
- `UdpServer::Config` 可按需扩展（如白名单、超时等）