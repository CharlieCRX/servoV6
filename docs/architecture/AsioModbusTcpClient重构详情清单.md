# AsioModbusTcpClient 重构详情清单

> **基础文档**：《AsioModbusTcpClient 当前问题分析.md》
> **评审版本**：v3（经第二轮架构评审，补齐 runtime lifecycle 契约）
> **生成日期**：2026-05-27
> **状态**：重构规划阶段

---

## 概述

本文档基于《AsioModbusTcpClient 当前问题分析》中识别的 7 个问题，结合当前 `AsioModbusTcpClient.h` / `.cpp` 的实际代码，逐项给出精确的重构区域、影响范围、需要修改的代码点以及验收标准。

v3 版本在 v2 基础上，重点补齐了 **async runtime 最核心的生命周期契约**：in-flight sequencing、ownership、cancellation semantics、queue semantics、backoff policy。

### 演进历史

| 版本 | 关键变化 |
|------|----------|
| v1 | 初始梳理：7 个重构项，同步 I/O → 异步、queue+cv 串行化 |
| v2 | 架构评审调整：queue+cv → strand、transaction 不直接 reconnect → supervisor、+Reconnecting/Stopping 状态 |
| v3 | runtime 契约补齐：in-flight gate、enable_shared_from_this、stop() cancellation contract、backoff、queue policy |

### 当前代码缺陷总览

| 代码标注 | 实际状态 | 说明 |
|----------|----------|------|
| Phase 2.3 socket 级超时 | ⚠️ 部分实现 | `steady_timer + socket.cancel()` 已引入，但 `&timedOut` 悬空引用导致 UB |
| `m_socketMutex` | ❌ 声明但未使用 | 方向根本错误，应改用 strand + in-flight gate |
| `isConnected()` | 仅二态 bool | 无 Operational/Fault/Reconnecting/Stopping 工业语义 |
| async callback 生命周期 | ❌ 未定义 | 无 `enable_shared_from_this`，stop() 后 callback 可能 UAF |
| transaction 互斥 | ❌ 无保护 | strand 只保证 handler 串行，不保证 socket transaction 串行 |
| stop() cancellation | ❌ 无契约 | pending transaction 的 promise 可能永远不返回或 double set_value |

---

## 核心架构原则（贯穿所有重构项）

### 原则一：利用 Asio 自身事件队列，不倒腾第二套调度系统

```
❌ 错误：std::queue + condition_variable + consumerLoop + std::mutex
✅ 正确：asio::strand<io_context::executor_type>
```

Asio 的 `io_context` 本身就是事件循环。事务串行化应该使用 `strand`——它保证同一 strand 内的 handler 串行执行，无需额外的锁或队列。

### 原则二：strand 只保证 handler 串行，不保证 socket transaction 串行

```
strand 保证：两个 handler 不会同时执行
strand 不保证：async_write 完成前不会有新的 async_write 发起
```

**关键**：异步操作的生命周期跨越多个 handler。当 TX1 的 `async_write` 完成、handler 返回后，TX2 的 handler 可能被 strand 调度执行并立即发起新的 `async_write`——而此时 TX1 的 `async_read` 可能仍在进行。结果：两个 transaction 同时共享 socket，response 交织，协议污染。

**解决方案**：in-flight transaction gate（见重构项 #2.2）。

### 原则三：所有异步回调必须持有对象生命周期

```
❌ 错误：[this](...) { ... }    — stop() 后 this 可能已析构
✅ 正确：[self = shared_from_this(), ctx](...) { ... }
```

这是 Asio 异步编程最经典的崩溃来源。所有 post 到 io_context 的 handler 必须通过 `shared_ptr` 持有对象引用，确保回调执行时对象仍然存活。

### 原则四：分层 — transaction layer 不污染 connection lifecycle

```
transaction 的职责：执行请求-响应，失败时 markDisconnected()
connection supervisor 的职责：统一决定是否 reconnect、何时 reconnect、是否 backoff
```

### 原则五：timeout 属于 transaction，不属于 connection

