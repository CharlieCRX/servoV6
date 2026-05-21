# 急停逻辑重构方案 ---- pending command 消费与 tick loop 统一

> **文档版本**: v1 (2026-05-18)
>
> **核心原则**: 不修改 `ISystemDriver` 接口，`pollFeedback()` 已完整覆盖反馈注入，只需补充 pending command 消费逻辑。

---

## 1. 问题诊断

### 1.1 当前急停数据流（残缺）

```
用户点击急停按钮
  -> QtAxisViewModel::emergencyStop()
    -> AxisViewModelCore::emergencyStop()
      -> EmergencyStopUseCase::execute(manager, groupName)
        -> ctx->emergencyStopController().requestEmergencyStop()
          -> m_pending_intent = EmergencyStopCommand{ true };  // ⚠️ 生成了命令但没有消费！
          -> m_state = EmergencyStopping
```

**问题**: `EmergencyStopController::requestEmergencyStop()` 将命令写入 `m_pending_intent`，但没有任何地方调用 `popPendingCommand()` 并通过 `ISystemDriver::send()` 发送到 PLC。急停命令永远不会到达硬件。

### 1.2 反馈通路已完整

`FakeAxisDriver::pollFeedback()` 内部已正确实现急停反馈注入：

```cpp
void pollFeedback(SystemContext& ctx) override {
    m_plc.tick(10);

    // ✅ 急停反馈注入 -- 已正确实现
    ctx.emergencyStopController().applyFeedback(m_plc.getEmergencyStopFeedback());

    // ✅ 龙门反馈注入
    GantryFeedback gf = m_plc.getGantryFeedback();
    ctx.gantryPowerController().applyFeedback(gf);
    ctx.gantryCouplingController().applyFeedback(gf);

    // ✅ 轴反馈注入
    for (auto axisId : ALL_AXIS_IDS) {
        auto* axis = ctx.tryGetAxis...;
        axis->applyFeedback(m_plc.getFeedback(axisId));
    }
}
```

**反馈通路无需修改。**

---

## 2. 重构方案

### 2.1 目标数据流

```
┌─────────────────────────────────────────────────────────────────┐
│                      main.cpp Tick Loop (10ms)                   │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  for each group:                                                 │
│    ┌──────────────────────────────────────────────────────┐     │
│    │ 1. pollFeedback()                                    │     │
│    │    ├─ inject axis feedback                           │     │
│    │    ├─ inject gantry feedback                         │     │
│    │    └─ inject emergencyStop feedback                  │     │
│    │                                                      │     │
│    │ 2. consume pending command ←── 🆕 新增              │     │
│    │    ├─ poll estopCtrl.hasPendingCommand()             │     │
│    │    ├─ pop command -> drv->send(cmd)                   │     │
│    │    └─ log delivery result                            │     │
│    └──────────────────────────────────────────────────────┘     │
│                                                                  │
│  for each ViewModel:                                             │
│    vm->tick()  ← 同步 UI 状态                                    │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘

用户操作 -> EmergencyStopUseCase -> requestEmergencyStop()
  -> m_pending_intent = EmergencyStopCommand{true}
  -> m_state = EmergencyStopping
         │
         ▼ (下一个 tick)
  tick loop 消费 pending command -> drv->send(cmd) -> PLC 寄存器
         │
         ▼ (后续 tick)
  pollFeedback -> drv->pollFeedback()
    -> ctx.emergencyStopController().applyFeedback(plcEmergencyStopped=true)
      -> m_state = EmergencyStopped  ✅ 急停完成
```

### 2.2 仅需修改 main.cpp tick loop

在现有 tick loop 中，`pollFeedback()` 之后新增 pending command 消费逻辑：

```cpp
QObject::connect(&systemClock, &QTimer::timeout, [&]() {
    for (const auto& groupName : manager.groupNames()) {
        SystemContext* ctx = nullptr;
        ContextRejection r;
        if (manager.tryGetGroup(groupName, ctx, r) && ctx) {
            auto* drv = ctx->driver();
            if (!drv) continue;

            // 6a. 反馈注入（轴 + 龙门 + 急停）
            drv->pollFeedback(*ctx);

            // 6b. 🆕 消费 EmergencyStopController 产生的 pending command
            auto& estopCtrl = ctx->emergencyStopController();
            if (estopCtrl.hasPendingCommand()) {
                auto commResult = drv->send(estopCtrl.popPendingCommand());
                if (!commResult.ok()) {
                    LOG_WARN(LogLayer::APP, "System",
                        "[" + groupName + "] EmergencyStop command delivery failed: "
                        + commResult.diagnostic);
                }
            }
        }
    }

    for (auto* vm : allViewModels) {
        vm->tick();
    }
});
```

