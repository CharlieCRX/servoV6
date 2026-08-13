# servoV6 剩余迁移工作实施方案

## 1. 文档目的

本文档用于明确当前 `servoV6` 基于 `PLC_re` 迁移工作的剩余内容、实施顺序、接口要求和真实 PLC 验收标准。

本文档基于以下已确认事实：

- 真实 PLC 使用 Modbus TCP，端口为 `502`，Unit ID 为 `1`；PLC IP 作为运行配置注入，不写死在业务代码中。
- 真实 PLC 可以部署并替换为当前 `PLC_re` 程序。
- `AxisTopology` 的 Magic、SchemaVersion、Revision 和当前 vnext layout 一致。
- A、B 两组最终都需要支持控制；当前 B 组仅在测试阶段暂时屏蔽。
- 触发型 M 线圈由 PLC 自动复位，上位机只写 ON，不补写 OFF。
- 龙门建立和解除的确认超时均为 `5000 ms`，来源于 `GantryParam.CoupleTimeoutMs` 与 `DecoupleTimeoutMs`。
- 真实 PLC 写入验收具备回滚方案，可以进行受控联机验证。

本文档不替代 PLC 变量协议和 PLC 状态机文档；地址和 PLC 内部逻辑以 `PLC_re` 工程及其协议文档为准。

## 2. 当前迁移状态

### 2.1 已完成部分

当前 `servoV6` 已基本完成以下 vnext 模块：

- `infrastructure/plc_vnext`
  - contracts
  - Modbus 地址 layout
  - 编解码和字序处理
  - 拓扑读取、解码和校验
  - 运行反馈读取
  - 单轴命令 writer
  - 龙门命令 writer
  - 连接状态和 I/O 串行化
  - Fake PLC 和离线测试
- `domain_vnext`
  - AxisSystem
  - AxisRegistry
  - GroupModel
  - SystemBoot
  - FeedbackDispatcher
  - 轴状态机
  - 安全状态机
  - 龙门联动状态机
  - CommandOutbox 和 CommandMapper
- `application_vnext`
  - `SystemManagerVnext`
  - `PlcRuntimeDriverAdapter`
  - 启动、反馈轮询、单轴命令和龙门命令的应用层测试

当前 vnext 相关离线测试已经覆盖协议、基础设施、Domain 和 Application 的主要路径。

### 2.2 尚未完成部分

当前生产入口仍然使用旧链路：

```text
旧 AsioModbusTcpClient
    -> 旧 ModbusSystemDriver
    -> 旧 SystemManager
    -> 旧 pollFeedback()
```

`plc_vnext` 的真实 `AsioModbusTcpClient` 仍是迁移桩，主程序也尚未切换到：

```text
真实 AsioModbusTcpClient
    -> PlcRuntimeGateway
    -> PlcRuntimeDriverAdapter
    -> SystemManagerVnext
    -> SystemBoot / poll / FeedbackDispatcher
```

因此当前状态是：

> 新架构离线开发基本完成，真实通讯客户端、生产运行时接入和真机闭环验收仍需完成。

## 3. 总体迁移目标

迁移完成后的正式链路应为：

```text
QML / UDP / 摇杆
        |
        v
SystemManagerVnext
        |
        v
domain_vnext 状态机与命令队列
        |
        v
PlcRuntimeDriverAdapter
        |
        v
PlcRuntimeGateway
        |
        v
ModbusIoExecutor
        |
        v
真实 AsioModbusTcpClient
        |
        v
PLC_re
```

反馈链路为：

```text
PLC_re
  -> Modbus TCP
  -> PlcRuntimeGateway.readTopology/readRuntime
  -> SystemManagerVnext.boot/poll
  -> FeedbackDispatcher
  -> domain_vnext 状态机
  -> ViewModel / UDP 响应 / UI 状态
```

正式版本不得由旧链路和新链路同时向同一 PLC 写入运动命令。

## 4. 剩余工作清单

## 4.1 完成 vnext 真实 Modbus TCP 客户端

目标文件：

