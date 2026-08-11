# PLC 变量协议与 Modbus 最终地址表

> 基线：`PLC_re` 当前工作区，2026-08-10。  
> 适用范围：标准 16 轴控制、轴拓扑、A 组龙门联动和上位机接口。  
> 地址依据：`VarNameAndAddr.csv`、变量表/结构体、`MAIN.LD`、`SBR_龙门齿轮.LD` 和仓库内 Modbus 测试程序。  
> 约束：本文没有从旧 `RegisterAddressAll.h` 补地址。不能由当前工程或既有实测记录确认的内容均标为“待定”。

## 1. 结论摘要

1. PLC 变量地址按 **0 基址**使用：`M192` 的 Modbus PDU 线圈地址就是 `192`，不加 `1`，也不写成 `000193`；D 区同理按 D 编号作为保持寄存器原始地址，不加 `40001`。
2. 线圈使用 FC01/FC05/FC0F；D 区使用 FC03/FC06/FC10。32 位值必须优先用 FC10 一次写完。
3. `INT` 占 1 个 D 字，`DINT`/枚举/`REAL` 占 2 个 D 字，结构体内 BOOL 按 D 字 bit 打包，后续非 BOOL 字段按编译器布局对齐。
4. `REAL`/`DINT` 采用低字在低地址、高字在高地址：`u32 = D[n] | (D[n+1] << 16)`；每个 Modbus 寄存器内部仍是高字节在前。
5. 新龙门接口为：
   - `AxisTopology`：`D1400..D1577`
   - `GantryParam[0..1]`：`D1600..D1643`，单项步长 22 D
   - `GantryCommand[0..1]`：`D180..D187`，单项步长 4 D
   - `GantryStatus[0..1]`：`D190..D225`，单项步长 18 D
6. 当前只实现并开放 A 组：`g=0`，X1=L0/轴下标0，X2=L1/轴下标1，逻辑轴 X=SYN0/轴下标13。B 组地址存在，但 PLC 当前要求 `Group[1].Valid=FALSE`，不得开放控制。
7. 新联动不再由旧 `联动成员信息`、`联动轴信息` 和 `超差阈值`驱动；这些地址只作为旧接口兼容区保留。
8. 龙门命令不使用线圈脉冲，而使用 `Command + RequestSeq` 事务。先写命令码，再改变序号；以 `AckSeq` 和 `CommandResult` 判断接收与完成。

## 2. 地址基准与传输规则

### 2.1 原始地址

| PLC 区域 | Modbus 数据区 | 读 | 单写 | 批量写 | PDU 原始地址 |
| --- | --- | --- | --- | --- | --- |
| M | Coil | FC01 | FC05 | FC0F | 等于 M 编号 |
| D | Holding Register | FC03 | FC06 | FC10 | 等于 D 编号 |

示例：

```text
点动心跳[13] = M192 + 13 = M205
FC05 PDU address = 205 (0x00CD)

定位速度[13] = D32 + 2*13 = D58..D59
FC03/FC10 PDU start address = 58 (0x003A)
```

不要把显示型地址 `00001/40001` 加到 PDU 地址中。若所用 Modbus 库只接受 `40001` 风格，应在适配层换算，业务协议仍以本文的原始 0 基址为准。

### 2.2 Unit ID、端口和设备 IP

| 项目 | 值 | 状态 |
| --- | --- | --- |
| Modbus TCP 端口 | 502 | 已由仓库测试程序固定 |
| Unit ID | 1 | 已由仓库测试程序默认并由旧实测协议使用 |
| PLC IP | 待定 | `jog_heartbeat.py` 的 `192.168.1.88` 只是脚本默认值，不应成为产品协议常量 |

### 2.3 32 位字序

```text
D[n]   = 低 16 位
D[n+1] = 高 16 位

u32 = (uint32(D[n+1]) << 16) | uint32(D[n])
REAL = reinterpret_float32(u32)
DINT = reinterpret_int32(u32)
```

写入时：

```text
D[n]   = u32 & 0xFFFF
D[n+1] = (u32 >> 16) & 0xFFFF
```

例如 `25.8f = 0x41CE6666`：

```text
D[n]   = 0x6666
D[n+1] = 0x41CE
```

