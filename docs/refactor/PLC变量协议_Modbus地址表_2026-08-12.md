# PLC_re 变量协议与 Modbus 地址表

> 基线：`PLC_re` 当前工作区，编译产物时间 2026-08-12。  
> 依据：`VarNameAndAddr.csv`、`CrossTable.crs`、结构体文件、`MAIN.LD`、`SBR_轴控.LD`、`SBR_龙门齿轮.LD` 及龙门状态机源码。  
> 地址约定：本文使用 PLC 原始 0 基址，不加 `1`，也不转换为 `40001` 风格地址。

## 1. 协议总览

| PLC 区域 | Modbus 数据区 | 读 | 单写 | 批量写 | PDU 地址 |
| --- | --- | --- | --- | --- | --- |
| M | Coil | FC01 | FC05 | FC0F | 等于 M 编号 |
| D | Holding Register | FC03 | FC06 | FC10 | 等于 D 编号 |

- Modbus TCP 端口：`502`。
- Unit ID：`1`。
- PLC IP：由部署配置提供，脚本中的默认 IP 不属于协议常量。
- `INT`/`WORD` 占 1 个 D 字；`REAL`/`DINT`/32 位枚举占连续 2 个 D 字。
- 32 位值采用低字在低地址、高字在高地址：

```text
D[n]   = u32 的低 16 位
D[n+1] = u32 的高 16 位
u32 = (uint32(D[n+1]) << 16) | uint32(D[n])
```

写入 `REAL`/`DINT` 时，优先使用 FC10 一次写完全部寄存器。

## 2. 地址总览

| 接口 | 起始 | 结束 | 长度 | 权限 | 当前状态 |
| --- | ---: | ---: | ---: | --- | --- |
| 标准 16 轴状态 | D128 | D175 | 48D | R | 开放 |
| `GantryCommand[0..1]` | D180 | D187 | 8D | A组 RW；B组禁用 | A组开放 |
| `GantryStatus[0..1]` | D190 | D225 | 36D | R | A组开放，B组保留 |
| 保持型位置/限位参数 | D1064 | D1223 | 160D | RW | 开放 |
| 旧超差阈值 | D1224 | D1226 | 3D | 兼容 | 禁用 |
| 软限位控制/职责 | D1228 | D1259 | 32D | RW/兼容 | 部分开放 |
| 旧联动成员信息 | D1260 | D1299 | 40D | 兼容 | 禁用 |
| 旧联动轴信息 | D1300 | D1308 | 9D | 兼容 | 禁用 |
| `AxisTopology` | D1400 | D1577 | 178D | 维护 RW；校验结果 R | 开放 |
| `GantryParam[0..1]` | D1600 | D1643 | 44D | A组维护 RW；B组保留 | A组开放 |

当前仅开放 A 组：X1 为 PLC 轴 0，X2 为 PLC 轴 1，逻辑轴 SYN0 为 PLC 轴 13。B 组地址已分配，但必须保持 `Group[1].Valid=FALSE`，不得开放控制。

## 3. 标准 16 轴 D 区

数组下标 `i=0..15`，实际轴绑定应以 `AxisTopology...PlcAxisIndex` 为准。

| 变量 | 范围 | 类型 | 单项步长 | 第 i 项 | 权限 | 说明 |
| --- | --- | --- | ---: | --- | --- | --- |
| 手动速度 | D0..D31 | REAL | 2D | `D(0+2i)..D(1+2i)` | RW | EU/s |
| 定位速度 | D32..D63 | REAL | 2D | `D(32+2i)..D(33+2i)` | RW | EU/s |
| 绝对位置 | D64..D95 | REAL | 2D | `D(64+2i)..D(65+2i)` | R | 当前绝对位置 |
| 相对位置 | D96..D127 | REAL | 2D | `D(96+2i)..D(97+2i)` | R | 绝对位置减相对原点 |
| 运动状态 | D128..D143 | INT | 1D | `D(128+i)` | R | 见状态编码 |
| 运动限制 | D144..D159 | INT | 1D | `D(144+i)` | R | 见限制编码 |
| 告警码 | D160..D175 | WORD | 1D | `D(160+i)` | R | 位掩码 |
| 相对原点记录 | D1064..D1095 | REAL | 2D | `D(1064+2i)..` | RW/保持 | EU |
| 绝对定位距离 | D1096..D1127 | REAL | 2D | `D(1096+2i)..` | RW/保持 | EU |
| 相对定位距离 | D1128..D1159 | REAL | 2D | `D(1128+2i)..` | RW/保持 | EU |
| 软件负限位 | D1160..D1191 | REAL | 2D | `D(1160+2i)..` | RW/保持 | EU |
| 软件正限位 | D1192..D1223 | REAL | 2D | `D(1192+2i)..` | RW/保持 | EU |
| 超差阈值 | D1224..D1226 | INT | 1D | `D(1224+i)` | 兼容 | 新龙门不使用 |
| 软限位控制 | D1228..D1243 | WORD | 1D | `D(1228+i)` | RW | bit0 正限位，bit1 负限位 |
| 职责 | D1244..D1259 | INT | 1D | `D(1244+i)` | 兼容/待定 | 新逻辑不消费 |

