# 安卓后台断网导致 Modbus TCP 断连 —— 连接状态监控与手动重连机制缺失分析

> 文档版本: v1.0  
> 创建日期: 2026-06-09  
> 关联需求:
> 1. 界面没有任何指示 PLC 网络是否正确
> 2. 断线后无法通过操作界面重新连接，只能杀死进程重新启动

---

## 1. 问题概述

程序编译部署到安卓系统后，在后台运行期间，由于网络断开（WiFi 断连、路由器重启、PLC 断电等），导致与 PLC 的 Modbus TCP 连接断开。当前存在两个互相关联的问题：

| 编号 | 问题 | 影响 |
|------|------|------|
| P1 | 界面没有任何指示 PLC 网络是否正常的 UI 元素 | 操作员无法判断当前是"PLC 离线"还是"设备故障" |
| P2 | 断线后无法通过界面操作重新连接 | 网络恢复后仍需杀死进程重启，工业现场不可接受 |

---

## 2. 当前架构分析

### 2.1 Modbus TCP 通讯链路

```
┌─────────────┐    ┌──────────────────┐    ┌─────────────────────┐
│   main.cpp   │───>│ AsioModbusTcp    │───>│  PLC (192.168.1.88) │
│  (主线程)     │    │ Client (io线程)   │    │  :502               │
└──────┬───────┘    └────────┬─────────┘    └─────────────────────┘
       │                     │
       │  setModbusClient()  │  m_connected (std::atomic<bool>)
       ▼                     │  start() / stop() / isConnected()
┌──────────────────┐         │
│ ModbusSystem     │◄────────┘
│ Driver           │
│  send()          │──► PlcDevice ──► IModbusClient (写)
│  pollFeedback()  │──► PlcPoller ──► IModbusClient (读)
└────────┬─────────┘
         │ ISystemDriver
         ▼
┌──────────────────┐
│ SystemContext    │──► Axis::applyFeedback()
│  (领域层)        │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ AxisViewModel    │──► Q_PROPERTY → QML UI
│ Core / QtAdapter │
└──────────────────┘
```

### 2.2 连接生命周期管理（现状）

`AsioModbusTcpClient` **已具备自动重连机制**：

```cpp
// AsioModbusTcpClient.h:60
uint32_t reconnectIntervalMs = 2000;  // 断线重连间隔 2 秒

// AsioModbusTcpClient.cpp 流程:
start()
  └─► m_worker = std::thread([this]() {
        startReconnect();           // 发起首次连接
        m_ioctx.run();              // 进入事件循环
      });

startReconnect()
  └─► DNS 解析 → async_connect()
        ├─ 成功: configureSocket() + m_connected = true
        └─ 失败: scheduleReconnect()  // 2秒后重试

// 任何 I/O 错误（超时/断连）:
m_connected.store(false);  // 标记断连
// → 自动触发 scheduleReconnect()
```

### 2.3 连接状态在 pollFeedback 中的处理（现状）

```cpp
// ModbusSystemDriver.h:616 — pollFeedback()
inline void ModbusSystemDriver::pollFeedback(SystemContext& ctx) {
    // 1. 没有检查连接状态！
    // 2. 只要 m_modbusClient 非空就继续轮询
    const auto req = m_poller->prepare();

    // 3. 逐个发起 Modbus 读取——断连时全部返回 Disconnected
    for (const auto& cr : req.coilRequests) {
        CommunicationResult result = m_modbusClient->readCoils(...);
        if (result.ok()) { coilResponses.push_back(...); }
        else { allCoilsOk = false; coilResponses.push_back({}); }
    }
    // ... Word 读取同理

    // 4. 任意读取失败 → 创建 untrusted 快照（数据不可信）
    if (!allCoilsOk || !allWordsOk) {
        auto untrusted = protocol::PlcPoller::untrusted(timestamp);
        if (m_device) { m_device->updateSnapshot(std::move(untrusted)); }
        return;  // ← 不更新 Axis 反馈，保留上次已知状态
    }

    // 5. 全部成功 → 更新 AxisFeedback → Axis::applyFeedback()
    // ...
}
```

### 2.4 问题根因分析

#### P1 根因：连接状态信息断流

`AsioModbusTcpClient::isConnected()` 返回 `std::atomic<bool>`，但此信息**完全未向上层传递**：