字序结论有既有 PLC 读回记录支持；当前仓库的编译地址表只独立证明“每个 32 位值连续占两个 D 字”。更换 PLC 固件、Modbus 网关或通信库后，仍应以固定值 `1.0f = 0x3F800000` 做一次上线读回验收。

## 3. 标准 16 轴地址表

数组下标 `i=0..15`，必须来自 `AxisTopology...PlcAxisIndex` 或明确的 PLC 轴绑定。

### 3.1 D 区

| 变量 | 基址 | 长度 | 类型 | 单项步长 | 第 i 项 | 权限 | 说明 |
| --- | ---: | ---: | --- | ---: | --- | --- | --- |
| 手动速度 | D0 | 16 | REAL | 2 D | `D(0+2i)..D(1+2i)` | RW | EU/s |
| 定位速度 | D32 | 16 | REAL | 2 D | `D(32+2i)..D(33+2i)` | RW | EU/s |
| 绝对位置 | D64 | 16 | REAL | 2 D | `D(64+2i)..D(65+2i)` | R | 当前绝对位置 |
| 相对位置 | D96 | 16 | REAL | 2 D | `D(96+2i)..D(97+2i)` | R | 绝对位置减相对原点 |
| 运动状态 | D128 | 16 | INT | 1 D | `D(128+i)` | R | 见 3.3 |
| 运动限制 | D144 | 16 | INT | 1 D | `D(144+i)` | R | 见 3.3 |
| 告警码 | D160 | 16 | WORD | 1 D | `D(160+i)` | R | 位掩码 |
| 相对原点记录 | D1064 | 16 | REAL | 2 D | `D(1064+2i)..` | RW | 保持，EU |
| 绝对定位距离 | D1096 | 16 | REAL | 2 D | `D(1096+2i)..` | RW | 保持，EU |
| 相对定位距离 | D1128 | 16 | REAL | 2 D | `D(1128+2i)..` | RW | 保持，EU |
| 软件负限位 | D1160 | 16 | REAL | 2 D | `D(1160+2i)..` | RW | 保持，EU |
| 软件正限位 | D1192 | 16 | REAL | 2 D | `D(1192+2i)..` | RW | 保持，EU |
| 超差阈值 | D1224 | 3 | INT | 1 D | `D(1224+i)` | 兼容 | 旧联动接口；新龙门不用 |
| 软限位控制 | D1228 | 16 | WORD | 1 D | `D(1228+i)` | RW | bit0 正软限位；bit1 负软限位 |
| 职责 | D1244 | 16 | INT | 1 D | `D(1244+i)` | 兼容/待定 | 当前新逻辑不消费，枚举协议未确定 |

位置和速度单位是轴工程单位 EU/EU/s；拓扑 `UnitType` 可说明 mm 或 degree，但不能把全部轴统一假定为 mm。

### 3.2 M 区及 ON/OFF 责任

| 变量 | 基址/范围 | 语义 | 上位机是否必须产生 ON→OFF |
| --- | --- | --- | --- |
| 使能轴控[i] | M0..M15 | 保持电平 | 是，按启停状态写 ON/OFF |
| 相对原点清除[i] | M16..M31 | 上升沿命令 | PLC 接收后自复位；客户端无需配对 OFF，但应读回确认 |
| 绝对位置清零[i] | M32..M47 | 上升沿命令 | PLC 接收后自复位；客户端无需配对 OFF，但应读回确认 |
| 绝对定位触发[i] | M48..M63 | 上升沿启动 | **需要**；客户端写 ON 后必须回写 OFF，不能只等运动完成清除 |
| 相对定位触发[i] | M64..M79 | 上升沿启动 | **需要**；客户端写 ON 后必须回写 OFF |
| 点动正转[i] | M80..M95 | 保持电平 | 是；按住 ON、松开 OFF，不是脉冲 |
| 点动反转[i] | M96..M111 | 保持电平 | 是；按住 ON、松开 OFF，不是脉冲 |
| 报警解除触发[i] | M112..M127 | 原设计为上升沿 | **不可用**；当前执行和自复位代码被注释，禁止正式 UI 使用 |
| 使能电机[i] | M128..M143 | 保持电平 | 是，按启停状态写 ON/OFF |
| 相对定位终止触发[i] | M144..M159 | 上升沿停止 | **需要**；PLC 只在相应运动条件成立时清除，客户端必须保证回落 |
| 绝对定位终止触发[i] | M160..M175 | 上升沿停止 | **需要**；PLC 只在相应运动条件成立时清除，客户端必须保证回落 |
| 相对原点设置[i] | M176..M191 | 上升沿命令 | PLC 接收后自复位；客户端无需配对 OFF，但应读回确认 |
| 点动心跳[i] | M192..M207 | 看门狗刷新 | 周期写 ON；PLC 每扫描周期写 OFF。停止点动时补一次 OFF，不必每次配对 OFF |
| 告警码置零[i] | M208..M223 | 电平检测的一次命令 | PLC 执行后自复位；客户端无需配对 OFF，但应读回确认 |
| 设备急停 | M224 | 软件急停锁存 | 不做短脉冲；写 ON 后保持，由解除命令清除 |
| 设备急停解除 | M225 | 上升沿解除 | PLC 自复位，并同时清除 M224；客户端应读回 M224/M225 |

