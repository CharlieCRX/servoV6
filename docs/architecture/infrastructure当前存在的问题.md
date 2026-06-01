## Infrastructure 层数据流转过程与 pollFeedback 获取 PLC 轴状态分析

---

## 一、Infrastructure 层整体架构回顾

根据架构文档和代码，infrastructure 层分为以下层级：

```
┌────────────────────────────────────────────┐
│  ISystemDriver (抽象接口)                    │
│  ├── send(cmd) → CommunicationResult       │  ← 命令下行通路
│  └── pollFeedback(ctx)                     │  ← 反馈上行通路
├────────────────────────────────────────────┤
│  ModbusSystemDriver (具体实现)              │
│  ├── send() 分派: CommandFormatter →       │
│  │   RegisterCodec::encode → m_device->    │
│  │   writeBool/writeFloat                  │
│  ├── pollFeedback() [STUB - 未实现反馈读取] │
│  └── PendingEdge 队列管理                  │
├────────────────────────────────────────────┤
│ PlcDevice (L1 驱动集成层)                   │
│  ├── updateSnapshot(PlcSnapshot)  ← 写入   │
│  ├── readValue(reg) → PlcValue    ← 解码   │
│  ├── writeBool / writeFloat       ← 编码   │
│  └── isStateTrusted()                      │
├────────────────────────────────────────────┤
│ L2-L3 Protocol Runtime                      │
│  ├── PlcPoller::prepare() → PollRequest    │
│  ├── PlcPoller::assemble() → PlcSnapshot   │
│  └── RegisterCodec::decode() → PlcValue    │
├────────────────────────────────────────────┤
│ AsioModbusTcpClient (L3 传输层)             │
│  ├── readCoils()                           │
│  └── readHoldingRegisters()                │
└────────────────────────────────────────────┘
```

---

## 二、当前数据流转的完整链路（基于 test_real_feedback_readonly.cpp）

test_real_feedback_readonly.cpp 展示了**绕过 ModbusSystemDriver** 直接操作底层协议的完整读取管线。这是当前唯一能获取 PLC 反馈的完整通路：

### Phase 1: 声明期 — 寄存器登记
```cpp
RegisterRegistry registry;
registry.addAll({                       // ← 开发者声明式配置
    plc::reg::x_axis::feedback::STATE,          // D100, Int16
    plc::reg::x_axis::feedback::ABS_POSITION,   // D120, Float32, CDAB序
    plc::reg::x_axis::feedback::MOVE_DONE,     // M100, Bool (Coil)
    // ... 共 35 个 Feedback 寄存器（X/Y/Z 三轴）
});
```

### Phase 2: 初始化期 — 地址打包
```cpp
PlcPoller poller(registry);
// 构造时自动分析:
//   Coil 地址 {100,101,102,104,105,106,...} 
//     → 排序去重 → AddressPacker::pack → [Range(100,7), Range(110,5), ...]
//   HoldingReg 地址 {100,120,121,122,123,...}
//     → 展开 Float32 占2字 → 排序去重 → [Range(100,1), Range(120,2), Range(122,2), ...]
```

### Phase 3: 运行时 — 网络 I/O（操作员手动编写）
```cpp
auto req = poller.prepare();           // 生成 PollRequest {coilRequests[], wordRequests[]}
for (auto& cr : req.coilRequests) {
    client->readCoils(cr.range.startAddr, cr.range.count, payload);    // FC01
}
for (auto& wr : req.wordRequests) {
    client->readHoldingRegisters(wr.range.startAddr, wr.range.count, payload); // FC03
}
```

### Phase 4: 快照拼装
```cpp
auto snapshot = poller.assemble(coilResponses, wordResponses, timestamp);
// → PlcSnapshot {
//     bits: RawBitSnapshot(startAddr=100, bitCount=7, payload=[0x03, ...])
//     words: RawWordSnapshot(startAddr=100, payload=[...])
//     complete: true/false     ← 可信度标记
//     timestamp: 12345
//   }
```

