# PLC 龙门联动配置与上位机操作指南

> 版本状态：基于 2026-08-10 已完成并经过现场测试的 PLC A 组龙门逻辑整理。  
> 当前正式可用组：A 组，X1=L0、X2=L1、逻辑轴 X=SYN0。  
> B 组/SYN1 仍属于后续扩展，当前上位机不得开放控制。

## 1. 文档目的

本文统一说明以下内容：

- PLC 轴拓扑和龙门参数如何配置；
- PLC 如何校验配置并生成运行轴绑定；
- 上位机如何发送建立、解除和复位命令；
- 建立联动、逻辑轴运动、解除联动及异常清理的状态变化；
- 上位机页面、按钮、状态显示和报警应如何设计；
- 哪些变量允许上位机写入，哪些变量必须只读；
- 当前已验证范围以及后续多组扩展边界。

本文是操作和接口汇总。详细设计仍以《PLC轴拓扑权威配置》《PLC龙门联动控制逻辑》和 `gantry_state_machine_blocks` 中的程序块为准。

## 2. 当前系统边界

### 2.1 当前已实现的 A 组

| 角色 | 物理/虚拟轴 | PLC 轴下标 | MotorNo | 用途 |
| --- | --- | ---: | ---: | --- |
| X1 | L0 | 0 | 1 | 龙门第一侧实体轴 |
| X2 | L1 | 1 | 2 | 龙门第二侧实体轴 |
| X | SYN0 | 13 | 0 | 龙门逻辑轴/电子齿轮主轴 |

拓扑角色固定为：

| Role 下标 | 含义 |
| :--: | :-: |
| `Role[0]` | X1 实体轴 |
| `Role[1]` | X2 实体轴 |
| `Role[5]` | X 龙门逻辑轴 |

`Role` 保持 `ST_RoleAxisBinding[8]`，其余位置用于未来轴角色扩展，不是错误。

### 2.2 当前未开放的 B 组

权威拓扑文档中已经规划 B 组，例如 X1=L4、X2=L5、逻辑轴=SYN1，但目前：

- A 组测试版校验程序要求 `AxisTopology.Group[1].Valid=FALSE`；
- B 组状态机和静态 MC 电子齿轮适配器尚未完整落地；
- 上位机只能显示“未开放/待实现”，不能发送 B 组联动命令；
- 当前轴数组范围为 0..15，对应现有 SYN0..SYN2，不应在未扩容前假定 SYN3 可用。

## 3. PLC 拓扑配置

### 3.1 配置头

上位机或配置初始化程序写入：

```iecst
AxisTopology.Magic := 20260806;
AxisTopology.SchemaVersion := 1;
AxisTopology.Revision := 1;
AxisTopology.ConfigCRC := 0;
```

其中 `Magic`、`SchemaVersion`、`Revision`、`ConfigCRC` 均按整数处理。配置内容发生正式变更时，应递增 `Revision`。

### 3.2 A 组组级配置

```iecst
AxisTopology.Group[0].Valid := TRUE;
AxisTopology.Group[0].HmiVisible := TRUE;
AxisTopology.Group[0].GroupCode := 0;
```

A 组核心角色配置如下：

| 字段 | X1：`Role[0]` | X2：`Role[1]` | X：`Role[5]` |
| --- | --- | --- | --- |
| `Valid` | TRUE | TRUE | TRUE |
| `PlcAxisIndex` | 0 | 1 | 13 |
| `MotorNo` | 1 | 2 | 0 |
| `AxisClass` | 线性实体轴 | 线性实体轴 | 虚拟轴 |
| `UnitType` | mm | mm | mm |
| `MotionMode` | 龙门基准成员 | 龙门跟随成员 | 龙门逻辑轴 |

`HmiVisible` 只决定上位机是否展示角色，不代替 `Valid`，也不参与安全许可判断。

### 3.3 配置有效性的唯一规则

`AxisTopology.ConfigValid` 和 `AxisTopology.ConfigErrorCode` 只能由 PLC 校验逻辑写入：

1. PLC 每个扫描周期先把校验结果初始化为有效；
2. 依次检查配置头、组、角色、轴下标、轴类型、单位、运动模式、重复占用等条件；
3. 任一检查失败时记录错误码；
4. 全部检查通过后，才把 `ConfigValid` 置为 TRUE。

