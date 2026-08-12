# application_vnext —— 应用层（基于 domain_vnext 的新实现）

> 状态：**P5 app 已完成**（SystemManager / UseCase 迁移到 domain_vnext）。
> 依据：《docs/refactor/domain_vnext/Domain层重构设计——domain_vnext.md》§8 P5。

## 目标

把旧 `application/` 的 `SystemManager` / UseCase 迁移到 `domain_vnext`，作为
`application_vnext` 独立 STATIC 库与旧层并行；P6 清理前旧层保留，互不侵入。

## 模块边界

- 依赖方向：`application_vnext -> domain_vnext -> plc_vnext::contracts`。
- **不 include** 旧 `domain/*`、`infrastructure/ISystemDriver`、Qt、Modbus。
- 驱动写通道经 `domain_vnext::gateway::IPlcDriver`（面向抽象）；测试注入
  `plc_vnext::fake::FakePlcRuntimeGateway`。

## 交付清单

| 文件 | 内容 |
| --- | --- |
| `SystemManagerVnext.h` | 组合根门面：boot / poll / 单轴用例 / 急停 / 龙门；完整链路「find → 领域意图 submit → Outbox drain → CommandMapper 映射 → 使能入口路由 → writeAxis」 |
| `PlcRuntimeDriverAdapter.h` | 实现 `gateway::IPlcDriver`，委托 `plc_vnext::IPlcRuntimeGateway` |
| `AppVnextError.h` | 应用层错误聚合（monostate=成功；含未 boot/未注册/状态拒绝/映射不支持/通讯失败/安全拒绝/龙门拒绝） |
| `tests/test_system_manager_vnext.cpp` | 集成测试：boot/poll、使能入口路由、点动、moveAbs 先后序、错误聚合、急停五态、龙门事务 |

## 单轴用例链路（§5.2）

```text
SystemManagerVnext.<enableAxis/jog/moveAbs/...>
  -> AxisSystem.find({group, fn})
  -> AxisStateMachine.submit(意图)        // 系统锁定/龙门同步/轴忙校验
  -> CommandOutbox.drain()                // 参数去重 + 运动保序 + 脉冲
  -> CommandMapper.mapAxis                // kind/value/level 透传
  -> effectiveEnableSlot                  // §4.6a 使能入口路由
  -> gateway::IPlcDriver.writeAxis(slot, cmd)
```

## 构建与测试

```bash
cmake --build build --target application_vnext_tests
ctest --test-dir build -R "^application_vnext\\."
```

> 与 domain_vnext 同约定：`ctest -R application_vnext_tests` 匹配可执行文件名
> 而非被发现的测试名，请用 `^application_vnext\.` 正则验证发现情况。