统一客户端规则：

- 保持电平：本机负责最终 OFF。
- 客户端脉冲：写 ON，确认至少跨过一个 PLC 扫描或看到状态变化，再写 OFF；通信异常时重连后先清 OFF。
- PLC 自复位：只写 ON，等待 PLC 读回 OFF；若超时未复位，报告故障并主动清 OFF。
- 点动期间每 0.5～1 s 向对应心跳线圈写一次 ON；PLC 超时门限为 3000 ms。
- M224 是软件急停，不替代安全等级硬接线急停。

### 3.3 标准状态编码

运动状态：`0=轴控入口未使能，1=空闲，2=正向点动，3=反向点动，4=绝对定位，5=相对定位`。

运动限制：`0=无限位，1=正软限位，2=负软限位，3=正硬限位，4=负硬限位`。

告警码：

| 位 | 掩码 | 含义 |
| --- | ---: | --- |
| bit0 | 0x0001 | 软件急停 |
| bit1 | 0x0002 | 旧联动职责占用冲突；新龙门不使用 |
| bit2 | 0x0004 | 旧联动建立超差；新龙门不使用 |
| bit3 | 0x0008 | 旧联动成员配置错误；新龙门不使用 |
| bit4 | 0x0010 | 旧联动运行超差；新龙门以 GantryStatus/FaultCode 为准 |
| bit5 | 0x0020 | 轴功能块、伺服或轴命令错误 |
| bit6 | 0x0040 | 点动心跳超时 |
| bit7..15 | — | 保留 |

## 4. 旧联动兼容区（新接口禁止依赖）

### 4.1 联动成员信息

`联动成员信息[0..9]`：基址 D1260，单项步长 4 D。

```text
member(i) = D1260 + 4*i
```

| 偏移 | 字段 | 类型 |
| --- | --- | --- |
| +0.bit0 | 使能 | BOOL |
| +1 | 联动轴号 | INT |
| +2..+3 | 是否主动 | DINT |

### 4.2 联动轴信息

`联动轴信息[0..2]`：基址 D1300，**单项步长 3 D**。

```text
legacy_group(g) = D1300 + 3*g
```

| 偏移 | 字段 | 类型 |
| --- | --- | --- |
| +0 | 主动轴 | INT |
| +1 | 从动轴 | INT |
| +2.bit0 | 联动轴有效 | BOOL |

旧附录中的“每项 4 D”与当前编译结果不符；当前 `VarNameAndAddr.csv` 总大小为 144 bit，`CrossTable.crs` 也明确落在 `D1300..D1308`，因此最终步长是 3 D。

## 5. 轴拓扑 `AxisTopology`

### 5.1 总体布局

`AxisTopology` 基址 D1400，总长 178 D（D1400..D1577）。