| 层级 | 组件 | 是否感知连接状态 | 说明 |
|------|------|:---:|------|
| 基础设施层 | `AsioModbusTcpClient` | ✅ | `m_connected` atomic flag |
| 基础设施层 | `ModbusSystemDriver` | ❌ | 不读取 `isConnected()`，不存储不传播 |
| 基础设施层 | `PlcDevice` | ❌ | `isStateTrusted()` 只表达快照可信度，不表达连接状态 |
| 应用层 | `ISystemDriver` | ❌ | 接口无连接状态查询方法 |
| 表现层 | `AxisViewModelCore` | ❌ | 无任何网络状态属性 |
| 表现层 | `QtAxisViewModel` | ❌ | 无 `Q_PROPERTY` 暴露连接状态 |
| 表现层 | QML UI | ❌ | 无任何连接状态指示器 |

#### P2 根因：重连触发路径缺失

**问题不在于自动重连不存在，而在于：**

1. **自动重连可能被轮询风暴破坏**
   - `pollFeedback()` 每 10ms 执行一次（`systemClock.start(10)`）
   - 断连期间每次轮询都发起多个 Modbus 读请求（Coil + Word 请求）
   - 每个请求在 `executeTransaction()` 中：
     ```cpp
     // AsioModbusTcpClient.cpp:319
     if (!m_connected.load(...)) {
         LOG_WARN_EVERY_MS(1000, ..., "not connected");  // 每秒限频
         return CommunicationResult::Disconnected();
     }
     ```
   - 虽然日志有限频，但 `promise/future` 创建、`asio::post` 调度、超时等待仍在持续
   - 高频无效轮询占用 io_context 线程，可能与重连逻辑竞争

2. **UI 层无手动重连入口**
   - `AsioModbusTcpClient::start()` / `stop()` 方法存在，但仅在 `main.cpp` 初始化和析构时调用
   - `ModbusSystemDriver` **不暴露** `start()` / `stop()` / `isConnected()` 给上层
   - `ISystemDriver` 接口只有 `send()` 和 `pollFeedback()`，无连接管理方法
   - 没有任何 ViewModel 方法可以触发重连

3. **连接配置硬编码**
   - PLC IP 地址 `192.168.1.88` 在 `main.cpp:194` 硬编码
   - 无法在运行时修改连接参数

---

## 3. 实现方案设计

### 3.1 总体策略

采用**分层渐进式**改造，在不破坏现有 Clean Architecture 分层的前提下，逐层添加连接状态感知和手动重连能力：

```
表现层 (QML)          ← 新增: 连接状态指示灯 + 重连按钮
    ↕
表现层 (ViewModel)    ← 新增: isConnected / connectionDiagnostic 属性 + reconnect() 方法
    ↕
应用层 (ISystemDriver) ← 新增: getConnectionState() / reconnect() 虚方法
    ↕
基础设施层 (Driver)    ← 新增: 桥接 AsioModbusTcpClient 的连接状态 + 重连触发
    ↕
基础设施层 (Client)    ← 已有: isConnected() / start() / stop()
```

### 3.2 P1 解决方案：界面 PLC 网络状态指示

#### 3.2.1 基础设施层改造

**`ISystemDriver.h`** — 添加连接状态查询接口：

```cpp
// 新增：连接状态结构体（在 CommunicationResult 之后）
struct ConnectionState {
    bool connected = false;
    std::string diagnostic;  // 例: "已连接 192.168.1.88:502" 或 "断连: ECONNREFUSED"
    std::chrono::steady_clock::time_point lastConnectedTime{};
    std::chrono::steady_clock::time_point lastDisconnectedTime{};
    uint32_t reconnectAttempts = 0;
};

class ISystemDriver {
public:
    virtual ~ISystemDriver() = default;
    virtual CommunicationResult send(const SystemCommand& cmd) = 0;
    virtual void pollFeedback(SystemContext& ctx) = 0;

    // ★ 新增：查询当前连接状态
    virtual ConnectionState getConnectionState() const = 0;
};
```

**`ModbusSystemDriver.h`** — 实现连接状态查询 + 重连触发：

```cpp
class ModbusSystemDriver : public ISystemDriver {
public:
    // ★ 新增方法
    ConnectionState getConnectionState() const override;
    void reconnect();  // 手动触发重连

    // 现有方法不变...
    CommunicationResult send(const SystemCommand& cmd) override;
    void pollFeedback(SystemContext& ctx) override;
    // ...

private:
    // ★ 新增成员：缓存连接诊断信息
    mutable std::string m_lastConnectionDiag;
    // 现有成员不变...
};
```

**`ModbusSystemDriver` 实现要点：**