严禁先将 `ConfigValid` 置为 TRUE 再开始校验。上位机也不得直接写 `ConfigValid` 或 `ConfigErrorCode`。

当前 A 组测试版中，核心结果包括：

| 条件 | 结果 |
| --- | --- |
| 配置完全正确 | `ConfigValid=TRUE`、`ConfigErrorCode=0` |
| A 组逻辑轴 `Role[5]` 无效 | `ConfigValid=FALSE`、`ConfigErrorCode=40` |
| 配置失效进入状态机 | `InternalStep=0`、`State=0`、`CommandErrorCode=100` |
| 龙门参数无效 | `CommandErrorCode=101` |

### 3.4 PLC 运行轴绑定

状态机初始化时从拓扑解析运行轴号，而不是在状态机中再次硬编码：

```iecst
GantryRuntime[0].X1AxisIndex :=
    AxisTopology.Group[0].Role[0].PlcAxisIndex;

GantryRuntime[0].X2AxisIndex :=
    AxisTopology.Group[0].Role[1].PlcAxisIndex;

GantryRuntime[0].LogicalAxisIndex :=
    AxisTopology.Group[0].Role[5].PlcAxisIndex;
```

A 组正常值应为 0、1、13。`GantryGearIn... [0]` 和 `[1]` 仍表示 X1/X2 成员槽位，不是 PLC 轴下标。

## 4. 龙门参数与建立准入

发送建立命令前，上位机至少应监控：

```text
AxisTopology.ConfigValid
AxisTopology.ConfigErrorCode
GantryParamCheckValid
GantryReadyToCouple
GantryReadyErrorCode
GantryStatus[0].State
GantryStatus[0].Fault
```

只有以下结果成立时才允许点击“建立联动”：

```text
AxisTopology.ConfigValid = TRUE
GantryParamCheckValid = TRUE
GantryReadyToCouple = TRUE
GantryReadyErrorCode = 0
GantryStatus[0].State = 1
GantryStatus[0].Fault = FALSE
```

PLC 建立准入还会检查：

- X1、X2 和 SYN0 轴控及电机处于需要的使能状态；
- 两实体轴无轴故障、驱动故障和运动指令错误；
- 两实体轴持续稳定停止；
- 无定位、点动、回零、GearIn、GearOut 等冲突命令；
- 两实体轴未被其他组占用；
- 龙门两侧偏差在建立阈值内。

当前停止稳定判断采用速度阈值和 300 ms 连续稳定确认。现场测试使用 0.1 作为停止判断阈值；正常运动速度应明显高于 0.1，避免运动与停止区间重叠。

## 5. 上位机命令协议

### 5.1 可写命令字段

每个联动组使用：

```text
GantryCommand[g].Command
GantryCommand[g].RequestSeq
```

命令码：

| `Command` | 含义 |
| ---: | --- |
| 0 | 无命令 |
| 1 | 建立联动 |
| 2 | 解除联动 |
| 3 | 故障复位/安全恢复 |

发送规则：

1. 先写 `Command`；
2. 再将 `RequestSeq` 在原值基础上加 1；
3. PLC 只在序号变化时接收一次命令；
4. 等待 `GantryStatus[g].AckSeq = RequestSeq`；
5. 根据 `CommandResult` 和 `CommandErrorCode` 显示结果。

不要只重复写相同的 `Command` 而不改变 `RequestSeq`。

### 5.2 命令结果

| `CommandResult` | 含义 | UI 建议 |
| ---: | --- | --- |
| 0 | 无结果 | 空闲/尚未执行 |
| 1 | 处理中 | 显示进度并禁止重复提交 |
| 2 | 成功 | 显示成功并刷新控制权限 |
| 3 | 拒绝 | 显示准入条件不满足和原因码 |
| 4 | 失败 | 显示故障，必要时开放复位 |

`CommandErrorCode` 是本次命令结果原因；`FaultCode` 是当前组持续存在的故障原因，两者不能混为一个字段。

## 6. PLC 状态和内部步骤

### 6.1 对外状态 `State`