### Phase 5: PlcDevice 绑定快照
```cpp
PlcDevice device(profile);              // INOVANCE_PROFILE: BigEndian + LowWordFirst
device.updateSnapshot(std::move(snapshot));
```

### Phase 6: 解码与消费
```cpp
// 读取 X 轴绝对位置
PlcValue absPosVal = device.readValue(plc::reg::x_axis::feedback::ABS_POSITION);
// 内部调用链:
//   RegisterCodec::decode(ABS_POSITION, snapshot, INOVANCE_PROFILE)
//     → resolvePolicy() → EndianPolicy{BigEndian, LowWordFirst}
//     → snapshot.words.getWords(120, 2) → span<uint16_t>{0x0000, 0x4316}
//     → decodeFloat([0x0000, 0x4316], CDAB) → 150.0f
//     → PlcValue{150.0f}

float absPos = getValue<float>(absPosVal);    // 150.0

// 读取 X 轴 STATE
PlcValue stateVal = device.readValue(plc::reg::x_axis::feedback::STATE);
// → snapshot.words.getWords(100, 1) → span<uint16_t>{0x0001}
// → decodeInt16 → PlcValue{(int16_t)1}
```

### Phase 7: 状态推导（在测试中由 AxisStateDeriver 完成）
```cpp
int16_t state = getValue<int16_t>(device.readValue(regFbState(X)));
int16_t alarmCode = getValue<int16_t>(device.readValue(regFbAlarmCode(X)));
bool absMoving = getValue<bool>(device.readValue(regFbAbsMoving(X)));
bool relMoving = getValue<bool>(device.readValue(regFbRelMoving(X)));
bool jogging = getValue<bool>(device.readValue(regFbJogging(X)));

AxisState axisState = deriveAxisState(state, alarmCode, absMoving, relMoving, jogging);
// 优先级链: Error > Disabled > MovingAbsolute > MovingRelative > Jogging > Idle
```

---

## 三、ModbusSystemDriver::pollFeedback() 的当前实现（STUB）

```cpp
// infrastructure/plc/ModbusSystemDriver.h: pollFeedback()
inline void ModbusSystemDriver::pollFeedback(SystemContext& ctx) {
    // TDD 阶段 5: 先处理到期的 EdgeTrigger OFF 脉冲
    servicePendingEdgeTriggers();

    // Sprint 1: 数据可信度门禁 — 不可信快照直接返回
    if (m_device && !m_device->isStateTrusted()) {
        return;
    }

    (void)ctx; // 后续 Sprint 将使用 ctx 进行轴/系统反馈注入
}
```

**关键断点**：第 5 阶段的实现只做了：
1. ✅ 服务边沿触发 OFF 脉冲
2. ✅ 可信度门禁
3. ❌ **没有执行 PLC 读取** — 没有调用 `PlcPoller::prepare()` / `client->readCoils()` / `client->readHoldingRegisters()`
4. ❌ **没有调用 `PlcPoller::assemble()`** 产生产出 PlcSnapshot
5. ❌ **没有调用 `device->updateSnapshot()`** 更新 PlcDevice 内部快照
6. ❌ **没有解码任何寄存器值并注入 Axis 实体**

---

## 四、如何让 pollFeedback 获取 PLC 轴状态信息

### 4.1 缺失的组件分析

pollFeedback 需要补齐以下组件才能完整工作：

| 缺失组件                   | 当前状态                    | 需要的动作                                               |
| -------------------------- | --------------------------- | -------------------------------------------------------- |
| `AsioModbusTcpClient`      | ModbusSystemDriver 没有持有 | 在构造/初始化时注入 TCP 客户端                           |
| `PlcPoller`                | ModbusSystemDriver 没有持有 | 在构造时基于完整 RegisterRegistry 构建                   |
| 反馈读取循环               | 不存在                      | pollFeedback 内执行 FC01/FC03 读取                       |
| `PlcPoller::assemble()`    | 不存在                      | pollFeedback 内调用                                      |
| `device->updateSnapshot()` | 不存在                      | pollFeedback 内调用                                      |
| 反馈解码与注入             | 不存在                      | 解码各轴 STATE/位置/运动标志，调用 axis->applyFeedback() |