```cpp
inline ConnectionState ModbusSystemDriver::getConnectionState() const {
    ConnectionState state;
    if (m_modbusClient) {
        state.connected = m_modbusClient->isConnected();
    }
    state.diagnostic = m_lastConnectionDiag;
    return state;
}

inline void ModbusSystemDriver::reconnect() {
    if (m_modbusClient) {
        // 需要将 AsioModbusTcpClient* 向下转型或通过接口扩展
        // 方案 A: 在 IModbusClient 添加 reconnect() 虚方法
        // 方案 B: 持有具体类型指针（当前已经持有 IModbusClient*）
        // 推荐方案 A，保持接口抽象
    }
}
```

**`IModbusClient.h`** — 添加重连接口：

```cpp
class IModbusClient {
public:
    virtual ~IModbusClient() = default;

    // ★ 新增：连接管理
    virtual bool isConnected() const = 0;
    virtual void requestReconnect() = 0;

    // 现有读/写接口不变...
};
```

**`AsioModbusTcpClient`** — 实现接口（`isConnected()` 已存在）：

```cpp
// isConnected() 已实现
// ★ 新增 requestReconnect() — 触发立即重连
void AsioModbusTcpClient::requestReconnect() {
    // 通过 io_context 调度：关闭当前 socket → 立即启动 startReconnect
    asio::post(m_ioctx, [this]() {
        std::error_code ec;
        m_socket.close(ec);
        m_connected.store(false, std::memory_order_release);
        m_timer.cancel(ec);
        startReconnect();  // 立即重连，不等 scheduleReconnect 的 2 秒延迟
    });
}
```

#### 3.2.2 应用层 / 表现层改造

**`AxisViewModelCore`** — 无需直接修改（ViewModel 通过 `SystemContext → ISystemDriver` 获取连接状态）。

**新增 `ConnectionViewModel`（推荐方案）** 或扩展 `QtAxisViewModel`：

推荐**新增专用 ViewModel**，因为连接状态是**分组级别**的概念（每个分组一个 ModbusSystemDriver 和一个 TCP 连接），不应挂在某个轴下面。

```cpp
// presentation/viewmodel/ConnectionViewModel.h (新建)
class ConnectionViewModelCore {
public:
    ConnectionViewModelCore(SystemManager& manager, const std::string& groupName);

    bool isConnected() const;
    std::string statusText() const;         // "已连接" / "断连 (192.168.1.88)"
    std::string diagnosticText() const;     // 详细诊断
    void reconnect();                       // 手动触发重连

    void tick();  // 每帧刷新
private:
    SystemManager& m_manager;
    std::string m_groupName;
};

// Qt 适配层
class QtConnectionViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectionChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY connectionChanged)
    Q_PROPERTY(QString diagnosticText READ diagnosticText NOTIFY connectionChanged)

public:
    Q_INVOKABLE void reconnect();
    // ...
};
```

#### 3.2.3 QML UI 改造

在 `TelemetryBlock.qml` 顶部状态栏添加连接状态指示灯 + 重连按钮：

```qml
// 新增：连接状态指示行（在标题栏下方）
RowLayout {
    Layout.fillWidth: true
    spacing: 8 * Theme.scale

    // 连接指示灯
    Rectangle {
        width: 12 * Theme.scale
        height: 12 * Theme.scale
        radius: width / 2
        color: connectionViewModel && connectionViewModel.connected
               ? Theme.colorIdle : Theme.colorError
        border.color: Qt.lighter(color, 1.5)
        border.width: 1
    }

    Text {
        text: connectionViewModel ? connectionViewModel.statusText : "未知"
        color: connectionViewModel && connectionViewModel.connected
               ? Theme.colorIdle : Theme.colorError
        font.pixelSize: Theme.fontSmall
    }

    Item { Layout.fillWidth: true }

    // 重连按钮（仅在断连时可见）
    IndustrialButton {
        text: "⟳ 重连"
        visible: connectionViewModel && !connectionViewModel.connected
        buttonSize: 60 * Theme.scale
        baseColor: "#D84315"
        onClicked: {
            if (connectionViewModel) connectionViewModel.reconnect()
        }
    }
}
```

### 3.3 P2 解决方案：手动重连机制

#### 3.3.1 核心改造点