| `State` | 含义 |
| ---: | --- |
| 0 | 未配置 |
| 1 | 已解除联动 |
| 2 | 建立中 |
| 3 | 已联动 |
| 4 | 解除中 |
| 5 | 故障 |

上位机业务逻辑以 `State` 为主；`InternalStep` 主要用于调试、诊断和进度展示，不能由上位机写入。

### 6.2 当前 A 组正式步骤

| `InternalStep` | 含义 |
| ---: | --- |
| 0 | 未配置/初始化 |
| 10 | 已解除，等待命令 |
| 20 | 建立预处理，启动 SYN0 位置对齐 |
| 30 | 等待 SYN0 位置设置完成 |
| 35 | 位置对齐完成，准备建立电子齿轮 |
| 60 | X1 GearIn |
| 70 | X2 GearIn |
| 80 | 已联动，逻辑轴控制阶段 |
| 150 | 请求 SYN0 Halt 并等待逻辑轴及成员停止 |
| 160 | X2 GearOut |
| 170 | X1 GearOut |
| 910 | 故障等待复位 |

正式建立路径：

```text
10 → 20 → 30 → 35 → 60 → 70 → 80
```

正式解除路径：

```text
80 → 150 → 160 → 170 → 10
```

旧 Step 40/45/50/55/120/130 是早期单成员测试流程，正式 MAIN 中已退役，不应再作为上位机流程依据。

## 7. 建立联动操作

### 7.1 上位机写入

假设当前 `RequestSeq=N`：

```text
GantryCommand[0].Command = 1
GantryCommand[0].RequestSeq = N + 1
```

### 7.2 PLC 执行过程

1. 在 Step 10 接收命令并确认准入条件；
2. 将 SYN0 位置设置为 X1/X2 归一化位置的平均值；
3. 等待位置设置完成并确认误差；
4. SYN0 依次对 X1、X2 执行 GearIn；
5. 两个成员均反馈 InGear 后进入 Step 80；
6. 实体轴独立控制被关闭，逻辑轴控制被开放。

### 7.3 成功判据

```text
GantryStatus[0].AckSeq = 本次 RequestSeq
GantryRuntime[0].InternalStep = 80
GantryStatus[0].State = 3
GantryStatus[0].CommandResult = 2
GantryStatus[0].CommandErrorCode = 0
GantryStatus[0].X1InGear = TRUE
GantryStatus[0].X2InGear = TRUE
GantryStatus[0].MemberControlAllowed = FALSE
GantryStatus[0].LogicalControlAllowed = TRUE
```

虚拟轴 `轴Status[13]=TRUE` 只表示 SYN0 可用，不能替代两个 `InGear` 反馈作为联动成功依据。

## 8. 已联动后的运动控制

### 8.1 控制权限

轴控 FB 的 `MotionAllowed` 接线为：

```text
Axis_L0ctr.MotionAllowed   ← GantryStatus[0].MemberControlAllowed
Axis_L1ctr.MotionAllowed   ← GantryStatus[0].MemberControlAllowed
Axis_SYN0ctr.MotionAllowed ← GantryStatus[0].LogicalControlAllowed
```

权限矩阵：

| 阶段 | L0/L1 普通运动 | SYN0 普通运动 |
| --- | --- | --- |
| 配置无效或故障 | 禁止 | 禁止 |
| Step 10 已解除 | 允许 | 禁止 |
| Step 20..70 建立中 | 禁止 | 禁止 |
| Step 80 已联动 | 禁止 | 允许 |
| Step 150..170 解除中 | 禁止 | 禁止 |

`MotionAllowed=FALSE` 时，轴控 FB 会清除绝对定位、相对定位、回零和点动等普通命令。安全相关的 Halt、急停、复位、GearOut 和反馈采集不能被该权限切断。

不要通过关闭整个轴控 FB 的扫描或强制 `使能轴控=FALSE` 来实现拓扑禁用，否则配置丢失时可能无法继续执行 Halt、GearOut 和状态反馈。

### 8.2 SYN0 运动变量

已联动且 `LogicalControlAllowed=TRUE` 后，上位机通过 SYN0 对应的标准轴命令控制整个龙门。例如相对定位：