```
❌ 错误：m_timer（全局唯一，所有 transaction 共享）
✅ 正确：AsyncTransactionContext::timeoutTimer（每个 transaction 独立）
```

---

## 第 1 步：修复悬空引用 UB

**优先级**：🔥🔥🔥 最高（必须最先修复，否则所有后续测试不稳定）

### 1.1 当前缺陷

```cpp
// AsioModbusTcpClient.cpp — executeTransaction lambda 内（当前代码）
bool timedOut = false;                                // ← 栈上变量

m_timer.async_wait([this, tid, &timedOut](...) {     // ← 捕获引用！
    timedOut = true;                                  // ← UB：lambda 可能在 timedOut 已析构后执行
    m_socket.cancel(cancelEc);
});
```

`timedOut` 是栈上局部变量。`async_wait` 回调异步执行——如果 timer 在 `executeTransaction` 返回后才触发，`&timedOut` 是**悬空引用**。

### 1.2 需要修改的文件

| 文件 | 修改性质 | 描述 |
|------|----------|------|
| `AsioModbusTcpClient.cpp` | Bug 修复 | 将 `timedOut` 改为 `shared_ptr<bool>` 管理生命周期 |

### 1.3 修复方案

```cpp
auto timedOut = std::make_shared<bool>(false);

m_timer.async_wait([this, tid, timedOut](std::error_code timerEc) {
    if (timerEc) return;
    *timedOut = true;
    m_socket.cancel(ec);
});
```

**注**：后续 #4 引入 `AsyncTransactionContext` 时，`timedOut` 将移入 context 结构体统一管理。

### 1.4 验收标准

- [ ] `executeTransaction` 内不再有通过引用捕获的局部变量作为异步回调的数据
- [ ] AddressSanitizer / ThreadSanitizer 无悬空引用或数据竞争报告

---

## 第 2 步：建立 transaction 安全执行模型（strand + in-flight gate）

**优先级**：🔥🔥🔥 最高（必须先于 async 化，建立单线程事务语义和互斥保证）

### 2.1 引入 asio::strand 实现 handler 串行化

#### 当前缺陷

```cpp
// AsioModbusTcpClient.h — 声明但未使用，且方向根本错误
mutable std::mutex m_socketMutex;
```

#### 方案

```cpp
// 移除 m_socketMutex，新增：
asio::strand<asio::io_context::executor_type> m_strand;
```

所有 `asio::post(m_ioctx, ...)` 替换为 `asio::post(m_strand, ...)`，strand 保证 handler 串行执行，无需额外锁。

### 2.2 引入 In-Flight Transaction Gate 实现 socket 级事务互斥

**这是 v3 新增的关键机制。**

#### 为什么 strand 不够

```
TX1: async_write → handler 返回             ← strand 看到 handler 返回
TX2: handler 被调度 → async_write          ← 此时 TX1 的 async_read 可能还在 pending
结果：两个 async_read 同时等待 response → 协议污染
```

strand 保证 handler 不并发，但**异步操作的生命周期跨越多个 handler**。TX1 的 `async_read` 还在等待 response 时，TX2 的 handler 已经可以发起新的 `async_write`。

#### 需要增加的机制

```cpp
// 头文件新增
bool m_transactionActive{false};
std::queue<PendingTransaction> m_pendingQueue;
// 注意：这里是 pending queue，不是调度 queue
// 所有入队/出队操作都在 strand 内，无需额外锁
```

#### In-Flight Gate 工作流

```
executeTransaction(request, promise):
    post to strand:
        if (m_transactionActive):
            m_pendingQueue.push({request, promise})
            return   // ← 当前 transaction 等待

        m_transactionActive = true
        执行 async_write → async_read → async_read 链

onTransactionComplete():
    // 在 async 链的最后一步（promise.set_value 之后）
    m_transactionActive = false
    if (!m_pendingQueue.empty()):
        next = m_pendingQueue.front()
        m_pendingQueue.pop()
        递归执行 next   // ← 启动排队的 transaction
```