运动状态：`0=轴控入口未使能，1=空闲，2=正向点动，3=反向点动，4=绝对定位，5=相对定位`。  
运动限制：`0=无限位，1=正软限位，2=负软限位，3=正硬限位，4=负硬限位`。

## 4. 标准 16 轴 M 区

| 变量 | 地址 | 行为 | 上位机规则 |
| --- | --- | --- | --- |
| 使能轴控[i] | M0..M15 | 保持电平 | 按启停写 ON/OFF |
| 相对原点清除[i] | M16..M31 | 上升沿 | PLC 自复位，读回确认 |
| 绝对位置清零[i] | M32..M47 | 上升沿 | PLC 自复位，读回确认 |
| 绝对定位触发[i] | M48..M63 | 上升沿启动 | PLC 自复位，只写 ON，不回写 OFF |
| 相对定位触发[i] | M64..M79 | 上升沿启动 | PLC 自复位，只写 ON，不回写 OFF |
| 点动正转[i] | M80..M95 | 保持电平 | 按住 ON，松开 OFF |
| 点动反转[i] | M96..M111 | 保持电平 | 按住 ON，松开 OFF |
| 报警解除触发[i] | M112..M127 | 未实现 | 禁止正式 UI 使用 |
| 使能电机[i] | M128..M143 | 保持电平 | 按启停写 ON/OFF |
| 相对定位终止触发[i] | M144..M159 | 上升沿停止 | PLC 自复位，只写 ON，不回写 OFF |
| 绝对定位终止触发[i] | M160..M175 | 上升沿停止 | PLC 自复位，只写 ON，不回写 OFF |
| 相对原点设置[i] | M176..M191 | 上升沿 | PLC 自复位，读回确认 |
| 点动心跳[i] | M192..M207 | 看门狗刷新 | 周期写 ON；PLC 扫描清 OFF |
| 告警码置零[i] | M208..M223 | 一次命令 | PLC 自复位，读回确认 |
| 设备急停 | M224 | 软件急停锁存 | 写 ON 后保持 |
| 设备急停解除 | M225 | 上升沿解除 | PLC 自复位并清 M224 |

### 当前 MAIN.LD 的使能同步

当前工程已增加以下逻辑：

```text
使能轴控[0] := 使能轴控[13]
使能轴控[1] := 使能轴控[13]
使能电机[0] := 使能电机[13]
使能电机[1] := 使能电机[13]
```

因此 A 组统一控制时，上位机应使用逻辑轴入口 `M13` 和 `M141`，不要再与 `M0/M1`、`M128/M129` 并行写入。

## 5. AxisTopology

基址 `D1400`，总长 `178D`。

| 地址 | 字段 | 类型 | 权限/说明 |
| --- | --- | --- | --- |
| D1400..D1401 | Magic | DINT | 维护 RW，当前要求 `20260806` |
| D1402 | SchemaVersion | INT | 维护 RW，当前为 `1` |
| D1403 | Reserved | INT | 写 0 |
| D1404..D1405 | Revision | DINT | 维护 RW，提交配置时递增 |
| D1406..D1407 | 编译器对齐空洞 | — | 禁止依赖或复用 |
| D1408..D1490 | Group[0] | ST_GroupAxisMap | A组，当前开放 |
| D1491..D1573 | Group[1] | ST_GroupAxisMap | B组，必须禁用 |
| D1574..D1575 | ConfigCRC | DINT | 当前固定写 0，算法待定 |
| D1576.bit0 | ConfigValid | BOOL | PLC 写，上位机只读 |
| D1577 | ConfigErrorCode | INT | PLC 写，上位机只读 |

地址公式：

```text
group_base(g) = D1408 + 83*g
role_base(g,r) = group_base(g) + 3 + 10*r
```