| 地址 | 字段 | 类型 | 权限 |
| --- | --- | --- | --- |
| D1400..D1401 | Magic | DINT | RW（维护） |
| D1402 | SchemaVersion | INT | RW（维护） |
| D1403 | Reserved | INT | 写 0 |
| D1404..D1405 | Revision | DINT | RW（维护，配置提交时递增） |
| D1406..D1407 | 编译器对齐空洞 | — | 禁止读写依赖 |
| D1408..D1490 | Group[0]（A 组） | ST_GroupAxisMap | RW/PLC 校验 |
| D1491..D1573 | Group[1]（B 组） | ST_GroupAxisMap | 仅保持禁用 |
| D1574..D1575 | ConfigCRC | DINT | RW（当前固定 0） |
| D1576.bit0 | ConfigValid | BOOL | **R，PLC 写** |
| D1577 | ConfigErrorCode | INT | **R，PLC 写** |

`D1406..D1407` 是编译布局中实际存在的对齐间隔，不得作为自定义字段使用。

### 5.2 Group 和 Role 地址公式

```text
group_base(g) = D1408 + 83*g, g=0..1
role_base(g,r) = group_base(g) + 3 + 10*r, r=0..7
```

组字段：

| 相对 group_base | 字段 | 类型 |
| --- | --- | --- |
| +0.bit0 | Valid | BOOL |
| +0.bit1 | HmiVisible | BOOL |
| +1 | GroupCode | INT；0=A，1=B |
| +2 | Reserved | INT，写 0 |
| +3..+82 | Role[0..7] | 每项 10 D |

角色字段：

| 相对 role_base | 字段 | 类型/编码 |
| --- | --- | --- |
| +0.bit0 | Valid | BOOL |
| +0.bit1 | HmiVisible | BOOL |
| +1 | PlcAxisIndex | INT，0..15；无效角色写 -1 |
| +2 | MotorNo | INT；虚轴为 0 |
| +3..+4 | AxisClass | 32 位枚举：0线性、1旋转、2虚轴 |
| +5..+6 | UnitType | 32 位枚举：0=mm、1=degree |
| +7..+8 | MotionMode | 32 位枚举：0未用、1龙门X1、2龙门X2、3线性单轴、4旋转单轴、5龙门逻辑轴 |
| +9 | Reserved | INT，写 0 |

A 组当前有效绑定：

| 角色 | 地址起点 | PlcAxisIndex | MotorNo | AxisClass | UnitType | MotionMode |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Role[0] X1 | D1411 | 0 | >0（当前 1） | 0 | 0 | 1 |
| Role[1] X2 | D1421 | 1 | >0 且不同于 X1（当前 2） | 0 | 0 | 2 |
| Role[5] X/SYN0 | D1461 | 13 | 0 | 2 | 0 | 5 |

当前 SchemaVersion=1 还要求：A 组 Role[2..4]、Role[6..7] 无效；B 组 `Group[1].Valid=FALSE`。否则 `ConfigValid=FALSE`。

主要拓扑错误码：`1 Magic错误，2版本错误，3/4组号错误，10 A组无效，20..22 X1错误，30..33 X2错误，40..42 SYN0错误，50未实现角色被启用，51预留角色被启用，60 B组被启用`。

## 6. 龙门参数 `GantryParam`

基址 D1600，数组长度 2，单项步长 22 D：

```text
param(g) = D1600 + 22*g
```

| 偏移 | 字段 | 类型 | 权限/约束 |
| --- | --- | --- | --- |
| +0.bit0 | Valid | BOOL | RW（维护） |
| +1 | DirectionX1 | INT | +1 或 -1 |
| +2 | DirectionX2 | INT | +1 或 -1 |
| +3 | RatioNumeratorX1 | INT | 非 0 |
| +4 | RatioDenominatorX1 | INT | 非 0 |
| +5 | RatioNumeratorX2 | INT | 非 0 |
| +6 | RatioDenominatorX2 | INT | 非 0 |
| +7..+8 | PositionOffsetX1 | REAL | mm |
| +9..+10 | PositionOffsetX2 | REAL | mm |
| +11..+12 | CoupleSkewLimit | REAL | >0，mm |
| +13..+14 | RunningSkewLimit | REAL | >= CoupleSkewLimit，mm |
| +15..+16 | SkewDelayMs | DINT | >0，当前建议 50..200 ms |
| +17..+18 | CoupleTimeoutMs | DINT | >0，当前建议 3000..10000 ms |
| +19..+20 | DecoupleTimeoutMs | DINT | >0，当前建议 3000..10000 ms |
| +21 | Reserved | INT | 写 0 |

A 组地址为 D1600..D1621；B 组地址为 D1622..D1643，但当前 B 组不执行。