```text
轴MoveXDPosition[13] = 目标相对距离
定位速度[13] = 目标速度
轴MoveXDDo[13] = TRUE（触发）
```

PLC 公共变量逻辑会循环执行：

```iecst
轴MoveSpeed[i] := 定位速度[i];
```

因此上位机应写 `定位速度[13]`，不要直接写会在下一扫描周期被覆盖的 `轴MoveSpeed[13]`。

### 8.3 运行健康监控

Step 80 持续检查：

- X1、X2 的 InGear 反馈；
- X1、X2、SYN0 的轴状态和命令错误；
- 龙门实时偏差 `GantryStatus[0].Skew`；
- 偏差是否持续超过 `GantryParam[0].RunningSkewLimit` 和 `SkewDelayMs`。

相关变量：

```text
GantryStatus[0].LogicalPosition
GantryStatus[0].Skew
GantryLogicalMotionReady
GantryLogicalMotionErrorCode
GantryRunningSkewExceeded
GantryRunningSkewTimeout
```

核心运行错误码：

| 错误码 | 含义 |
| ---: | --- |
| 150 | 电子齿轮/联动健康条件丢失 |
| 151 | 运行偏差持续超限 |
| 152 | 轴状态或轴命令错误 |
| 153 | 逻辑轴停止超时 |

`GantryRunningSkewTestForce` 和 `GantryLogicalHaltTimeoutTestForce` 仅用于调试注入，正式上位机页面不得暴露。

## 9. 正常解除联动

### 9.1 上位机写入

假设当前 `RequestSeq=N`：

```text
GantryCommand[0].Command = 2
GantryCommand[0].RequestSeq = N + 1
```

### 9.2 PLC 执行顺序

1. 从 Step 80 进入 Step 150；
2. PLC 请求 SYN0 Halt；
3. 等待逻辑轴停止完成，并等待 X1/X2 持续稳定停止；
4. Step 160 解除 X2 电子齿轮；
5. Step 170 解除 X1 电子齿轮；
6. 确认两个成员均不再 InGear；
7. 返回 Step 10，重新开放实体轴独立控制。

实际扫描速度很快，监控表可能看不到 150、160、170 的瞬时值。只要最终状态和反馈正确，不要求 UI 必须捕获每个内部步骤。

### 9.3 成功判据

```text
GantryStatus[0].AckSeq = 本次 RequestSeq
GantryRuntime[0].InternalStep = 10
GantryStatus[0].State = 1
GantryStatus[0].CommandResult = 2
GantryStatus[0].CommandErrorCode = 0
GantryStatus[0].X1InGear = FALSE
GantryStatus[0].X2InGear = FALSE
GantryStatus[0].MemberControlAllowed = TRUE
GantryStatus[0].LogicalControlAllowed = FALSE
```

运动过程中收到解除命令也走同一条安全路径：先 Halt，再等待停止，最后 GearOut；上位机不能直接写 GearOut Execute 跳过停止过程。

## 10. 故障、复位和配置丢失

### 10.1 故障状态

进入 Step 910 后：

```text
State = 5
Fault = TRUE
MemberControlAllowed = FALSE
LogicalControlAllowed = FALSE
```

上位机应显示 `FaultCode` 和 `CommandErrorCode`，禁止建立、解除以外的普通运动，并开放“安全复位”按钮。

### 10.2 复位命令

```text
GantryCommand[0].Command = 3
GantryCommand[0].RequestSeq = N + 1
```

PLC 不会简单清零故障。如果仍有成员处于 InGear，会重新进入 Step 150，先停止并解除残留联动；确认无残留后才返回 Step 10。若配置仍无效，则最终保持 Step 0。

### 10.3 运行中配置失效

运行中发现 `AxisTopology.ConfigValid=FALSE` 时，PLC 使用 `GantryConfigLossCleanupActive` 锁存安全清理流程：

```text
关闭全部普通运动权限
→ Step 150 停止 SYN0
→ Step 160 解除 X2
→ Step 170 解除 X1
→ Step 0 未配置
```

清理完成后的典型结果：