```text
infrastructure/plc_vnext/transport/AsioModbusTcpClient.h
infrastructure/plc_vnext/transport/AsioModbusTcpClient.cpp
```

应迁移旧客户端的真实能力，并适配新接口 `transport::IModbusClient` 和 `contracts::CommunicationResult`。

必须支持：

- FC01：读线圈
- FC03：读保持寄存器
- FC05：写单线圈
- FC06：写单保持寄存器
- FC10：写多个保持寄存器
- TCP 连接、断开和自动重连
- 请求超时和 socket 取消
- MBAP/PDU 长度校验
- Transaction ID 校验
- Modbus 异常响应转换
- 连接状态查询和手动重连
- 0 基地址原样传递

注意事项：

- 业务地址以 `PLC_re` 协议的 0 基地址为准。
- 不得把 `40001`、`00001` 等显示地址直接传给底层 PDU。
- 不能因断线自动重放运动命令。
- FC10 的请求失败不得被伪装成成功。
- 读写错误必须保留诊断信息，至少包括 PLC endpoint、功能码、地址和错误类型。

## 4.2 将主程序切换到 vnext

目标文件：

```text
main.cpp
```

需要完成：

1. 从运行配置读取 PLC IP、端口、Unit ID、超时和重连参数。
2. 创建真实 `AsioModbusTcpClient`。
3. 创建 `PlcRuntimeGateway`。
4. 创建 `PlcRuntimeDriverAdapter`。
5. 创建 `SystemManagerVnext`。
6. 启动时调用 `boot()`。
7. 启动成功后按周期调用 `poll()`。
8. 将 UI、UDP 和连接状态 ViewModel 接到 vnext manager。
9. 删除或隔离旧 `SystemManager` 的生产调用路径。

启动顺序必须是：

```text
创建通讯客户端
    -> 建立/等待连接
    -> 读取拓扑
    -> 校验拓扑和 Revision
    -> 动态建立 AxisSystem
    -> 读取首次运行反馈
    -> 同步急停状态
    -> 开放允许的操作
```

在首次拓扑、运行反馈和急停状态同步完成之前，普通运动操作必须保持锁定。

## 4.3 A/B 组改为配置驱动

当前 `PlcRuntimeGateway` 的默认 group gate 仅放行 Group 0，需要改为配置驱动，不能永久硬编码禁止 Group 1。

建议配置模型：

```cpp
struct PlcGroupCapability {
    bool enabled = false;
    bool allowAxisControl = false;
    bool allowGantryControl = false;
};
```

测试阶段可以配置为：

```text
Group A: enabled=true,  allowAxisControl=true,  allowGantryControl=true
Group B: enabled=false, allowAxisControl=false, allowGantryControl=false
```

正式上线配置为：

```text
Group A: enabled=true, allowAxisControl=true, allowGantryControl=true
Group B: enabled=true, allowAxisControl=true, allowGantryControl=true
```

配置 gate 只能作为上位机能力开关，不能绕过 PLC 的：

- `ConfigValid`
- `MemberControlAllowed`
- `LogicalControlAllowed`
- `State`
- `CommandResult`

Group B 需要完成独立的拓扑、轴 slot、龙门参数和运动权限验收后，才能在 UI 中标记为可控制。

## 4.4 完成急停 M224/M225 接口

### PLC 语义

急停逻辑按以下规则实现：

```text
设备急停 M224 = ON：
    持续保持急停状态
    保留“使能轴控[i]”当前值，不修改
    关闭电机使能、定位、点动、回零等所有运动命令
    拒绝所有普通操作
    唯一允许的操作是设备急停解除 M225 = ON

设备急停解除 M225 = ON：
    PLC 处理解除请求
    M224、M225 自动变为 OFF
    不自动恢复之前的电机使能
    不自动恢复之前的运动命令
    后续必须由上位机重新下发使能和运动命令
```

### 上位机接口

上位机需要保留以下明确接口：

```cpp
requestEmergencyStop();
requestReleaseEmergencyStop();
isEmergencyStopped();
isEmergencyStopping();
isReleasingEmergencyStop();
isMotionLocked();
```