#### 为什么 pending queue 不需要 mutex

因为所有 queue 操作（push / pop / check empty）都在 strand 内完成。strand 已经保证了串行，不需要额外锁。这避免了 v1 设计的"双调度系统"问题。

### 2.3 需要修改的文件

| 文件 | 修改性质 | 描述 |
|------|----------|------|
| `AsioModbusTcpClient.h` | 新增成员 / 移除旧成员 | 新增 `m_strand`、`m_transactionActive`、`m_pendingQueue`；移除 `m_socketMutex` |
| `AsioModbusTcpClient.cpp` | 修改 post 目标 + 新增 gate 逻辑 | `asio::post(m_strand, ...)` + in-flight 检查 |

### 2.4 验收标准

- [ ] `m_socketMutex` 声明被移除
- [ ] 多线程并发调用读写接口，socket 上严格只有一个 in-flight transaction
- [ ] 并发 transaction 在 pending queue 中排队，前一个完成后自动启动下一个
- [ ] 新增单元测试：并发 3 个 transaction 验证串行执行 + pending 启动
- [ ] 现有测试全部通过

---

## 第 3 步：确立 Async Ownership Model（异步生命周期契约）

**优先级**：🔥🔥🔥 最高（这是 async 系统最经典的崩溃来源，必须在 async 化之前确立）

### 3.1 问题描述

当前所有 async callback 使用裸 `[this]` 捕获：

```cpp
async_read(socket, buffer, [this](error_code ec, size_t len) {
    // this 可能已析构 ← UAF
});
```

**场景**：

```
1. TX1 正在执行 async_read（callback 在 io_context queue 中等待）
2. 业务线程调用 stop() → socket.close() → io_context.stop() → join()
3. ~AsioModbusTcpClient() 析构
4. TX1 callback 被 io_context 调度执行 → this 是悬空指针 → UAF
```

**即使 `work_guard` 阻止了 `io_context::run()` 返回，`stop()` 调用 `socket.close()` 后，已排队的 handler 仍会被调用（带 error_code）。**

### 3.2 解决方案：enable_shared_from_this

```cpp
class AsioModbusTcpClient
    : public IModbusClient
    , public std::enable_shared_from_this<AsioModbusTcpClient>  // ← 新增
{
    // ...
};
```

**强制规则**：所有异步回调必须通过 `shared_from_this()` 捕获自身：

```cpp
// ❌ 禁止
async_write(socket, buffer, [this](error_code ec, size_t len) { ... });

// ✅ 必须
auto self = shared_from_this();
async_write(socket, buffer, [self, ctx](error_code ec, size_t len) { ... });
```

### 3.3 Transaction Context 的生命周期

`AsyncTransactionContext` 使用 `shared_ptr` 管理：

```cpp
auto ctx = std::make_shared<AsyncTransactionContext>();
ctx->promise = std::move(promise);
ctx->timeoutTimer = asio::steady_timer(m_ioctx);

// 整个 async 链的所有 callback 都持有 ctx
auto self = shared_from_this();
async_write(socket, buffer, [self, ctx](error_code ec, size_t len) {
    // ctx 保证 requestFrame, mbap, pdu, timer, promise 都存活
    // self 保证 client 对象存活
});
```

**所有权模型**：

```
┌─────────────────────────────────────────┐
│ AsyncTransactionContext (shared_ptr)     │
│ ├── requestFrame                         │
│ ├── mbap buffer                          │
│ ├── pdu buffer                           │
│ ├── timeoutTimer                         │
│ ├── timedOut (shared_ptr<bool>)          │
│ ├── promise                              │
│ └── tid                                  │
│                                           │
│ 生命周期：由 callback 链持有              │
│ 最后一个 callback 返回后自动析构          │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│ AsioModbusTcpClient (shared_from_this)  │
│                                           │
│ 生命周期：由 callback 链 + 外部持有者     │
│ stop() 不会立即析构，pending callback    │
│ 通过 self 保持对象存活直到完成            │
└─────────────────────────────────────────┘
```