组字段：`+0.bit0 Valid`、`+0.bit1 HmiVisible`、`+1 GroupCode`、`+2 Reserved`。  
角色字段：`+0.bit0 Valid`、`+0.bit1 HmiVisible`、`+1 PlcAxisIndex`、`+2 MotorNo`、`+3..+4 AxisClass`、`+5..+6 UnitType`、`+7..+8 MotionMode`、`+9 Reserved`。

A 组当前有效角色：

| 角色 | 起始地址 | PlcAxisIndex | MotorNo | AxisClass | UnitType | MotionMode |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Role[0] X1 | D1411 | 0 | 当前 1 | 0 线性 | 0 mm | 1 龙门X1 |
| Role[1] X2 | D1421 | 1 | 当前 2 | 0 线性 | 0 mm | 2 龙门X2 |
| Role[5] X/SYN0 | D1461 | 13 | 0 | 2 虚轴 | 0 mm | 5 龙门逻辑轴 |

其余 A 组角色必须无效；B 组 `Group[1].Valid=FALSE`。

## 6. GantryParam

数组 `[0..1]`，基址 `D1600`，单项步长 `22D`：

```text
param(g) = D1600 + 22*g
```

| 偏移 | 字段 | 类型 | 约束 |
| ---: | --- | --- | --- |
| +0.bit0 | Valid | BOOL | A组有效；B组保持无效 |
| +1 | DirectionX1 | INT | `+1` 或 `-1` |
| +2 | DirectionX2 | INT | `+1` 或 `-1` |
| +3 | RatioNumeratorX1 | INT | 非 0 |
| +4 | RatioDenominatorX1 | INT | 非 0 |
| +5 | RatioNumeratorX2 | INT | 非 0 |
| +6 | RatioDenominatorX2 | INT | 非 0 |
| +7..+8 | PositionOffsetX1 | REAL | mm |
| +9..+10 | PositionOffsetX2 | REAL | mm |
| +11..+12 | CoupleSkewLimit | REAL | `>0` mm |
| +13..+14 | RunningSkewLimit | REAL | 不小于建立阈值 |
| +15..+16 | SkewDelayMs | DINT | `>0`，建议 50..200 ms |
| +17..+18 | CoupleTimeoutMs | DINT | `>0`，建议 3000..10000 ms |
| +19..+20 | DecoupleTimeoutMs | DINT | `>0`，建议 3000..10000 ms |
| +21 | Reserved | INT | 写 0 |

A 组范围为 `D1600..D1621`，B 组范围为 `D1622..D1643`。

## 7. GantryCommand

数组 `[0..1]`，基址 `D180`，单项步长 `4D`：

| 字段 | A组地址 | B组地址 | 类型 | 权限 |
| --- | --- | --- | --- | --- |
| Command | D180 | D184 | INT | A组 RW，B组禁用 |
| RequestSeq | D181..D182 | D185..D186 | DINT | A组 RW，B组禁用 |
| Reserved | D183 | D187 | INT | 写 0 |

命令码：`0=无命令，1=建立联动，2=解除联动，3=安全复位`。

事务写入规则：先写 `Command`，最后写 `RequestSeq`；推荐一次 FC10 写 `D180..D182`。重复相同命令必须使用新的 `RequestSeq`，不使用线圈脉冲。

## 8. GantryStatus

数组 `[0..1]`，基址 `D190`，单项步长 `18D`。

| 偏移 | 字段 | 类型 | A组地址 | 权限 |
| ---: | --- | --- | --- | --- |
| +0 | State | INT | D190 | R |
| +1 | InternalStep | INT | D191 | R |
| +2..+3 | AckSeq | DINT | D192..D193 | R |
| +4 | CommandResult | INT | D194 | R |
| +5 | CommandErrorCode | INT | D195 | R |
| +6.bit0..5 | ReadyToCouple / ReadyToDecouple / MemberControlAllowed / LogicalControlAllowed / X1InGear / X2InGear | BOOL | D196 bits | R |
| +7..+8 | X1Position | REAL | D197..D198 | R，mm |
| +9..+10 | X2Position | REAL | D199..D200 | R，mm |
| +11..+12 | LogicalPosition | REAL | D201..D202 | R，mm |
| +13..+14 | Skew | REAL | D203..D204 | R，X1-X2 |
| +15.bit0 | Fault | BOOL | D205.bit0 | R |
| +16 | FaultCode | INT | D206 | R |
| +17 | Reserved | INT | D207 | R |

B 组对应范围为 `D208..D225`，当前只读保留。