### 上位机行为

- 急停期间拒绝普通运动、点动、定位、回零、龙门建立、龙门解除和龙门复位。
- 急停期间允许发送解除急停请求。
- 急停时不主动清除 `使能轴控[i]`。
- 解除急停成功后不自动恢复电机使能。
- 解除急停成功后不自动恢复运动目标或运动命令。
- 解除后由用户重新执行电机使能和运动操作。
- 通讯断开或反馈不可信时，系统保持锁定。

### 反馈接入

M224/M225 状态应纳入真实周期通讯：

```text
读取 M224/M225
    -> 转换为急停反馈
    -> FeedbackDispatcher::dispatchSafety()
    -> SafetyStateMachine
```

如果 `RuntimeSnapshot` 不包含 M224 状态，则由独立安全状态读取器完成读取，但必须使用同一个串行 I/O 通道。

## 4.5 完成单轴命令真实闭环

单轴命令分为三类。

### 保持型命令

需要根据 UI 或状态持续维护 ON/OFF：

- 轴使能
- 电机使能
- 正向点动
- 反向点动

### PLC 自动复位型命令

只写 ON，不手动写 OFF：

- 相对原点清除
- 绝对位置清零
- 绝对定位触发
- 相对定位触发
- 相对定位停止
- 绝对定位停止
- 相对原点设置

具体命令以 `PlcAxisCommand` 和 `PLC_re` 当前版本为准。

### 周期心跳型命令

点动心跳需要按 PLC 约定周期发送；停止点动时发送一次必要的 OFF，并确认 PLC 已清除超时状态。

所有运动触发都必须满足：

```text
写请求成功 != PLC 已执行
```

执行结果必须通过后续反馈判断。

## 4.6 完成龙门 AckSeq/CommandResult 确认机制

龙门命令必须遵守：

```text
写 GantryCommand.Command
    -> 写 GantryCommand.RequestSeq
    -> 等待 GantryStatus.AckSeq == RequestSeq
    -> 读取 CommandResult
    -> 得出最终结果
```

确认超时：

```text
建立联动：5000 ms
解除联动：5000 ms
```

最终结果至少包括：

- Submitted：通讯写入成功，等待 PLC 处理
- Processing：AckSeq 已对齐，PLC 正在执行
- Succeeded：`CommandResult=2`
- Rejected：`CommandResult=3`
- Failed：`CommandResult=4`
- Timeout：超时未完成
- CommitUncertain：请求提交结果不确定，禁止盲目重发

上位机不得仅根据 Modbus 写入成功就将龙门操作显示为成功。

`RequestSeq` 写入失败时，必须依据后续 `AckSeq` 判断请求是否已经被 PLC 接收，不能立即重发同一命令。

## 4.7 迁移 UI、UDP 和摇杆调用

生产控制入口需要逐步改为调用 `SystemManagerVnext`：

- 轴 ViewModel
- 龙门 ViewModel
- 急停 ViewModel
- 连接状态 ViewModel
- UDP 命令分发器
- 摇杆点动控制
- 运动完成和错误提示

迁移期间可以保留旧代码用于回滚，但必须保证：

- 同一时刻只有一套路径向 PLC 写入。
- 不允许旧 `SystemManager` 和 `SystemManagerVnext` 同时运行控制循环。
- 不允许旧、新两套 writer 同时发送相同运动命令。
- UI 轴标识统一使用 `PlcGroupIndex + AxisFunction + PlcAxisSlot`，避免 A/B 组同名轴冲突。

## 5. 实施阶段和顺序

### 阶段 1：真实通讯客户端

完成新 `AsioModbusTcpClient`，优先只验证通讯基础能力：

- 连接 PLC
- FC01 读取 M 区
- FC03 读取 D 区
- FC05/FC06/FC10 受控测试
- 断线、超时、重连
- Modbus 异常响应

此阶段不接 UI 运动控制。

### 阶段 2：真实只读影子运行

接入 `PlcRuntimeGateway`，但禁止写入运动命令，完成：