### 4.2 需要在 ModbusSystemDriver 中新增的成员

```cpp
class ModbusSystemDriver : public ISystemDriver {
    // ... 现有成员 ...

    // 新增 1: PLC 反馈轮询器（基于全量寄存器表构造）
    protocol::PlcPoller m_poller;

    // 新增 2: Modbus TCP 客户端（网络 I/O 层）
    std::unique_ptr<AsioModbusTcpClient> m_client;

    // 新增 3: 全量寄存器注册表（需要用完整的 RegisterInfo 构造 m_poller）
    protocol::RegisterRegistry m_fullRegistry;
};
```

### 4.3 pollFeedback() 需要实现的完整流程

```C++
pollFeedback(SystemContext& ctx):
  1. servicePendingEdgeTriggers()           // ✅ 已实现
  2. 可信度门禁检查                            // ✅ 已实现

  // ===== 新增：PLC 反馈读取管线 =====
  3. PollRequest req = m_poller.prepare()
     → 生成 coilRequests[] + wordRequests[]

  4. 执行 FC01 批量读取:
     for (auto& cr : req.coilRequests)
       m_client->readCoils(cr.startAddr, cr.count, payload)
     → 失败任一条则记录日志，产出 untrusted snapshot

  5. 执行 FC03 批量读取:
     for (auto& wr : req.wordRequests)
       m_client->readHoldingRegisters(wr.startAddr, wr.count, payload)

  6. PlcSnapshot snapshot = m_poller.assemble(coilResponses, wordResponses, now())
     → RawBitSnapshot + RawWordSnapshot + complete + timestamp

  7. m_device->updateSnapshot(std::move(snapshot))
     → PlcDevice 内部持有最新快照供 send() 时查询

  // ===== 新增：反馈注入到领域实体 =====
  8. for each AxisId in {X, X1, X2, Y, Z, R}:
       a. ContextRejection reason; Axis* axis;
       b. if ctx.tryReadAxis(id, axis, reason):  // 绕过安全锁定，只读
            // 8.1 读取 STATE → Int16
            PlcValue stateVal = m_device->readValue(regFbState(id));
            int16_t stateCode = getValue<int16_t>(stateVal);

            // 8.2 读取 ALARM_CODE → Int16
            PlcValue alarmVal = m_device->readValue(regFbAlarmCode(id));
            int16_t alarmCode = getValue<int16_t>(alarmVal);

            // 8.3 读取运动标志 Coils → Bool
            bool absMoving = getValue<bool>(m_device->readValue(regFbAbsMoving(id)));
            bool relMoving = getValue<bool>(m_device->readValue(regFbRelMoving(id)));
            bool jogging   = getValue<bool>(m_device->readValue(regFbJogging(id)));

            // 8.4 多信号融合 → AxisState
            AxisState derivedState = deriveAxisState(stateCode, alarmCode,
                                                     absMoving, relMoving, jogging);

            // 8.5 读取位置 → Float32
            float absPos = getValue<float>(m_device->readValue(regFbAbsPos(id)));
            float relPos = getValue<float>(m_device->readValue(regFbRelPos(id)));

            // 8.6 注入 Axis 实体（Axis 需要提供 applyFeedback 接口）
            axis->applyFeedback(derivedState, absPos, relPos);

  9. 急停状态注入:
       bool estopped = getValue<bool>(m_device->readValue(regFbEmergencyStopActive()));
       ctx.emergencyStopController().applyFeedback(estopped);

  10. 龙门状态注入:
       int16_t gantryErr = getValue<int16_t>(m_device->readValue(regFbGantryErrorCode()));
       bool linkageState = getValue<bool>(m_device->readValue(regFbLinkageState()));
       // 注入 GantryCouplingController / GantryPowerController
```

