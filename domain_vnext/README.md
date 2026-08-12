# domain_vnext —— PLC 领域层（重构新实现）

> 状态：**P1 model 已完成**、**P2 state 已完成**、**P3 system 已完成**（2026-08-12）。
> 依据：《docs/refactor/domain_vnext/Domain层重构设计——domain_vnext.md》。

## 目标

仿照 `plc_vnext`（已替换旧 `infrastructure/plc`）的做法，以独立 STATIC 库
`domain_vnext`（命名空间 `domain_vnext`）替换旧 `domain` 层；纯领域 DTO +
状态机 + TDD 驱动，与旧层并行迁移。

## 模块边界（三条铁律）

1. 独立 STATIC 库 `domain_vnext`，不 include 旧 `domain/*`、
   `infrastructure/ISystemDriver`、Qt、Modbus；只有纯领域状态机与 DTO。
2. 以 `plc_vnext::contracts` 作为基础设施边界（纯 DTO，无 I/O、无业务）。
   依赖方向：domain → plc_vnext contracts。
3. TDD 驱动、按 Phase 递进（P1 model → P2 state → P3 system → P4 link →
   P5 app → P6 清理）。

## 目录职责

| 目录 | 职责 |
| --- | --- |
| `model/` | 纯值对象 / 领域 DTO（AxisKey/AxisFunction/AxisState/AxisParameterSet/AxisCommand/GantryStatus/GantryParam/SafetyState） |
| `state/` | 状态机（CommandOutbox/AxisStateMachine/SafetyStateMachine/GantryCouplingStateMachine） |
| `system/` | 组合根（P3：AxisRegistry/GroupModel/AxisSystem/SystemBoot） |
| `command/` | 命令产出边界（P4 起） |
| `gateway/` | 领域依赖的驱动抽象（P4 起） |
| `tests/` | TDD（target: `domain_vnext_tests`） |

## P1 model 交付清单

| 文件 | 内容 |
| --- | --- |
| `model/AxisFunction.h` | X/X1/X2/Y/Z/R + Role 下标映射（Role[0..5]） |
| `model/AxisKey.h` | (PlcGroupIndex, AxisFunction) 领域身份 |
| `model/AxisState.h` | MotionState / LimitState / SoftLimitControl（D128/D144/D1228 解码） |
| `model/AxisParameterSet.h` | 13 项设置快照（前 7 反馈 + 后 6 参数区）+ trusted |
| `model/AxisCommand.h` | 14 触发 + 参数写枚举 + AxisCommand + AxisCommandEnvelope |
| `model/GantryStatus.h` | GantryCouplingState 映射 + GantryStatusModel |
| `model/GantryParam.h` | GantryParamModel（纯配置，只读） |
| `model/SafetyState.h` | 急停五态 |

## P2 state 交付清单

| 文件 | 内容 |
| --- | --- |
| `state/CommandOutbox.h` | 批量意图槽位：参数按字段去重 + 运动保序（seq）+ 脉冲队列；drain 一次取走 |
| `state/AxisStateMachine.h` | 单轴「意图->校验->命令入 Outbox」：系统锁定/龙门同步/轴忙校验，四接口解耦 |
| `state/SafetyStateMachine.h` | 急停五态（M224/M225）+ EStopCommand + SafetyRejection |
| `state/GantryCouplingStateMachine.h` | 龙门联动状态机：GantryStatus 映射 + RequestSeq 事务 + 多条件闭环 |

## P3 system 交付清单

| 文件 | 内容 |
| --- | --- |
| `system/AxisRegistry.h` | 全局 16 槽位 -> `Axis` 实体；`Axis` = 统一单轴（key/slot/角色）+ P2 状态机 + CommandOutbox |
| `system/GroupModel.h` | 分组功能视图 `(AxisFunction->Axis&)` + 龙门控制器 + HmiVisible；`isReady()` 有效且未降级才可开放控制 |
| `system/AxisSystem.h` | 组合根：注册表 + `GroupModel[2]` + 全局急停；`find(AxisKey)/findBySlot` |
| `system/SystemBoot.h` | `TopologySnapshot -> AxisSystem` 动态建轴 / A/B 分组 / HmiVisible 判定 / 重复或非法配置降级锁定 |
| `tests/system/test_system_boot.cpp` | P3 验证点：建轴、分组、HmiVisible、降级锁定 |

## 构建与测试（独立通道）

根 `CMakeLists.txt` 在非 Android 分支引入 `add_subdirectory(domain_vnext)`。

```bash
cmake --build build --target domain_vnext_tests
ctest --test-dir build -R "^domain_vnext\."
ctest --test-dir build -N
```

> 注意：`ctest -R domain_vnext_tests` 匹配的是可执行文件名而非被发现的测试名，
> 不能证明测试真的被发现；请始终用 `^domain_vnext\.` 正则验证。

### 测试文件登记约定

不使用 `file(GLOB_RECURSE ...)` 自动收集。每个 Phase 在
`domain_vnext/tests/CMakeLists.txt` 中**显式追加**新增测试文件。