### 3.4 与 stop() 的交互

```cpp
void stop() {
    m_running.store(false);
    // cancel 所有 outstanding async operation
    m_socket.close();       // 所有 pending async_read/write 将以 error_code 返回
    m_timer.cancel();       // 所有 pending timer 回调将以 error_code 返回
    // 此时 callback 通过 self 持有对象，不会 UAF
    // 所有 callback 执行完毕后，外部持有者 release → 析构
}
```

### 3.5 需要修改的文件

| 文件 | 修改性质 | 描述 |
|------|----------|------|
| `AsioModbusTcpClient.h` | 接口变更 | 继承 `enable_shared_from_this` |
| `AsioModbusTcpClient.cpp` | 全量修改 | 所有 async callback 改为 `[self = shared_from_this(), ctx]` |
| 所有创建 client 的代码 | 适配 | `new` → `make_shared` |

### 3.6 验收标准

- [ ] 所有 async callback 不再使用裸 `[this]` 捕获
- [ ] `AsioModbusTcpClient` 只能通过 `shared_ptr` 创建
- [ ] stop() → 析构路径无 UAF（AddressSanitizer 验证）
- [ ] 新增测试：stop() 期间有 pending async_read 时正常退出

---

## 第 4 步：消除同步阻塞 I/O，迁移到 async_write / async_read

**优先级**：🔥🔥🔥 最高（核心架构风险，必须在 strand + gate + ownership 之后进行）

### 4.1 当前实现

```cpp
readBytes = asio::write(m_socket, asio::buffer(frame), ec);   // 同步阻塞
readBytes = asio::read(m_socket, asio::buffer(mbap), ec);     // 同步阻塞
readBytes = asio::read(m_socket, asio::buffer(pdu), ec);      // 同步阻塞
```

半开连接场景下可能永不返回，`socket.cancel()` 不可靠。

### 4.2 AsyncTransactionContext 设计

```cpp
struct AsyncTransactionContext {
    std::vector<uint8_t>         requestFrame;
    std::vector<uint8_t>         mbap;             // 7 bytes
    std::vector<uint8_t>         pdu;
    asio::steady_timer           timeoutTimer;     // 独立：timeout 属于 transaction
    std::vector<uint8_t>*        responsePtr;
    std::promise<CommunicationResult> promise;
    std::shared_ptr<bool>        timedOut;         // #1 修复后移入
    uint16_t                     tid;
    uint8_t                      functionCode;
};
```

### 4.3 异步链状态机

```
executeTransaction()
  ├── 创建 ctx = make_shared<AsyncTransactionContext>()
  ├── 启动 ctx->timeoutTimer
  ├── post 到 m_strand：
  │     if (m_transactionActive) → 入队 pendingQueue, return future
  │     m_transactionActive = true
  │     self = shared_from_this()
  │     async_write(socket, ctx->requestFrame, [self, ctx](...) {
  │       ├── timeout/timerEc → processTimeout(ctx)
  │       ├── writeEc → processIOError(ctx)
  │       └── 成功 → async_read(socket, ctx->mbap, 7, [self, ctx](...) {
  │             ├── timeout → processTimeout(ctx)
  │             ├── readEc → processIOError(ctx)
  │             ├── 解析失败 → ctx->promise.set_value(InvalidResponse)
  │             └── 成功 → async_read(socket, ctx->pdu, pduLen, [self, ctx](...) {
  │                   ├── timeout → processTimeout(ctx)
  │                   ├── readEc → processIOError(ctx)
  │                   └── 成功 → 校验 TID + Modbus exception
  │                          → ctx->promise.set_value(...)
  │                          → onTransactionComplete()  ← 释放 gate，启动下一个
  │             })
  │       })
  └── return future.get()
```

### 4.4 关键辅助方法