```text
AxisTopology.ConfigValid = FALSE
GantryRuntime[0].InternalStep = 0
GantryStatus[0].State = 0
GantryStatus[0].CommandErrorCode = 100
GantryStatus[0].X1InGear = FALSE
GantryStatus[0].X2InGear = FALSE
MemberControlAllowed = FALSE
LogicalControlAllowed = FALSE
```

修复配置后，PLC 重新校验成功并初始化到 Step 10；上位机不得强行改写状态机步骤。

## 11. 上位机 UI 设计

### 11.1 推荐页面结构

建议分为四个区域：

1. **拓扑配置页**：组、角色、PLC 轴号、轴类型、单位、运动模式、版本；
2. **龙门操作页**：建立、解除、复位及当前阶段；
3. **轴运动页**：根据 PLC 权限在实体轴控制和逻辑轴控制之间切换；
4. **诊断页**：步骤、InGear、位置、偏差、命令结果、故障码和超时状态。

### 11.2 组和角色展示规则

组显示条件建议为：

```text
Group.Valid AND Group.HmiVisible
```

组内角色显示条件建议为：

```text
Group.Valid AND Group.HmiVisible
AND Role.Valid AND Role.HmiVisible
```

轴名称和轴下标应从 `AxisTopology` 获取。不要在 UI 中另建一套与 PLC 不一致的 L0/L1/SYN0 固定映射。

### 11.3 状态卡片

每个组至少显示：

```text
State / 状态文本
InternalStep / 调试步骤
ConfigValid / ConfigErrorCode
ReadyToCouple / ReadyToDecouple
MemberControlAllowed / LogicalControlAllowed
X1InGear / X2InGear
LogicalPosition / Skew
Fault / FaultCode
CommandResult / CommandErrorCode
AckSeq
```

状态颜色建议：

| 状态 | 颜色建议 |
| --- | --- |
| 未配置 | 灰色 |
| 已解除 | 蓝色或普通色 |
| 建立中/解除中 | 黄色 |
| 已联动 | 绿色 |
| 故障 | 红色 |

### 11.4 按钮开放条件

| 按钮 | 建议开放条件 |
| --- | --- |
| 建立联动 | `State=1 AND ReadyToCouple AND ConfigValid AND NOT Fault` |
| 解除联动 | `State=3`，或 PLC 明确给出 `ReadyToDecouple=TRUE` |
| 故障复位 | `State=5 OR Fault=TRUE` |
| X1/X2 普通运动 | `MemberControlAllowed=TRUE` |
| SYN0 逻辑运动 | `LogicalControlAllowed=TRUE AND GantryLogicalMotionReady=TRUE` |

按钮禁用时仍应显示轴位置、状态和错误信息。控制权限不等于数据显示权限。

### 11.5 命令交互

上位机发送命令后：

1. 立即将本地按钮置为“提交中”，避免双击；
2. 等待 `AckSeq` 对齐本次 `RequestSeq`；
3. `CommandResult=1` 时显示执行中；
4. `CommandResult=2` 时显示成功；
5. `CommandResult=3/4` 时显示原因码和可读文本；
6. 超过上位机通信超时时间仍未 Ack，只能提示通信超时，不能擅自判定 PLC 动作失败或改写步骤。

### 11.6 配置编辑策略

拓扑配置修改应只在维护模式下进行，推荐条件：

- 所有联动组已解除；
- 所有相关轴已经停止；
- 没有 GearIn/GearOut/定位/点动命令；
- 修改完成后递增 `Revision`；
- 等待 PLC 完整校验，再依据 `ConfigValid` 决定是否开放操作。

如果配置在联动中被撤销，PLC 会执行安全清理。UI 应显示“配置失效，正在安全解除”，而不是立即把设备当作普通未配置轴开放控制。

## 12. 上位机变量读写边界

### 12.1 正常业务允许写入

```text
AxisTopology 的配置输入字段（仅维护模式）
GantryParam 的配置输入字段（仅维护模式）
GantryCommand[g].Command
GantryCommand[g].RequestSeq
标准轴运动目标、速度和触发变量
```

### 12.2 必须只读