## 7. 龙门命令 `GantryCommand`

基址 D180，数组长度 2，单项步长 4 D：

```text
command(g) = D180 + 4*g
```

| 偏移 | 字段 | 类型 | 权限 |
| --- | --- | --- | --- |
| +0 | Command | INT | RW；0无、1建立、2解除、3安全复位 |
| +1..+2 | RequestSeq | DINT | RW；每个新事务递增 |
| +3 | Reserved | INT | 写 0 |

A 组完整地址：`Command=D180`，`RequestSeq=D181..D182`，`Reserved=D183`。

推荐用一次 FC10 写 D180..D182，使 PLC 同一扫描看到一致的命令码和序号。若分两次写，必须先写 `Command`，最后写 `RequestSeq`。命令无需清零，也不需要 ON/OFF 脉冲；重复相同命令必须使用新的 `RequestSeq`。

## 8. 龙门状态 `GantryStatus`

基址 D190，数组长度 2，单项步长 18 D：

```text
status(g) = D190 + 18*g
```

| 偏移 | 字段 | 类型 | 权限 |
| --- | --- | --- | --- |
| +0 | State | INT | R |
| +1 | InternalStep | INT | R |
| +2..+3 | AckSeq | DINT | R |
| +4 | CommandResult | INT | R |
| +5 | CommandErrorCode | INT | R |
| +6.bit0 | ReadyToCouple | BOOL | R |
| +6.bit1 | ReadyToDecouple | BOOL | R |
| +6.bit2 | MemberControlAllowed | BOOL | R |
| +6.bit3 | LogicalControlAllowed | BOOL | R |
| +6.bit4 | X1InGear | BOOL | R |
| +6.bit5 | X2InGear | BOOL | R |
| +7..+8 | X1Position | REAL | R，mm |
| +9..+10 | X2Position | REAL | R，mm |
| +11..+12 | LogicalPosition | REAL | R，mm |
| +13..+14 | Skew | REAL | R，X1-X2，mm |
| +15.bit0 | Fault | BOOL | R |
| +16 | FaultCode | INT | R |
| +17 | Reserved | INT | R |

A 组完整范围 D190..D207；B 组完整范围 D208..D225。

状态编码：

| State | 含义 |
| ---: | --- |
| 0 | 未配置 |
| 1 | 已配置、已解除 |
| 2 | 建立中 |
| 3 | 已联动 |
| 4 | 解除中 |
| 5 | 联动故障 |

命令结果：`0无结果，1处理中，2成功，3拒绝，4失败`。

正式内部步骤：`0未配置，10已解除，20/30/35位置对齐，60 X1 GearIn，70 X2 GearIn，80已联动，150停止逻辑轴，160 X2 GearOut，170 X1 GearOut，910故障等待复位`。上位机业务以 State 为主，InternalStep 只用于诊断。

核心运行错误码：`100拓扑无效，101龙门参数无效，150联动健康丢失，151运行偏差持续超限，152轴状态/轴命令错误，153逻辑轴停止超时`。其他状态机原因码应直接显示原值；尚未形成稳定对外枚举的码标为待定，不应由旧头文件补文本。

## 9. 新的上位机接口流程

### 9.1 配置提交

只在相关轴停止、无联动、无 GearIn/GearOut/定位/点动命令且进入维护模式时修改：

1. 写 `AxisTopology` 的输入字段和 `GantryParam[0]`。
2. 保持所有 Reserved 和 D1406..D1407 为 0/不写。
3. 最后递增 `AxisTopology.Revision`。
4. 等待 PLC 更新 `D1576.bit0 ConfigValid` 和 `D1577 ConfigErrorCode`。
5. 不得写 ConfigValid、ConfigErrorCode、GantryStatus 或内部运行变量。

### 9.2 建立联动

建立按钮准入：

```text
ConfigValid = TRUE
GantryStatus[0].State = 1
GantryStatus[0].ReadyToCouple = TRUE
GantryStatus[0].Fault = FALSE
```

事务：

```text
FC10 D180..D182 = Command(1) + RequestSeq(N+1，低字在前)
等待 AckSeq == N+1
CommandResult == 1：处理中
CommandResult == 2 且 State == 3 且 X1InGear/X2InGear：建立成功
CommandResult == 3/4：按 CommandErrorCode 显示拒绝/失败
```