### 4.4 Axis 实体需要补充的 applyFeedback 接口

当前 `Axis` 实体需要扩展一个专门供 infrastructure 层调用的反馈注入方法：

```cpp
// domain/entity/Axis.h 中需要新增:
class Axis {
public:
    // ... 现有接口 ...

    /// @brief 接受 infrastructure 层注入的 PLC 反馈快照
    /// @param state  多信号融合后的轴状态
    /// @param absPos 绝对位置 (mm)
    /// @param relPos 相对位置 (mm)
    /// @param rawStateCode 原始 D100 STATE 值（用于诊断）
    void applyPlcFeedback(AxisState state, float absPos, float relPos,
                          int16_t rawStateCode = 0);

private:
    // 从 PLC 反馈推导的状态（与领域层自身状态机区分）
    AxisState m_plcDerivedState = AxisState::Disabled;
    float m_plcAbsPosition = 0.0f;
    float m_plcRelPosition = 0.0f;
    int16_t m_plcRawStateCode = 0;
};
```

### 4.5 与现有架构的衔接关系

```
                 主循环 (tickLoop / main.cpp)
                       │
          ┌────────────┼────────────┐
          ▼            ▼            ▼
  SystemManager   ISystemDriver   UI/ViewModel
  (UseCase级)    .pollFeedback()  (读取 Axis 状态)
                       │
          ┌────────────┼────────────────┐
          ▼            ▼                ▼
   PendingEdge     PlcPoller      RegisterCodec
   服务写入OFF    prepare/assemble    decode (每个轴)
                       │
          ┌────────────┼────────────────┐
          ▼            ▼                ▼
   AsioModbusTcpClient           AxisStateDeriver
   readCoils / readHoldingRegs   deriveAxisState()
                       │
                       ▼
                  PlcDevice
               updateSnapshot()
                       │
          ┌────────────┼────────────────┐
          ▼            ▼                ▼
     SystemContext   Axis             EmergencyStop
     tryReadAxis()  applyPlcFeedback()  Controller
                    存储位置/状态        applyFeedback()
```

---

## 五、总结

**当前状态**：
- `test_real_feedback_readonly.cpp` 证明了底层 Protocol Runtime 管线（PlcPoller → AsioModbusTcpClient → PlcDevice → RegisterCodec）完整可用
- 但这个管线目前**只在测试中手动编排**，尚未集成到 `ModbusSystemDriver::pollFeedback()` 中

**核心差距**：
1. `ModbusSystemDriver` 缺少 `AsioModbusTcpClient` 和 `PlcPoller` 成员
2. `pollFeedback()` 缺少 PLC 读取 → 快照拼装 → 快照注入的完整实现
3. `Axis` 实体缺少接受 PLC 反馈注入的 `applyPlcFeedback()` 接口
4. 缺少 `AxisStateDeriver::deriveAxisState()` 在 pollFeedback 内部的集成调用
5. 反馈读取使用的寄存器表需要是**全量**的（包含 Feedback 组所有寄存器），而非仅 Feedback 子集

**实现优先级建议**：
1. **P0**: 在 `ModbusSystemDriver` 中注入 `AsioModbusTcpClient` 和 `PlcPoller`
2. **P0**: 实现 `pollFeedback()` 内部的 PLC 读取管线（prepare → FC01/FC03 → assemble → updateSnapshot）
3. **P1**: 在 `Axis` 实体中新增 `applyPlcFeedback()` 接口
4. **P1**: 在 `pollFeedback()` 末尾添加反馈解码与注入逻辑（遍历 AxisId → decode → deriveAxisState → applyPlcFeedback）
5. **P2**: 集成急停/龙门状态注入（EmergencyStopController / GantryCouplingController）