```cpp
void processTimeout(shared_ptr<AsyncTransactionContext> ctx) {
    m_socket.cancel(ec);  // 取消 pending async operation
    ctx->promise.set_value(CommunicationResult::Timeout);
    onTransactionComplete();
}

void processIOError(shared_ptr<AsyncTransactionContext> ctx) {
    m_connected.store(false);
    m_supervisor.onTransactionFailure();  // 不直接启动 reconnect
    ctx->promise.set_value(CommunicationResult::Disconnected);
    onTransactionComplete();
}

void onTransactionComplete() {
    m_transactionActive = false;
    startNextPendingTransaction();  // 从 pendingQueue 取下一个
}
```

### 4.5 验收标准

- [ ] `executeTransaction` 内部不再包含任何 `asio::write` / `asio::read` 同步调用
- [ ] 网络断开时，transaction 在 `timeoutMs + 500ms` 内返回 Timeout
- [ ] 所有 handler 通过 `m_strand` 串行，socket 上严格只有一个 in-flight transaction
- [ ] 所有 callback 通过 `shared_from_this()` + `ctx` 持有生命周期
- [ ] 新增测试：拔网线场景下 transaction 超时恢复

---

## 第 5 步：stop() Cancellation Contract（异步取消契约）

**优先级**：🔥🔥🔥 最高（与 #4 紧密耦合，决定系统在异常下的行为一致性）

### 5.1 问题描述

当前 `stop()` 没有定义：
- Pending transaction 的 promise 如何处理？
- 排队中的 transaction 如何处理？
- 如何保证每个 promise 恰好 `set_value` 一次？

### 5.2 必须定义的契约

#### Contract #1：stop() 后所有 pending transaction 收到 `Cancelled`

```cpp
void stop() {
    m_running.store(false);

    // 1. 取消排队中的 transaction
    while (!m_pendingQueue.empty()) {
        auto& pending = m_pendingQueue.front();
        pending.promise.set_value(CommunicationResult::Cancelled);
        m_pendingQueue.pop();
    }

    // 2. 取消当前 in-flight transaction
    m_socket.close();   // → 所有 pending handler 将以 error_code 返回
    m_timer.cancel();   // → 所有 pending timer 将以 timerEc 返回

    // 3. 在 handler 中检查 m_running
    //    如果 !m_running 且收到 cancel 错误：
    //      ctx->promise.set_value(CommunicationResult::Cancelled)
    //      return（不再调用 onTransactionComplete）
}
```

#### Contract #2：每个 promise 恰好 set_value 一次

在 async callback 中增加 running 检查：

```cpp
// 在 async 链的每个 handler 入口
if (!self->m_running.load(std::memory_order_acquire)) {
    ctx->promise.set_value(CommunicationResult::Cancelled);
    self->onTransactionComplete();  // 释放 gate + 启动下一个（下一个也会被 cancel）
    return;
}
```

这确保：即使 `socket.close()` 和 `timer.cancel()` 都返回正常错误码，promise 也只收到一次 `Cancelled`。

#### Contract #3：onTransactionComplete 在 cancel 下的行为

```cpp
void onTransactionComplete() {
    m_transactionActive = false;
    if (m_running.load(std::memory_order_acquire)) {
        startNextPendingTransaction();
    }
    // 如果 !m_running，pendingQueue 已在 stop() 中被清空并设置了 Cancelled
}
```

### 5.3 需要修改的文件

| 文件 | 修改性质 | 描述 |
|------|----------|------|
| `AsioModbusTcpClient.cpp` | `stop()` 重写 | 增加 pendingQueue 清空 + promise Cancelled 逻辑 |
| `AsioModbusTcpClient.cpp` | handler 修改 | 所有 async handler 入口增加 `m_running` 检查 |

### 5.4 验收标准

- [ ] 所有 pending transaction 在 stop() 后收到 `CommunicationResult::Cancelled`
- [ ] 每个 promise 恰好 `set_value` 一次（无 double set、无 never set）
- [ ] stop() 后不再有新的 transaction 被处理
- [ ] 新增测试：stop() 取消 pending + in-flight transaction
- [ ] 新增测试：stop() 期间无 UAF（配合 #3 ownership 模型）