### 2.3 不需要修改的文件

| 文件 | 原因 |
|------|------|
| `ISystemDriver.h` | `pollFeedback()` 已覆盖急停反馈注入 |
| `FakeAxisDriver.h` | `pollFeedback()` 已实现急停反馈注入 |
| `EmergencyStopController.h` | `hasPendingCommand()` / `popPendingCommand()` 已实现 |
| `SystemContext.h` | `emergencyStopController()` 已暴露引用 |
| `SystemManager.h` | `groupNames()` 已实现 |

---

## 3. 设计决策

### 3.1 为什么不在 pollFeedback 内部消费 pending command？

| 位置 | 优点 | 缺点 |
|------|------|------|
| `pollFeedback` 内部 | 封装性好 | pollFeedback 语义上只负责"读"，不是"写"；引入发送逻辑破坏单一职责 |
| **tick loop 中**（✅ 选择） | 反馈与命令对称排列；驱动接口保持纯正；消费失败可直接日志诊断 | main.cpp 多一行逻辑 |

### 3.2 为什么不在 UseCase 中直接发送？

UseCase 是无状态的值语义对象，不持有 Driver 引用。它只负责"生成意图"，命令的物理发送延迟到 tick loop 统一处理。这符合 **Command/Feedback 双通路** 架构设计。

### 3.3 失败处理

`drv->send()` 返回 `CommunicationResult`。若失败（如网络断开），仅记录 `LOG_WARN`，不重试。`EmergencyStopController` 保持在 `EmergencyStopping` 状态，下一个 tick 会重新尝试发送（因为 `popPendingCommand()` 后会清空 intent，所以实际上只会发送一次）。

> **注意**: 当前实现中 `popPendingCommand()` 消费后即清空。若需要"发送失败后重试"语义，需修改 `EmergencyStopController` 为保留 intent 直到 `send()` 成功。这超出了本次重构范围。

---

## 4. 实施步骤

| 步骤 | 内容 | 文件 |
|------|------|------|
| Step 1 | 在 tick loop 中新增 pending command 消费逻辑 | `main.cpp` |
| Step 2 | 编译验证 | `cmake --build build` |
| Step 3 | 运行测试 | `ctest --test-dir build` |

---

## 5. 完整变更 Diff

```diff
 // main.cpp tick loop
 QObject::connect(&systemClock, &QTimer::timeout, [&]() {
     for (const auto& groupName : manager.groupNames()) {
         SystemContext* ctx = nullptr;
         ContextRejection r;
         if (manager.tryGetGroup(groupName, ctx, r) && ctx) {
-            if (auto* drv = ctx->driver()) {
-                drv->pollFeedback(*ctx);
-            }
+            auto* drv = ctx->driver();
+            if (!drv) continue;
+
+            // 6a. 反馈注入
+            drv->pollFeedback(*ctx);
+
+            // 6b. 消费急停 pending command
+            auto& estopCtrl = ctx->emergencyStopController();
+            if (estopCtrl.hasPendingCommand()) {
+                auto commResult = drv->send(estopCtrl.popPendingCommand());
+                if (!commResult.ok()) {
+                    LOG_WARN(LogLayer::APP, "System",
+                        "[" + groupName + "] EmergencyStop command delivery failed: "
+                        + commResult.diagnostic);
+                }
+            }
         }
     }
     for (auto* vm : allViewModels) {
         vm->tick();
     }
 });
```

---

## 6. 验证清单

- [ ] 点击 QML 急停按钮 -> `EmergencyStopController` 状态进入 `EmergencyStopping`
- [ ] tick loop 消费 pending command -> `FakeAxisDriver::send()` 被调用
- [ ] FakePLC 设置急停状态 -> 后续 tick 的 pollFeedback 注入 `plcEmergencyStopped=true`
- [ ] `EmergencyStopController` 状态跃迁到 `EmergencyStopped`
- [ ] ViewModel tick 检测到系统锁定 -> UI 正确禁用操作
- [ ] 日志中出现 command delivery 成功/失败的 TRACE 记录