| 改造位置 | 改造内容 | 优先级 |
|----------|---------|:---:|
| `IModbusClient` | 添加 `requestReconnect()` 纯虚方法 | 高 |
| `AsioModbusTcpClient` | 实现 `requestReconnect()` — 跳过 2s 延迟立即重连 | 高 |
| `ISystemDriver` | 添加 `getConnectionState()` / `reconnect()` 虚方法 | 高 |
| `ModbusSystemDriver` | 桥接实现，缓存诊断信息 | 高 |
| `ConnectionViewModel` | 新建 ViewModel，暴露连接状态 + 重连触发 | 高 |
| QML UI | 添加连接指示灯 + 重连按钮 | 高 |
| `pollFeedback` | 断连时**跳过无效轮询**，避免 io_context 竞争 | 中 |

#### 3.3.2 pollFeedback 断连优化（性能保护）

```cpp
inline void ModbusSystemDriver::pollFeedback(SystemContext& ctx) {
    servicePendingEdgeTriggers();

    if (!m_modbusClient || !m_poller) {
        return;
    }

    // ★ 新增：断连时跳过轮询，避免无效 Modbus 请求
    if (!m_modbusClient->isConnected()) {
        // 创建 untrusted 快照但不发起网络请求
        const auto now = m_clock->now();
        const uint64_t timestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count());
        auto untrusted = protocol::PlcPoller::untrusted(timestamp);
        if (m_device) { m_device->updateSnapshot(std::move(untrusted)); }
        // ★ 更新诊断信息
        m_lastConnectionDiag = "断连: 等待自动重连中... (重试间隔 "
                              + std::to_string(/* reconnectIntervalMs */) + "ms)";
        return;
    }

    // 原有轮询逻辑...
    const auto req = m_poller->prepare();
    // ...
}
```

#### 3.3.3 main.cpp 组装变更

```cpp
// ★ 新增：连接状态 ViewModel（每个分组一个）
ConnectionViewModelCore connVMCore_A(manager, "Machine_A");
ConnectionViewModelCore connVMCore_B(manager, "Machine_B");
QtConnectionViewModel qtConnVM_A(&connVMCore_A);
QtConnectionViewModel qtConnVM_B(&connVMCore_B);

// ★ 注册到 QML 上下文
engine.rootContext()->setContextProperty("connectionVM_A", &qtConnVM_A);
engine.rootContext()->setContextProperty("connectionVM_B", &qtConnVM_B);

// tick loop 中推进
QObject::connect(&systemClock, &QTimer::timeout, [&]() {
    // ... 现有 pollFeedback + ViewModel tick

    // ★ 新增：推进连接状态
    qtConnVM_A.tick();
    qtConnVM_B.tick();
});
```

---

## 4. 实施路线图

### 阶段 1：基础设施层接口扩展（影响范围小，风险低）

```
步骤 1.1: IModbusClient 添加 requestReconnect() + isConnected() 纯虚方法
步骤 1.2: AsioModbusTcpClient 实现 requestReconnect()（立即重连逻辑）
步骤 1.3: ISystemDriver 添加 ConnectionState + getConnectionState() + reconnect()
步骤 1.4: ModbusSystemDriver 桥接实现
步骤 1.5: pollFeedback 添加断连跳过优化
```

**预估改动文件**: 5 个  
**预估新增代码**: ~120 行  
**测试要点**: 单元测试验证 `requestReconnect()` 正确关闭 socket 并触发 `startReconnect()`

### 阶段 2：表现层连接状态暴露

```
步骤 2.1: 新建 ConnectionViewModelCore (.h/.cpp)
步骤 2.2: 新建 QtConnectionViewModel (.h/.cpp)
步骤 2.3: 在 CMakeLists.txt 中注册新文件
步骤 2.4: 在 main.cpp 中组装并注入 QML 上下文
步骤 2.5: 在 tick loop 中推进
```

**预估改动文件**: 新建 2 个 + 修改 2 个  
**预估新增代码**: ~200 行  

### 阶段 3：QML UI 改造

```
步骤 3.1: 在 TelemetryBlock.qml 顶部添加连接状态指示行
步骤 3.2: 添加连接指示灯（绿/红/灰） + 状态文本
步骤 3.3: 添加重连按钮（断连时可见）
步骤 3.4: 适配移动端（isMobile）布局
```

**预估改动文件**: 1 个  
**预估新增代码**: ~50 行

### 阶段 4：测试与验证

```
步骤 4.1: 编写 ConnectionViewModelCore 单元测试
步骤 4.2: 集成测试：模拟断网 → 验证 UI 指示灯变红
步骤 4.3: 集成测试：点击重连按钮 → 验证重连成功
步骤 4.4: 安卓真机测试：WiFi 断开/恢复场景
```