---

## 第 6 步：ConnectionSupervisor 独立重连状态机

**优先级**：🔥🔥 中高

### 6.1 当前缺陷

- 只处理初始连接失败，运行时断线无自动重连
- 如果简单在 transaction 错误路径加 `startReconnect()`，多个并发失败会导致多个重连接叉

### 6.2 设计原则

```
Transaction Layer:
  失败 → markDisconnected()  ← 不调用 startReconnect()

ConnectionSupervisor:
  统一决策：是否重连、何时重连、backoff 策略、stop 抑制
```

### 6.3 ConnectionSupervisor 接口

```cpp
class ConnectionSupervisor {
public:
    enum class State {
        Disconnected,
        Connecting,
        Connected,
        Operational,
        Reconnecting,
        Stopping,
        Fault
    };

    void onConnected();
    void onTransactionSuccess();
    void onTransactionFailure();   // transaction 失败时调用
    void onStop();                 // enter Stopping
    State state() const;
};
```

### 6.4 状态转换图

```
Disconnected ──(start)→ Connecting ──(成功)→ Connected ──(首次成功TX)→ Operational
     ▲                    │                     │                         │
     │                    │(失败)               │(TX失败)                 │(连续失败≥N)
     │                    ▼                     ▼                         ▼
     │              Reconnecting ◄──────────────────────────────────── Fault
     │                    │
     │                    │(成功)
     │                    ▼
     │                Connected
     │
     │  stop() 调用时，任意状态 → Stopping
     │
     └──(stop完成)→ Disconnected
```

### 6.5 验收标准

- [ ] 运行时断线 → 自动重连成功
- [ ] 多个并发 transaction 同时失败 → 只触发一次重连
- [ ] stop() 后 transaction 失败不会触发重连

---

## 第 7 步：扩展 ConnectionState 模型

**优先级**：🔥 中（依赖 #6 supervisor 就位）

### 7.1 完整状态枚举

```cpp
enum class ConnectionState {
    Disconnected,    // 初始 / 主动断开 / stop 完成后
    Connecting,      // DNS 解析或 TCP 连接进行中
    Connected,       // TCP 已连接但未验证通讯
    Operational,     // 最近一次 transaction 成功
    Reconnecting,    // 断线后等待重连间隔
    Fault,           // 连续 N 次 transaction 失败
    Stopping         // stop() 被调用，正在停止
};
```

### 7.2 接口变更

```cpp
[[deprecated("Use connectionState() instead")]]
bool isConnected() const;

[[nodiscard]]
ConnectionState connectionState() const;
```

### 7.3 验收标准

- [ ] `connectionState()` 正确反映当前阶段
- [ ] stop() 期间返回 `Stopping`，不会自动重连

---

## 第 8 步：Reconnect Backoff Policy

**优先级**：⚠️ 中长期（非 block，但工业现场必须）

### 8.1 问题

固定间隔重连（如 500ms 循环 forever）在工业现场会导致：
- Log storm（高频日志）
- CPU 空转
- 网络风暴
- PLC 恢复后的连接雪崩

### 8.2 方案：Exponential Backoff

```cpp
std::chrono::milliseconds reconnectInterval(int consecutiveFailures) {
    // 0.5s → 1s → 2s → 5s → 10s → 30s (max)
    constexpr int base = 500;
    int ms = std::min(base * (1 << consecutiveFailures), 30000);
    return std::chrono::milliseconds(ms);
}
```

### 8.3 验收标准

- [ ] 首次重连 500ms，连续失败逐渐延长到 30s max
- [ ] 重连成功后重置 backoff 计数器
- [ ] 新增测试：验证 backoff 间隔序列

---

## 第 9 步：Transaction Queue Semantic 定义

**优先级**：⚠️ 中长期

### 9.1 需要定义的语义