```text
AxisTopology.ConfigValid
AxisTopology.ConfigErrorCode
GantryRuntime[*]
GantryStatus[*]
GantryReadyToCouple
GantryReadyErrorCode
GantryLogicalMotionReady
GantryLogicalMotionErrorCode
GantryGearInExecute[*]
GantryGearOutExecute[*]
GantryGearInInGear[*]
GantrySynSetPositionExecute
GantryConfigLossCleanupActive
所有 Timer、Busy、Done、Error 和内部步骤变量
```

上位机绝不能通过直接写 `State`、`InternalStep`、`MemberControlAllowed`、`LogicalControlAllowed`、`InGear` 或 Gear Execute 来“加速”流程。

### 12.3 不进入正式 UI 的调试变量

```text
GantryRunningSkewTestForce
GantryLogicalHaltTimeoutTestForce
GantryLegacySyn0ControlEnable
GantryLegacyGearControlEnable
```

两个 Legacy 开关应保持 FALSE，避免旧 SYN0/旧电子齿轮网络与新状态机同时控制同一命令。

## 13. PLC 程序块执行顺序

MAIN 的“龙门状态机”网络中按以下顺序执行，并且必须位于轴控 FB 调用之前：

1. `01_preprocess.st`
2. `01a_logical_motion_health.st`
3. `01b_logical_halt_feedback.st`
4. `02_base_steps.st`
5. `05_dual_steps.st`
6. `05a_logical_stop_step.st`
7. `06_fault_unknown.st`
8. `07_status_sync.st`
9. `08_axis_command_merge.st`

注意：

- `05a` 必须位于 `05` 之后、`06` 之前；
- `08` 必须位于所有状态步骤之后、SYN0 轴控 FB 之前；
- 同一个定时器实例一个扫描周期只能调用一次；
- 旧 `03_x1_steps.st`、`04_x2_steps.st` 不再加入正式 MAIN；
- 旧 SYN0 和旧电子齿轮网络必须由 Legacy 开关保持禁用。

## 14. 上线验收清单

### 14.1 配置验收

- [ ] A 组 Role[0]/Role[1]/Role[5] 分别解析为 0/1/13；
- [ ] `ConfigValid=TRUE`、`ConfigErrorCode=0`；
- [ ] 状态机初始化到 Step 10、State 1；
- [ ] B 组在未实现前保持无效且 UI 不开放。

### 14.2 建立验收

- [ ] `ReadyToCouple=TRUE`、原因码为 0；
- [ ] Command=1、RequestSeq 递增后 Ack 对齐；
- [ ] 状态自动走到 Step 80、State 3；
- [ ] X1InGear/X2InGear 均为 TRUE；
- [ ] L0/L1 普通运动被禁止，SYN0 逻辑运动被允许。

### 14.3 运动验收

- [ ] SYN0 正向和反向小距离运动完成；
- [ ] X1/X2 跟随且无指令错误；
- [ ] 偏差未持续超过运行阈值；
- [ ] 运动中实体轴独立命令被清除或拒绝。

### 14.4 解除验收

- [ ] 运动中发送解除也会先停止；
- [ ] 最终回到 Step 10、State 1；
- [ ] X1InGear/X2InGear 均为 FALSE；
- [ ] 实体轴控制恢复，SYN0 普通运动关闭。

### 14.5 异常验收

- [ ] 运行偏差超时进入 Fault 151；
- [ ] 逻辑轴 Halt 超时进入 Fault 153；
- [ ] Command=3 能完成安全解除并恢复；
- [ ] 联动中拓扑失效能按 150→160→170→0 安全清理；
- [ ] 配置恢复后重新进入 Step 10，不自动重新建立联动。

## 15. 后续扩展建议

扩展 B 组或更多逻辑轴时，保持同一上位机协议：

```text
AxisTopology.Group[g]
GantryParam[g]
GantryCommand[g]
GantryStatus[g]
GantryRuntime[g]
```

上位机只需按组索引生成相同的组件，不需要知道内部 Step 的具体实现。PLC 侧可复用通用状态层，但 MC_GearIn/MC_GearOut 的 Axis 引用通常需要为每个实际轴组合保留静态适配网络。只有在对应组的拓扑校验、状态机、电子齿轮适配器、权限接线和异常清理全部完成后，UI 才能把该组标记为“可控制”。