---

## 5. 设计决策与注意事项

### 5.1 为什么新增 ConnectionViewModel 而不是扩展现有 ViewModel？

- **语义清晰**：连接状态是"分组级别"概念，不属于任何一个轴
- **职责单一**：`AxisViewModelCore` 已经很大（150+ 行），继续膨胀违反 SRP
- **可扩展**：未来可能需要显示更多连接指标（延迟、丢包率、重连次数）

### 5.2 为什么不直接暴露 AsioModbusTcpClient 指针给 UI？

- 违反 Clean Architecture 依赖方向（UI 不应感知基础设施层具体实现）
- 破坏线程安全（`isConnected()` 是 atomic 安全，但 `start()` / `stop()` 需要 io_context 线程调度）

### 5.3 自动重连 + 手动重连的协作

- 断连时自动重连继续工作（每 2 秒重试）
- 手动重连相当于"加速"：跳过当前等待周期，立即发起重连
- 两者不冲突：`requestReconnect()` 内部取消当前 timer → 关闭 socket → 调用 `startReconnect()`

### 5.4 日志系统增强

建议在连接状态变化时输出 `LOG_SUMMARY` 级别日志，便于生产环境追溯：

```cpp
// 在 AsioModbusTcpClient 连接成功回调中：
LOG_SUMMARY(LogLayer::HAL, m_moduleName,
    "PLC 连接已建立: " + ep.address().to_string() + ":" + std::to_string(ep.port()));

// 在断连检测时：
LOG_SUMMARY(LogLayer::HAL, m_moduleName,
    "PLC 连接已断开: " + ec.message());
```

---

## 6. 相关文件清单

| 文件路径 | 改造类型 | 说明 |
|----------|:---:|------|
| `infrastructure/ISystemDriver.h` | 修改 | 添加 `ConnectionState` 结构体 + `getConnectionState()` / `reconnect()` 虚方法 |
| `infrastructure/plc/protocol/IModbusClient.h` | 修改 | 添加 `isConnected()` / `requestReconnect()` 纯虚方法 |
| `infrastructure/plc/protocol/AsioModbusTcpClient.h` | 修改 | 添加 `requestReconnect()` 声明 |
| `infrastructure/plc/protocol/AsioModbusTcpClient.cpp` | 修改 | 实现 `requestReconnect()` + `isConnected()` override |
| `infrastructure/plc/ModbusSystemDriver.h` | 修改 | 实现 `getConnectionState()` / `reconnect()` + pollFeedback 断连优化 |
| `presentation/viewmodel/ConnectionViewModelCore.h` | **新建** | 连接状态 ViewModel 纯逻辑核心 |
| `presentation/viewmodel/ConnectionViewModelCore.cpp` | **新建** | 实现 |
| `presentation/viewmodel/QtConnectionViewModel.h` | **新建** | Qt 适配层 |
| `presentation/viewmodel/QtConnectionViewModel.cpp` | **新建** | 实现 |
| `presentation/CMakeLists.txt` | 修改 | 注册新文件 |
| `main.cpp` | 修改 | 组装 ConnectionViewModel + 注入 QML + tick 推进 |
| `Main.qml` | 修改 | 注入 connectionViewModel 到 TelemetryBlock |
| `presentation/qml/blocks/TelemetryBlock.qml` | 修改 | 添加连接状态指示行 + 重连按钮 |

---

## 7. 附录：当前连接相关代码位置索引

| 功能 | 文件 | 行号/位置 |
|------|------|----------|
| 连接配置 | `main.cpp` | 194-201（硬编码 IP） |
| 客户端启动 | `main.cpp` | 205-206 |
| 自动重连间隔 | `AsioModbusTcpClient.h` | 60 |
| TCP 连接 | `AsioModbusTcpClient.cpp` | 715-777 (`startReconnect`) |
| 重连调度 | `AsioModbusTcpClient.cpp` | 780-812 (`scheduleReconnect`) |
| 断连检测 | `AsioModbusTcpClient.cpp` | 319-327, 402-426, 447-472, 535-559 |
| 快照可信度 | `ModbusSystemDriver.h` | 664-668 (untrusted 创建) |
| 轮询主循环 | `main.cpp` | 411-451 (10ms tick) |
| 状态摘要 | `main.cpp` | 454-473 (1s 周期) |
| isConnected flag | `AsioModbusTcpClient.h` | 185 (`std::atomic<bool> m_connected`) |
| CommunicationResult | `ISystemDriver.h` | 24-137 |