| 语义 | 定义 |
|------|------|
| **Queue 容量** | 当前阶段无上限（pending queue 在 strand 内部，内存可控） |
| **stop() 行为** | 清空 queue，所有 pending promise 收到 `Cancelled` |
| **reconnect 时 queue** | 保留（pending transaction 在重连成功后顺序执行） |
| **timeout transaction** | 已超时的 transaction 不重试，直接返回 Timeout |
| **retry policy** | 当前阶段不自动重试（由上层 Policy 决定） |

### 9.2 验收标准

- [ ] stop() 清空 queue 且每个 promise 收到 Cancelled
- [ ] reconnect 成功后 pending transaction 顺序执行
- [ ] 文档化语义被单元测试覆盖

---

## 第 10 步：日志系统增强 — Connection Session ID

**优先级**：⚠️ 中长期（工业排障非常关键）

### 10.1 当前问题

重新连接后，无法区分日志中的 transaction 属于哪次连接会话。

### 10.2 方案

```cpp
uint64_t m_connectionSessionId{0};  // 每次 connect 成功时 ++
```

日志格式：

```
[session=42][TID=7][MODBUS] ← 00 03 00 00 00 06 01 03 00 00 00 01
```

### 10.3 验收标准

- [ ] 每次 connect 成功 sessionId 递增 1
- [ ] 所有日志含 sessionId 前缀

---

## 第 11 步：日志系统工程化（std::cout 替换）

**优先级**：⚠️ 中长期优化（可随时独立进行）

详见原重构项 #6，与 v2 保持一致，此处不重复展开。

| 当前 std::cout 前缀 | 新日志级别 | Tag |
|---------------------|------------|-----|
| `[IO]` + 生命周期 | `LOG_INFO` | `modbus.io` |
| `[CONNECT]` + 连接 | `LOG_INFO` | `modbus.connect` |
| `[TX]` + 事务执行 | `LOG_DEBUG` | `modbus.tx` |
| `[MODBUS]` + 帧交互 | `LOG_TRACE` | `modbus.frame` |
| `[TIMEOUT]` + 超时 | `LOG_WARN` | `modbus.timeout` |

---

## 第 12 步：未来异步模型升级（远期规划）

**优先级**：⚠️ 长期架构优化（非当前必做）

当前 `promise/future` 模型是**伪异步**——只是把阻塞从 io 线程转移到业务线程。多轴高频轮询时会导致 poller thread 卡死。

长期方向：`promise/future → callback → coroutine / awaitable`。

当前阶段保留 `promise/future` 是合理的——重点是先把 socket 生命周期做稳定。

---

## 当前阶段保留的合理设计（未来需要演进）

### A. `promise/future` 同步桥接

**当前状态**：✅ 保留。当前阶段是合理的同步-异步桥接方案。
**未来演进**：多轴高频轮询时迁移到 callback / coroutine。
**注意事项**：重构中不要移除 `promise/future`。

### B. 全局 `m_timer` 用于重连

**当前状态**：✅ 保留。属 connection 级别关注点，用于重连间隔。
**未来演进**：transaction timeout 使用独立的 `AsyncTransactionContext::timeoutTimer`。
**关键边界**：`m_timer` 仅用于重连调度，不用于 transaction 超时。

### C. ConnectionSupervisor 为非严格 FSM

**当前状态**：✅ 保留。使用 `onConnected()` / `onFailure()` callback-style 管理是合理的中间态。
**未来演进**：升级为 event-driven FSM（单入口 `handleEvent(Event)`）。
**当前评估**：supervisor 已独立，已是巨大进步，event-driven 可后续重构。

---

## 执行顺序