### 9.3 已联动后的 SYN0 运动

新状态机在 State=3 时禁止 L0/L1 独立运动，只允许逻辑轴 SYN0（轴下标13）。上位机继续使用标准公开轴变量，不应写 PLC 内部 `轴Move*` 数组：

| 操作 | SYN0 公共地址 |
| --- | --- |
| 轴控入口 | M13 |
| 电机使能 | M141 |
| 手动速度 | D26..D27 |
| 定位速度 | D58..D59 |
| 绝对目标 | D1122..D1123 |
| 相对目标 | D1154..D1155 |
| 绝对定位触发 | M61，客户端 ON→OFF |
| 相对定位触发 | M77，客户端 ON→OFF |
| 正向点动 | M93，保持电平 |
| 反向点动 | M109，保持电平 |
| 点动心跳 | M205，周期写 ON |
| 绝对位置 | D90..D91 |
| 相对位置 | D122..D123 |
| 运动状态 | D141 |
| 运动限制 | D157 |
| 告警码 | D173 |

只有 `GantryStatus[0].LogicalControlAllowed=TRUE` 时才可发送 SYN0 普通运动命令。只有 `MemberControlAllowed=TRUE` 时才可独立控制 L0/L1。

### 9.4 正常解除与故障复位

解除：用新序号发送 `Command=2`，等待 `AckSeq` 对齐，最终要求：

```text
State=1
CommandResult=2
X1InGear=FALSE
X2InGear=FALSE
MemberControlAllowed=TRUE
LogicalControlAllowed=FALSE
```

故障复位：用新序号发送 `Command=3`。若仍有成员 InGear，PLC 会先 Halt SYN0，再依次 GearOut，不能通过上位机直接写内部 GearOut Execute 跳过清理。

运行中拓扑或参数失效时，PLC 会关闭普通运动权限并走 `150→160→170→0` 安全清理。上位机在清理结束前不得提前开放实体轴控制。

## 10. 待定项与禁止猜测项

| 项目 | 当前处理 |
| --- | --- |
| PLC 产品 IP | 待定，由部署配置提供 |
| B 组龙门运行接口 | 待定；虽已有地址槽位，但状态机和静态 Gear 适配未完成，禁止启用 |
| `职责[D1244..D1259]` 枚举 | 待定；新状态机不消费 |
| M112..M127 报警解除 | 待定/未实现；不得开放 |
| 未列出的 Gantry 错误码文本 | 待定；保留原始数值，不从旧 `RegisterAddressAll.h` 补定义 |
| ConfigCRC 算法 | 待定；当前写 0，不能自创算法 |
| 任何结构体对齐空洞和 Reserved | 不属于扩展地址；禁止复用 |

## 11. 上线最小验收

1. 用 FC01 读取 M205、FC05 写 ON，确认 PLC 下一扫描将其清 OFF，验证线圈 0 基址。
2. 用安全的维护值读写一个 INT D 字，确认 D 编号与 PDU 地址一致。
3. 写入/读回 `1.0f`，确认寄存器为低地址 `0x0000`、高地址 `0x3F80`。
4. 读取 D1400..D1577，核对 Magic、版本、A 组 Role[0]/[1]/[5] 和 ConfigValid。
5. 用 FC10 写 D180..D182 发送建立命令，确认 D192..D193 AckSeq 对齐。
6. 建立成功必须同时满足 State=3、X1InGear、X2InGear 和 LogicalControlAllowed；不能只看 SYN0 轴状态。
7. 解除成功必须同时满足 State=1、两个 InGear 均为 FALSE、MemberControlAllowed=TRUE。

## 12. 权威性顺序

发生冲突时按以下顺序处理：

1. 当前编译生成的 `VarNameAndAddr.csv` 和 `CrossTable.crs`：物理基址、总大小、已引用字段物理位置。
2. 当前 `.stru`、`.gvt`、`MAIN.LD`、`SBR_龙门齿轮.LD`：类型、字段顺序、权限和运行语义。
3. 本文：对外稳定协议。
4. 旧附录和旧上位机头文件：只用于迁移比对，不得反向覆盖当前地址和逻辑。