- 拓扑读取
- 运行反馈读取
- 急停状态读取
- A/B 组绑定展示
- 连接状态展示
- 新旧读取结果并排比对

### 阶段 3：单轴受控写入

建议顺序：

1. 手动速度
2. 定位速度
3. 轴使能
4. 电机使能
5. 点动
6. 位置目标
7. 定位触发
8. 停止、清零和回零
9. 急停
10. 急停解除

### 阶段 4：龙门受控写入

建议顺序：

1. Group A 建立
2. Group A 解除
3. Group A 复位
4. Group B 建立
5. Group B 解除
6. Group B 复位
7. 两组同时存在时的通讯串行化和互不干扰

### 阶段 5：正式运行时切换

完成 UI、UDP、摇杆和连接状态到 vnext 的迁移后，切换生产主循环。

保留旧路径作为回滚版本，观察窗口结束后再单独删除 legacy。

## 6. 真实 PLC 验收标准

### 6.1 通讯验收

- PLC IP、端口和 Unit ID 可配置。
- FC01/FC03 读数与 `plc_read_validate.py` 一致。
- FC05/FC06/FC10 返回结果正确。
- 超时和断线能够被识别。
- 重连后不自动重放运动命令。
- 每个请求有可追踪日志。

### 6.2 拓扑验收

- Magic 正确。
- SchemaVersion 正确。
- Revision 读取稳定。
- A/B 两组角色和 slot 映射正确。
- `ConfigValid` 和 `ConfigErrorCode` 原样保留。
- 拓扑 Revision 变化时能够重新 boot 或安全锁定。

### 6.3 急停验收

- M224 置 ON 后保持急停状态。
- 不修改使能轴控[i]。
- 普通运动命令全部拒绝。
- 只允许 M225 解除。
- M225 处理后 M224/M225 自动 OFF。
- 解除后不自动恢复电机使能和运动。
- 通讯异常期间保持锁定。

### 6.4 单轴验收

- 保持型线圈的 ON/OFF 行为正确。
- 自动复位型线圈只写 ON。
- 运动触发后能通过反馈确认执行状态。
- 目标设置和触发完全分离。
- 点动心跳和停止行为符合 PLC 约定。
- 急停期间所有普通单轴命令被拒绝。

### 6.5 龙门验收

- Command 先于 RequestSeq 写入。
- RequestSeq 写入顺序正确。
- AckSeq 能正确对齐。
- `CommandResult=2` 判定为成功。
- `CommandResult=3/4` 正确显示拒绝或失败。
- 建立/解除超时为 5000 ms。
- CommitUncertain 不自动重发。
- A/B 两组均能独立控制。

## 7. 回滚与安全要求

真实 PLC 已具备回滚方案，但上位机仍应遵守以下安全原则：

- 首次联机先只读，确认拓扑和反馈后再写入。
- 写入验收按单项、单轴、单组逐步进行。
- 运动命令不得在断线重连后自动重放。
- 急停命令和解除命令必须保持可用。
- 出现反馈不可信、Revision 变化、配置无效或通讯异常时，普通控制自动锁定。
- 旧链路和 vnext 链路不得同时写 PLC。
- 每次写入验收保留请求、响应、反馈和结果日志。

## 8. 最终完成条件

满足以下条件后，才可认为 `servoV6` 完成基于 `PLC_re` 的迁移：

1. vnext 真实 Modbus TCP 客户端完成并通过真实 PLC 通讯验收。
2. 主程序已切换到 `PlcRuntimeGateway + SystemManagerVnext`。
3. 拓扑、运行反馈和急停状态均来自真实 PLC。
4. A/B 两组均通过独立控制验收。
5. 急停和解除逻辑符合 PLC_re 的新语义。
6. 单轴所有正式命令完成真实反馈闭环。
7. 龙门 Command/RequestSeq/AckSeq/CommandResult 闭环完成。
8. 建立和解除超时使用 5000 ms 或 PLC 读取到的对应参数。
9. UI、UDP、摇杆均不再依赖旧生产控制链路。
10. 旧链路已完成回滚窗口验证，并通过独立清理变更删除或隔离。