| 顺序 | 重构项 | 理由 |
|------|--------|------|
| **第 1 步** | #1 修复悬空引用 UB | 不改此 bug，所有后续测试不稳定；可独立合入 |
| **第 2 步** | #2 strand + in-flight gate | 建立事务安全执行模型；strand 串行 + gate 互斥 |
| **第 3 步** | #3 async ownership 模型 | `enable_shared_from_this` + callback capture 规则；必须在 async 化之前 |
| **第 4 步** | #4 async_write/read 改造 | 在 strand+gate+ownership 基础上进行异步化 |
| **第 5 步** | #5 stop() cancellation 契约 | 定义 pending/in-flight transaction 取消行为；每个 promise 恰好 set_value 一次 |
| **第 6 步** | #6 ConnectionSupervisor | 独立重连管理 |
| **第 7 步** | #7 ConnectionState 扩展 | 依赖 supervisor 就位 |
| **第 8 步** | #8 Backoff Policy | 依赖 supervisor 的重连触发点 |
| **第 9 步** | #9 Queue Semantics | 定义 pending queue 的 stop/reconnect 行为 |
| **第 10 步** | #10 Session ID | 日志增强 |
| **第 11 步** | #11 日志系统 | 独立，可随时并行 |
| **第 12 步** | #12 async 模型升级 | 远期规划 |

### 依赖关系图

```
#1 (修复 UB)
  │
  ▼
#2 (strand + gate)
  │
  ▼
#3 (ownership model)
  │
  ▼
#4 (async I/O)
  │
  ├──► #5 (stop() cancellation)  ← 与 #4 紧密耦合
  │
  ├──► #6 (ConnectionSupervisor)
  │       │
  │       ├──► #7 (ConnectionState)
  │       ├──► #8 (Backoff Policy)
  │       └──► #9 (Queue Semantics)
  │
  └──► #10 (Session ID)
         │
         ▼
        #11 (日志系统)
         │
         ▼
        #12 (async 模型升级)

注：#5 必须在 #4 之后立即完成（同一 PR 或紧随其后），
    因为 #5 定义的行为直接影响 #4 的 handler 实现。
```

### 前三步为何必须此顺序

```
#1 UB 修复：
  └─ 不改，所有测试不可靠
       │
#2 strand + gate：
  └─ 建立线程模型 + 事务互斥保证
    不先做这一步，后面的 async 化会在并发交织中崩溃
       │
#3 ownership：
  └─ 建立生命周期保证
    不先做这一步，async 化之后的 stop() → UAF
```

---

## 风险评估

| 重构项 | 风险等级 | 风险说明 | 缓解措施 |
|--------|----------|----------|----------|
| #1 | 🟢 低 | 局部修复 | 可直接合入 |
| #2 | 🟢 低 | strand 是 Asio 原生机制 | 逐一验证 post 目标 |
| #3 | 🟡 中 | 改动影响所有 async callback | 统一规则，逐文件替换 |
| #4 | 🔴 高 | 核心 I/O 路径重写 | 分步：先 write → 再读 MBAP → 再读 PDU，每步全量测试 |
| #5 | 🟡 中 | 每个 promise 必须恰好 set_value 一次 | 单元测试覆盖所有退出路径 |
| #6 | 🟡 中 | 状态转换边界可能遗漏 | enum class + exhaustive switch |
| #7 | 🟢 低 | 纯增量变更 | deprecated 逐步迁移 |
| #8 | 🟢 低 | 独立逻辑 | 后端纯函数，易测试 |
| #9 | 🟢 低 | 语义定义 | 文档化 + 测试覆盖 |
| #10 | 🟢 低 | 纯增量 | 单行变更 |
| #11 | 🟢 低 | 纯替换 | 脚本辅助 |
| #12 | 🔵 远 | 长期 | 单独分支 |

---

## 附录 A：当前代码快速定位索引

| 关键代码 | 文件 | 行号区域（约） |
|----------|------|---------------|
| `m_socketMutex` 声明（待移除） | `AsioModbusTcpClient.h` | 末行 |
| `executeTransaction()` | `AsioModbusTcpClient.cpp` | ~L220-L380 |
| `&timedOut` 悬空引用 | `AsioModbusTcpClient.cpp` | executeTransaction lambda 内部 |
| `startReconnect()` | `AsioModbusTcpClient.cpp` | ~L400 |
| `scheduleReconnect()` | `AsioModbusTcpClient.cpp` | ~L440 |
| `configureSocket()` | `AsioMod