状态：`0=未配置，1=已解除，2=建立中，3=已联动，4=解除中，5=故障`。  
命令结果：`0=无结果，1=处理中，2=成功，3=拒绝，4=失败`。

## 9. SYN0 逻辑轴接口

已联动后仅允许逻辑轴 SYN0（轴下标 13）运动，且必须满足 `GantryStatus[0].LogicalControlAllowed=TRUE`。

| 操作 | 地址 |
| --- | --- |
| 轴控入口 | M13 |
| 电机使能 | M141 |
| 手动速度 | D26..D27 |
| 定位速度 | D58..D59 |
| 绝对目标 | D1122..D1123 |
| 相对目标 | D1154..D1155 |
| 绝对定位触发 | M61（PLC 自复位·只写 ON） |
| 相对定位触发 | M77（PLC 自复位·只写 ON） |
| 正向点动 | M93，保持电平 |
| 反向点动 | M109，保持电平 |
| 点动心跳 | M205，周期写 ON |
| 绝对位置 | D90..D91 |
| 相对位置 | D122..D123 |
| 运动状态 | D141 |
| 运动限制 | D157 |
| 告警码 | D173 |

`MemberControlAllowed=TRUE` 时才允许独立控制 X1/X2；联动状态下不得直接写 PLC 内部 `轴Move*` 或 GearIn/GearOut 执行变量。

## 10. 配置与运行流程

### 10.1 配置提交

仅在相关轴停止、无联动、无 GearIn/GearOut/定位/点动命令并进入维护模式时：

1. 写入 `AxisTopology` 输入字段和 `GantryParam[0]`。
2. Reserved 字段及 D1406..D1407 保持 0，不复用对齐空洞。
3. 最后递增 `AxisTopology.Revision`。
4. 等待 `D1576.bit0 ConfigValid` 和 `D1577 ConfigErrorCode` 更新。
5. 禁止写 `ConfigValid`、`ConfigErrorCode`、`GantryStatus` 及内部运行变量。

### 10.2 建立联动

准入条件：`ConfigValid=TRUE`、`State=1`、`ReadyToCouple=TRUE`、`Fault=FALSE`。

```text
FC10 D180..D182 = Command(1) + RequestSeq(N+1)
等待 AckSeq == N+1
CommandResult=2 且 State=3 且 X1InGear/X2InGear=TRUE 且 LogicalControlAllowed=TRUE
```

### 10.3 解除与复位

解除使用 `Command=2`，最终要求 `State=1`、两个 InGear 均为 FALSE、`MemberControlAllowed=TRUE`。  
故障复位使用 `Command=3`。PLC 会先停止 SYN0，再依次解除实体轴齿轮，不得由上位机直接跳过清理流程。

## 11. 兼容区与禁止项

- D1260..D1299、D1300..D1308、D1224..D1226 属于旧联动兼容区，新龙门逻辑不依赖。
- M112..M127 报警解除触发当前未实现，禁止开放。
- B 组地址虽已存在，但当前状态机和校验逻辑不允许启用。
- `ConfigCRC` 算法尚未确定，当前只能写 0。
- 结构体对齐空洞和 Reserved 字段不得扩展为自定义协议字段。
- 未形成稳定协议的错误码只保留原始数值，不得从旧头文件猜测文本。

## 12. 上线验收

1. FC01 读取 M205，FC05 写 ON，确认 PLC 下一扫描清 OFF。
2. 读写一个 INT D 字，确认 PDU 地址与 D 编号一致。
3. 写入/读回 `1.0f`，确认低地址为 `0x0000`、高地址为 `0x3F80`。
4. 读取 D1400..D1577，核对 Magic、版本、A 组 Role[0]/[1]/[5] 和 ConfigValid。
5. FC10 写 D180..D182 建立命令，确认 D192..D193 的 AckSeq 对齐。
6. 建立成功必须同时满足 State=3、两个 InGear 和 LogicalControlAllowed。
7. 解除成功必须同时满足 State=1、两个 InGear=FALSE、MemberControlAllowed=TRUE。

## 13. 权威性顺序

发生冲突时依次以以下来源为准：

1. 当前编译生成的 `VarNameAndAddr.csv`、`CrossTable.crs`：物理地址、总长度和编译布局。
2. 当前结构体、变量表、`MAIN.LD`、轴控及龙门状态机源码：类型、权限和运行语义。
3. 本文：对外稳定协议。
4. 旧附录和旧上位机头文件：仅用于迁移比对，不得覆盖当前地址和逻辑。