## 9. 相关文件

- `infrastructure/plc_vnext/`
- `domain_vnext/`
- `application_vnext/`
- `main.cpp`
- `tools/plc_read_validate.py`
- `docs/refactor/PLC通讯基础设施层重构设计.md`
- `docs/refactor/PLC通讯基础设施层重构——TDD实施文档.md`
- `PLC_re/docs/PLC变量协议_Modbus最终地址表.md`
- `PLC_re/docs/PLC龙门联动配置与上位机操作指南.md`
- `PLC_re/docs/PLC龙门联动控制逻辑.md`

## 10. 分阶段验收准则

每个阶段必须同时满足进入条件、验收动作和通过标准，才允许进入下一阶段。未通过时只能修复当前阶段问题，不得带问题进入真实运动控制阶段。

### 10.1 阶段 1：真实 Modbus TCP 客户端

进入条件：

- `IModbusClient` 接口和 `CommunicationResult` 已稳定。
- IP、端口、Unit ID、超时和重连参数可配置。
- Fake、编解码和旧客户端回归测试通过。

验收动作：

1. 连接真实 PLC，确认端口 `502`、Unit ID `1` 生效。
2. 用 FC01 读取 M0、M48、M80、M192、M224、M225。
3. 用 FC03 读取 D0、D128、D1400、D1600、D180、D190。
4. 在维护模式下分别验证 FC05、FC06、FC10。
5. 断开网络，验证超时、断线、重连和错误返回。
6. 检查报文地址、功能码、Transaction ID 和响应长度。

通过标准：

- FC01/FC03 读数与现场工具或 PLC 监视值一致。
- FC05/FC06/FC10 的地址、数据和返回结果正确。
- 地址使用 0 基址，无 `+1` 或 `40001` 偏移错误。
- 超时、断线、协议异常均返回失败，不伪造成功。
- 重连后不自动重放运动命令。
- 并发请求不交叉，日志可定位 endpoint、功能码、地址和错误原因。

禁止：本阶段不得从 UI、UDP 或摇杆触发运动控制。

### 10.2 阶段 2：真实只读影子运行

进入条件：

- 阶段 1 已通过。
- `PlcRuntimeGateway` 已接入真实客户端。
- 读取路径不会调用任何 PLC 写接口。

验收动作：

1. `readTopology()` 与 `plc_read_validate.py --only topology` 逐字段对拍。
2. `readRuntime()` 与 `--only axis`、`--only status` 对拍。
3. 连续运行观察窗口，记录采样时间、耗时和快照质量。
4. 验证 Revision 变化、ConfigValid=false、非法 slot、重复 slot 和不支持 SchemaVersion。
5. 读取 M224/M225，验证急停状态投影。
6. 并排比较 legacy 与 vnext 只读结果。

通过标准：

- Magic、SchemaVersion、Revision、ConfigValid、ConfigErrorCode 及 A/B 组绑定一致。
- 16 个 slot 的位置、状态、限位、报警和参数一致。
- 两组龙门的状态、AckSeq、CommandResult、控制许可和故障信息一致。
- 快照不完整、超时或部分失败时不会标记为可信。
- 配置无效、Revision 不一致或反馈不可信时，普通控制保持锁定。
- 影子运行期间无任何 PLC 写报文。

禁止：不得因读取成功自动开启轴控、电机或运动控制。

### 10.3 阶段 3：单轴受控写入

进入条件：

- 阶段 2 已通过。
- 拓扑、运行反馈和急停状态已同步。
- 现场急停、断电和 PLC 回滚条件已准备。
- 一次只开放一个轴和一个命令类别。

验收动作：

1. 验证手动速度、定位速度和目标位置的地址、字序和值。
2. 验证轴使能、电机使能和点动保持型线圈的 ON/OFF。
3. 验证定位、清零、回零等 PLC 自动复位型线圈只写 ON。
4. 验证点动心跳、点动停止和超时清理。
5. 在空载或安全低速条件下执行单轴运动。
6. 运动过程中触发 M224，验证急停锁存和普通操作拒绝。
7. 写入 M225，确认 M224/M225 自动 OFF。
8. 解除后确认电机使能和运动不会自动恢复。
9. 验证断线、超时和反馈不可信时命令被拒绝或进入安全状态。

