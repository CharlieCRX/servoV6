# AsioModbusTcpClient 当前问题分析

## 一、当前状态评估

当前 `AsioModbusTcpClient` 已经完成：

- TCP 异步连接
- Modbus TCP MBAP 构建与解析
- Transaction ID 管理
- start/stop 生命周期管理
- reconnect 基础框架
- 多次 start/stop 稳定性
- 基础事务测试
- 日志体系

目前已经属于：

```text
“可工作的工业通讯框架雏形”
```

而不再是简单 socket demo。

------

# 二、当前已经解决的问题

## 1. io_context 提前退出问题

### 原问题

```text
io_context.run() 在 connect 完成后无任务退出
```

导致：

```text
asio::post() 无人处理
```

最终：

```text
transaction timeout
```

------

## 当前解决方案

引入：

```cpp
work_guard
```

并采用：

```text
自然 drain
```

替代：

```cpp
io_context.stop()
```

------

## 当前状态

✅ 已解决

------

# 三、当前架构中仍存在的问题

------

# 1. 最大问题：同步阻塞 I/O 运行在 io_context 线程

------

## 当前实现

当前 transaction 内部：

```cpp
asio::write(...)
asio::read(...)
```

是在：

```text
io_context worker thread
```

中同步执行。

------

## 风险

这是当前系统最大架构风险。

因为：

```text
一个 read 卡死
=
整个 io_context 卡死
```

------

## 会导致：

### 1. reconnect 失效

```text
timer 无法执行
```

------

### 2. stop() 卡死

```text
worker.join() 无法返回
```

------

### 3. timeout 机制失效

当前：

```cpp
future.wait_for()
```

只是：

```text
业务线程超时
```

并不是：

```text
socket I/O 超时
```

真正：

```cpp
asio::read()
```

仍可能永久阻塞。

------

## 工业现场风险

以下情况都可能触发：

```text
PLC 卡死
交换机丢包
TCP 半断开
RST 丢失
网络抖动
```

最终：

```text
read 永不返回
```

------

## 当前优先级

🔥🔥🔥 最高优先级问题

------

# 2. 缺少真正 socket 级 timeout

------

## 当前 timeout 本质

当前：

```cpp
future.wait_for(timeout)
```

属于：

```text
调用层 timeout
```

不是：

```text
socket timeout
```

------

## 问题

即使：

```text
wait_for timeout
```

后台：

```cpp
asio::read()
```

仍可能卡住。

------

## 正确工业方案

应使用：

```cpp
steady_timer
async_wait
socket.cancel()
```

形成：

```text
真正 I/O 超时
```

------

## 当前优先级

🔥🔥🔥 高优先级

------

# 3. transaction 缺少串行化机制

------

## 当前问题

目前：

```cpp
executeTransaction()
```

理论上允许：

```text
多个线程同时调用
```

------

## 风险

Modbus TCP 虽然支持：

```text
Transaction ID
```

但：

```text
大量 PLC 实际只支持串行事务
```

如果多个 transaction 并发：

```text
write/read 交叉
```

可能导致：

```text
响应错乱
协议污染
transaction mismatch
```

------

## 正确方案

应增加：

```text
transaction queue
```

例如：

```cpp
std::queue<PendingTransaction>
```

形成：

```text
单连接串行事务模型
```

------

## 当前优先级

🔥🔥 中高优先级

------

# 4. reconnect 状态机仍不完整

------

## 当前 reconnect

目前仅验证：

```text
connect failure
```

------

## 缺少：

### 运行时断线恢复

例如：

```text
PLC 运行中断电
网线拔掉
交换机断开
```

------

## 当前问题

当前：

```cpp
asio::read/write error
```

后：

```text
未完全形成 reconnect 状态闭环
```

------

## 正确方案

应形成：

```text
Connected
↓
Read Error
↓
Disconnected
↓
Reconnect Timer
↓
Connecting
↓
Connected
```

完整状态机。

------

## 当前优先级

🔥🔥 中优先级

------

# 5. 当前缺少通信状态模型

------

## 当前

```cpp
isConnected()
```

仅代表：

```text
TCP socket connected
```

------

## 风险

工业场景：

```text
TCP 未断
PLC 已死
```

仍可能：

```text
isConnected == true
```

------

## 正确方案

建议：

```cpp
enum class ConnectionState
{
    Disconnected,
    Connecting,
    Connected,
    Operational,
    Fault
};
```

------

## 当前优先级

🔥 中优先级

------

# 6. 缺少 async transaction 模型

------

## 当前

transaction：

```text
同步 write/read
```

------

## 长期问题

后续：

```text
多轴
高频采集
心跳
周期轮询
```

会导致：

```text
worker thread 长时间阻塞
```

------

## 正确方向

未来应迁移：

```cpp
async_write
async_read
```

实现：

```text
真正异步 transaction state machine
```

------

## 当前优先级

⚠️ 长期架构优化

------

# 7. 日志系统尚未工程化

------

## 当前日志

目前：

```text
std::cout
```

直接输出。

------

## 问题

未来：

```text
多轴
高频轮询
```

日志会：

```text
完全不可读
```

------

## 正确方案

建议：

```cpp
LogLevel
Logger
Sink
```

支持：

```text
TRACE
DEBUG
INFO
WARN
ERROR
```

以及：

```text
文件日志
控制台日志
环形缓存
```

------

## 当前优先级

⚠️ 中长期优化

------

# 四、当前测试覆盖情况

当前已经覆盖：

| 测试项                  | 状态 |
| ----------------------- | ---- |
| start/stop              | ✅    |
| reconnect after restart | ✅    |
| transaction             | ✅    |
| MBAP parsing            | ✅    |
| transaction ID          | ✅    |
| multiple start/stop     | ✅    |
| timer recovery          | ✅    |

------

# 五、当前缺少的重要测试

------

## 1. PLC 运行时断线恢复

例如：

```text
运行中关闭 diagslave
```

验证：

```text
自动 reconnect
```

------

## 2. transaction timeout recovery

验证：

```text
超时后 socket 是否恢复
```

------

## 3. 并发 transaction

验证：

```text
多个线程 executeTransaction
```

------

## 4. reconnect 后 transaction 是否恢复

验证：

```text
断线恢复后继续读寄存器
```

------

# 六、建议的下一阶段路线

------

# Phase 1（已完成）

✅ connect
✅ transaction
✅ lifecycle
✅ reconnect 基础
✅ logging

------

# Phase 2（推荐立即开始）

🔥 socket 级 timeout

目标：

```text
彻底消除永久阻塞
```

------

# Phase 3

🔥 transaction queue

目标：

```text
事务串行化
```

------

# Phase 4

🔥 reconnect state machine

目标：

```text
运行时断线恢复
```

------

# Phase 5

⚠️ async transaction

目标：

```text
真正异步 pipeline
```

------

# 七、当前整体评价

当前 `AsioModbusTcpClient`：

已经具备：

```text
工业通讯框架基础能力
```

但：

当前仍属于：

```text
“单 worker + 同步 transaction”
```

模型。

下一阶段核心任务：

应从：

```text
功能正确性
```

转向：

```text
工业健壮性
```

尤其是：

```text
timeout
reconnect
transaction queue
async state machine
```

这几个方向。