通过标准：

- 每个命令只写入协议规定的寄存器或线圈。
- 触发型线圈只写 ON，不补写 OFF。
- 保持型线圈正确维护 ON/OFF。
- 最终状态以 PLC 反馈为准，不以写返回值代替执行结果。
- 急停期间不修改“使能轴控[i]”，并拒绝普通运动操作。
- 解除急停后 M224/M225 为 OFF，但不自动恢复电机使能和运动。
- 运动完成、停止、报警和限位状态正确反馈到 UI/Domain。
- 不存在重复发送、断线重放或目标与触发顺序错误。

禁止：不得同时开放多个未验收轴或命令类别；不得把通讯写成功直接显示为运动成功。

### 10.4 阶段 4：龙门受控写入

进入条件：

- 阶段 3 已通过。
- A/B 两组拓扑、参数和 slot 已现场确认。
- `GroupGate` 已改为配置驱动，B 组不再永久禁止。
- 维护模式和回滚方案已准备。

验收动作：

1. A 组执行建立：先写 Command=1，再递增写 RequestSeq。
2. 等待 `AckSeq == RequestSeq`，检查 CommandResult 和错误码。
3. 验证建立超时为 `CoupleTimeoutMs=5000 ms`。
4. 执行解除，验证 `DecoupleTimeoutMs=5000 ms`。
5. 执行复位并检查最终状态。
6. 对 B 组重复建立、解除和复位。
7. 模拟 RequestSeq 写入异常，验证 CommitUncertain 且不会自动重发。
8. 并发执行反馈读取、单轴写和龙门提交，验证 Command→RequestSeq 不被插入。
9. 验证两组地址、状态和命令互不串扰。

通过标准：

- Command 始终先于 RequestSeq。
- AckSeq 正确匹配本次 RequestSeq。
- 只有 `CommandResult=2` 判定成功。
- `CommandResult=3/4` 正确显示拒绝或失败。
- 建立/解除超过 5000 ms 未完成时进入超时，不无限等待。
- CommitUncertain 不盲目重发，可通过 AckSeq 继续判定。
- A/B 两组均可独立控制，且急停、配置无效或控制许可为 FALSE 时拒绝命令。

禁止：不得直接写 State、InternalStep、ControlAllowed、InGear 等只读字段绕过 PLC 状态机。

### 10.5 阶段 5：正式运行时切换

进入条件：

- 阶段 1 至阶段 4 全部通过。
- UI、UDP、摇杆和连接状态均已接入 vnext。
- 生产配置明确 A/B 两组能力和 PLC endpoint。
- 旧链路仍可回滚，但已停止并行控制。

验收动作：

1. 从 UI 完成启动、拓扑加载、反馈显示、单轴和龙门操作。
2. 从 UDP 完成等价命令和反馈验证。
3. 验证摇杆点动、心跳和停止。
4. 验证应用重启、PLC 重启、断网和重连。
5. 验证启动急停、配置无效、Revision 变化和 B 组不可用场景。
6. 检查日志、错误提示、连接状态和命令结果。
7. 观察窗口内确认没有旧链路写报文。

通过标准：

- 正式控制入口全部经过 `SystemManagerVnext` 和 `IPlcRuntimeGateway`。
- 主程序不再使用旧 `SystemManager` 作为生产控制入口。
- 任意时刻只有一套控制链路向 PLC 写入。
- UI、UDP、摇杆的轴、龙门、急停和连接状态一致。
- 重启和重连不会自动恢复或重放运动命令。
- 未完成同步、急停、反馈不可信、配置无效或通讯异常时普通控制自动锁定。
- A/B 两组在正式配置下均可完成允许的控制操作。
- 旧链路回滚后 PLC 状态仍可控，没有遗留未清理命令。

禁止：观察窗口结束前不得删除 legacy；不得用 UI 显示正常替代真实 PLC 反馈验收。
