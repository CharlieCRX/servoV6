# PLC 龙门联动控制逻辑

## 1. 目标

本文档定义 PLC 如何响应上位机的 A/B 组龙门联动操作：

- 建立联动；
- 解除联动；
- 联动故障复位；
- 向上位机反馈确定的联动状态和控制权限。

轴号映射以 `AxisTopology` 为唯一权威来源：

```text
A组：X1=L0/0，X2=L1/1，X=SYN0/13
B组：X1=L4/4，X2=L5/5，X=SYN1/14
```

状态机不得再次保存一份固定轴号。运行时应从每组的 X1、X2、X 角色解析 PLC 轴下标。

## 2. 总体数据关系

```text
AxisTopology                 GantryParam
角色到PLC轴的映射             方向、比例、偏差和超时参数
         │                         │
         └──────────┬──────────────┘
                    ▼
上位机 ── GantryCommand ──► A/B组联动状态机
                    │
                    ├─ 控制GearIn/GearOut
                    ├─ 管理X、X1、X2控制权
                    ├─ 监控成员状态和龙门偏差
                    └─ 输出GantryStatus ──► 上位机
```

上位机只对“组”发出建立或解除命令，不直接选择 SYN、X1、X2，也不直接修改实体轴的 GearIn 执行条件。

## 3. PLC数据结构

以下定义只使用当前 PLC 支持的 `BOOL`、`INT`、`DINT` 和 `REAL`。当前 Easy522 版本的自定义枚举可用于同类型赋值和配置显示，但不能直接用于 `IF` 关系比较或作为 `CASE` 的整数表达式，因此运行命令、运行状态和命令结果统一使用 `INT` 编码；轴属性等静态描述仍可继续使用枚举。

### 3.1 命令编码

| 名称 | 值 | 说明 |
| --- | :--: | :-- |
| `GANTRY_CMD_NONE` | 0 | 无命令 |
| `GANTRY_CMD_COUPLE` | 1 | 建立联动 |
| `GANTRY_CMD_DECOUPLE` | 2 | 解除联动 |
| `GANTRY_CMD_RESET` | 3 | 联动故障复位 |

`GANTRY_CMD_*` 仅作为本文档中的语义名称，PLC 结构体和运行逻辑实际保存、比较对应的 `INT` 数值。

### 3.2 状态编码

| 名称 | 值 | 上位机含义 |
| --- | :--: | --- |
| `GANTRY_STATE_UNCONFIGURED` | 0 | 未配置 |
| `GANTRY_STATE_DECOUPLED` | 1 | 已配置、已解除联动 |
| `GANTRY_STATE_COUPLING` | 2 | 建立联动中 |
| `GANTRY_STATE_COUPLED` | 3 | 已建立联动 |
| `GANTRY_STATE_DECOUPLING` | 4 | 解除联动中 |
| `GANTRY_STATE_FAULT` | 5 | 联动故障 |

`GANTRY_STATE_*` 仅作为本文档中的语义名称，PLC 结构体和运行逻辑实际保存、比较对应的 `INT` 数值。

### 3.3 命令结果编码

| 名称 | 值 | 说明 |
| --- | :--: | --- |
| `GANTRY_RESULT_NONE` | 0 | 无结果 |
| `GANTRY_RESULT_PROCESSING` | 1 | 处理中 |
| `GANTRY_RESULT_SUCCESS` | 2 | 成功 |
| `GANTRY_RESULT_REJECTED` | 3 | 条件不满足，命令被拒绝 |
| `GANTRY_RESULT_FAILED` | 4 | 执行失败 |

`GANTRY_RESULT_*` 仅作为本文档中的语义名称，PLC 结构体和运行逻辑实际保存、比较对应的 `INT` 数值。

### 3.4 联动参数 ST_GantryParam

```iecst
TYPE ST_GantryParam :
STRUCT
    Valid               : BOOL;     // 当前联动组参数是否有效

    DirectionX1         : INT;      // X1位置和齿轮方向，只允许+1或-1
    DirectionX2         : INT;      // X2位置和齿轮方向，只允许+1或-1

    RatioNumeratorX1    : INT;      // X1电子齿轮比分子
    RatioDenominatorX1  : INT;      // X1电子齿轮比分母，不能为0
    RatioNumeratorX2    : INT;      // X2电子齿轮比分子
    RatioDenominatorX2  : INT;      // X2电子齿轮比分母，不能为0

    PositionOffsetX1    : REAL;     // X1归一化机械位置偏置，单位mm
    PositionOffsetX2    : REAL;     // X2归一化机械位置偏置，单位mm

    CoupleSkewLimit     : REAL;     // 允许建立联动的最大龙门偏差，单位mm
    RunningSkewLimit    : REAL;     // 已联动运行时允许的最大龙门偏差，单位mm

    SkewDelayMs         : DINT;     // 运行超差持续判定时间，单位ms
    CoupleTimeoutMs     : DINT;     // 建立联动命令超时时间，单位ms
    DecoupleTimeoutMs   : DINT;     // 解除联动命令超时时间，单位ms

    Reserved            : INT;      // 预留字段，当前固定为0
END_STRUCT
END_TYPE
```

```iecst
GantryParam : ARRAY[0..1] OF ST_GantryParam;
```

约定：

```text
GantryParam[0] = A组
GantryParam[1] = B组
Direction只能为+1或-1
RatioDenominator不能为0
```

### 3.5 上位机命令 ST_GantryCommand

GantryCommand不能保持。

```iecst
TYPE ST_GantryCommand :
STRUCT
    Command       : INT;   // 0=无命令，1=建立联动，2=解除联动，3=故障复位
    RequestSeq    : DINT;  // 上位机命令序号，每次发送新命令时递增
    Reserved      : INT;   // 预留字段，当前固定为0
END_STRUCT
END_TYPE
```

```iecst
GantryCommand : ARRAY[0..1] OF ST_GantryCommand;
```

上位机应先写 `Command`，最后递增并写入 `RequestSeq`。PLC 仅在检测到新序号时接收一次命令，避免 Modbus 脉冲丢失或重复执行。

### 3.6 状态反馈 ST_GantryStatus

GantryStatus不需要保持。

```iecst
TYPE ST_GantryStatus :
STRUCT
    State                   : INT;   // 0=未配置，1=已解除，2=建立中，3=已联动，4=解除中，5=故障
    InternalStep            : INT;   // PLC联动状态机当前内部执行步骤

    AckSeq                  : DINT;  // PLC已接收或已处理的上位机命令序号
    CommandResult           : INT;   // 0=无结果，1=处理中，2=成功，3=拒绝，4=失败
    CommandErrorCode        : INT;   // 本次命令被拒绝或执行失败的原因码

    ReadyToCouple           : BOOL;                 // 当前是否满足建立联动的基本条件
    ReadyToDecouple         : BOOL;                 // 当前是否允许执行正常解除联动
    MemberControlAllowed    : BOOL;                 // 是否允许上位机独立控制X1和X2实体轴
    LogicalControlAllowed   : BOOL;                 // 是否允许上位机控制X龙门逻辑轴

    X1InGear                : BOOL;                 // X1实体轴是否已成功加入逻辑轴电子齿轮
    X2InGear                : BOOL;                 // X2实体轴是否已成功加入逻辑轴电子齿轮

    X1Position              : REAL;                 // X1经过方向和偏置归一化后的机械位置，单位mm
    X2Position              : REAL;                 // X2经过方向和偏置归一化后的机械位置，单位mm
    LogicalPosition         : REAL;                 // X1和X2归一化位置的平均值，单位mm
    Skew                    : REAL;                 // 龙门两侧位置差X1Position-X2Position，单位mm

    Fault                   : BOOL;                 // 当前联动组是否存在需要复位的联动故障
    FaultCode               : INT;                  // 当前联动组故障原因码
    Reserved                : INT;                  // 预留字段，当前固定为0
END_STRUCT
END_TYPE
```

```iecst
GantryStatus : ARRAY[0..1] OF ST_GantryStatus;
```

### 3.7 PLC内部运行数据 ST_GantryRuntime

```iecst
TYPE ST_GantryRuntime :
STRUCT
    LastRequestSeq        : DINT;  // PLC最后一次接收并锁存的上位机命令序号
    ActiveCommand         : INT;   // 0=无命令，1=建立联动，2=解除联动，3=故障复位

    X1AxisIndex           : INT;                // 从AxisTopology解析出的X1实体轴PLC下标
    X2AxisIndex           : INT;                // 从AxisTopology解析出的X2实体轴PLC下标
    LogicalAxisIndex      : INT;                // 从AxisTopology解析出的X逻辑轴PLC下标

    InternalStep          : INT;                // PLC联动状态机当前内部执行步骤
    StepStartTimeMs       : DINT;               // 当前步骤开始时的系统毫秒时间，用于超时判断

    X1GearInSucceeded     : BOOL;               // 本次建立过程中X1是否已成功GearIn
    X2GearInSucceeded     : BOOL;               // 本次建立过程中X2是否已成功GearIn
    X1GearOutSucceeded    : BOOL;               // 本次解除或回滚过程中X1是否已成功GearOut
    X2GearOutSucceeded    : BOOL;               // 本次解除或回滚过程中X2是否已成功GearOut

    RollbackRequired      : BOOL;               // 建立过程部分成功后是否需要执行解除回滚
    InternalErrorCode     : INT;                // 状态机内部执行错误码
END_STRUCT
END_TYPE
```

```iecst
    GantryRuntime : ARRAY[0..1] OF ST_GantryRuntime;
```

该结构只供 PLC 内部使用，不掉电保持。

GantryRuntime必须清零：上电统一回到初始化步骤。

放置位置为：

| 变量               | 放置位置   | 主要写入者               | 上电保持 |
| ------------------ | ---------- | ------------------------ | -------- |
| `AxisTopology`     | 控制变量表 | PLC初始化/工程配置       | 是       |
| `GantryParam[2]`   | 控制变量表 | PLC初始化/上位机调试配置 | 是       |
| `GantryCommand[2]` | 控制变量表 | 上位机                   | 否       |
| `GantryStatus[2]`  | 状态表     | PLC                      | 否       |
| `GantryRuntime[2]` | 普通变量表 | PLC内部状态机            | 否       |

## 4. 从AxisTopology解析轴号

每组的角色约定为：

```text
Role[0] = X1
Role[1] = X2
Role[5] = X逻辑轴
```

解析示意：

```iecst
GantryRuntime[GroupIndex].X1AxisIndex :=
    AxisTopology.Group[GroupIndex].Role[0].PlcAxisIndex;

GantryRuntime[GroupIndex].X2AxisIndex :=
    AxisTopology.Group[GroupIndex].Role[1].PlcAxisIndex;

GantryRuntime[GroupIndex].LogicalAxisIndex :=
    AxisTopology.Group[GroupIndex].Role[5].PlcAxisIndex;
```

解析后必须检查：

1. `AxisTopology.ConfigValid=TRUE`；
2. 组、X1、X2、X角色均有效；
3. X1和X2下标在0～9；
4. X逻辑轴下标在13～15；
5. 三个轴没有重复或被其他组占用；
6. `GantryParam[GroupIndex].Valid=TRUE`；
7. 方向、比例、偏差和超时参数合法。

校验失败时进入状态0，不允许相关轴运动或建立联动。

## 5. 轴控制权

建议新增：

```iecst
AxisControlledByGantry : ARRAY[0..15] OF BOOL;
AxisCommandAllowed     : ARRAY[0..15] OF BOOL;
AxisOwnerGroup         : ARRAY[0..15] OF INT;
```

`AxisOwnerGroup=-1` 表示未被联动组占用，0表示A组，1表示B组。

控制权矩阵：

| 状态 | X1/X2单轴控制 | X逻辑轴控制 |
| --- | --- | --- |
| 未配置 | 禁止 | 禁止 |
| 已解除联动 | 允许 | 禁止 |
| 建立联动中 | 禁止 | 禁止 |
| 已建立联动 | 禁止 | 允许 |
| 解除联动中 | 禁止 | 禁止 |
| 联动故障 | 禁止 | 禁止 |

PLC 的单轴控制入口必须检查 `AxisCommandAllowed[]`。不能只依靠上位机隐藏按钮，否则通信错误或第三方客户端仍可能向被联动接管的实体轴发送命令。

Y、Z、R轴不受X龙门状态影响。

## 6. 位置和龙门偏差

应先把两个实体轴位置归一化到相同机械方向：

```iecst
X1Position :=
    GantryParam[GroupIndex].DirectionX1
    * AxisCurPosition[X1AxisIndex]
    + GantryParam[GroupIndex].PositionOffsetX1;

X2Position :=
    GantryParam[GroupIndex].DirectionX2
    * AxisCurPosition[X2AxisIndex]
    + GantryParam[GroupIndex].PositionOffsetX2;

Skew := X1Position - X2Position;
LogicalPosition := (X1Position + X2Position) / 2.0;
```

不能继续使用硬编码的：

```text
ABS(轴1位置 + 轴2位置)
```

因为该表达式隐含了固定安装方向。统一使用：

```text
ABS(Skew)
```

## 7. 建立联动流程

### 7.1 接收命令

状态1收到新的 `GANTRY_CMD_COUPLE` 后：

```text
AckSeq                 = RequestSeq
CommandResult          = PROCESSING
State                  = COUPLING
MemberControlAllowed   = FALSE
LogicalControlAllowed  = FALSE
```

### 7.2 建立条件检查

至少检查：

- 拓扑和联动参数有效；
- X1、X2均已使能、无轴故障和驱动故障；
- X1、X2均已停止；
- 没有定位、点动、回零、GearIn或GearOut命令正在执行；
- 位置或回零状态有效；
- 两个实体轴未被其他组占用；
- `ABS(Skew) <= CoupleSkewLimit`。

条件不满足时返回状态1：

```text
CommandResult        = REJECTED
CommandErrorCode     = 对应原因
MemberControlAllowed = TRUE
```

命令被拒绝不等同于联动故障。

### 7.3 对齐虚轴位置

建立GearIn之前，将X逻辑轴位置设置为 `LogicalPosition`，避免实体轴加入虚轴时产生位置跳变。

### 7.4 加入两个成员

依次建立：

```text
X逻辑轴 → X1实体轴
X逻辑轴 → X2实体轴
```

A组默认关系：

```text
SYN0/13 → L0/0
SYN0/13 → L1/1
```

B组默认关系：

```text
SYN1/14 → L4/4
SYN1/14 → L5/5
```

只有 `X1InGear=TRUE` 且 `X2InGear=TRUE` 才能进入状态3：

```text
State                  = COUPLED
CommandResult          = SUCCESS
MemberControlAllowed   = FALSE
LogicalControlAllowed  = TRUE
```

虚轴 `轴Status[13..15]` 只能说明虚轴已使能，不能作为联动成功判据。

### 7.5 建立失败回滚

如果X1已经GearIn、X2建立失败，PLC必须：

1. 禁止X、X1、X2运动；
2. 对已经成功的成员执行GearOut；
3. 确认两个成员均已解除；
4. 进入故障状态并记录错误码。

建立联动是一个整体事务，不能出现只成功一个成员却向上位机报告成功或已解除。

## 8. 已联动状态监控

状态3只允许控制X逻辑轴。PLC持续监控：

- X1、X2的InGear状态；
- X、X1、X2的轴故障和驱动故障；
- 实体轴使能状态；
- `ABS(Skew)`；
- GearIn关系是否意外丢失。

当：

```text
ABS(Skew) > RunningSkewLimit
```

持续超过 `SkewDelayMs`，或者任一成员丢失联动时：

1. 停止逻辑轴；
2. 必要时直接停止两个实体轴；
3. 禁止三个轴的运动命令；
4. 进入状态5；
5. 输出明确的 `FaultCode`。

## 9. 解除联动流程

状态3收到新的 `GANTRY_CMD_DECOUPLE` 后：

```text
AckSeq                 = RequestSeq
CommandResult          = PROCESSING
State                  = DECOUPLING
MemberControlAllowed   = FALSE
LogicalControlAllowed  = FALSE
```

执行顺序：

1. 停止X逻辑轴；
2. 等待X、X1、X2全部静止；
3. 对X1执行GearOut；
4. 对X2执行GearOut；
5. 确认两个成员均不再InGear；
6. 确认GearOut不忙、无错误；
7. 返回状态1。

成功反馈：

```text
State                  = DECOUPLED
CommandResult          = SUCCESS
MemberControlAllowed   = TRUE
LogicalControlAllowed  = FALSE
```

任一GearOut失败或超时：

```text
State                  = FAULT
CommandResult          = FAILED
MemberControlAllowed   = FALSE
LogicalControlAllowed  = FALSE
```

只有PLC明确反馈状态1后，上位机才能开放X1/X2单轴控制。

## 10. 故障复位

收到 `GANTRY_CMD_RESET` 后，不能直接清除状态。必须确认：

- X1、X2均已停止；
- GearIn和GearOut均不忙；
- 两个实体轴都确定未处于联动状态；
- 驱动器和轴故障已经复位；
- `AxisTopology` 和 `GantryParam` 仍有效。

全部满足后才允许：

```text
State                  = DECOUPLED
Fault                  = FALSE
FaultCode              = 0
MemberControlAllowed   = TRUE
LogicalControlAllowed  = FALSE
```

无法确认两个成员已经解除时，必须保持状态5。

## 11. 内部步骤建议

对外只发布0～5六种状态；PLC内部可使用以下步骤：

| InternalStep | 说明 |
| ---: | --- |
| 0 | 初始化和拓扑校验 |
| 10 | 已解除联动，等待命令 |
| 20 | 建立条件检查 |
| 30 | 对齐虚轴位置 |
| 40 | X1 GearIn |
| 50 | X2 GearIn |
| 60 | 验证两个成员均InGear |
| 70 | 已联动监控 |
| 80 | 停止逻辑轴，准备解除 |
| 90 | X1 GearOut |
| 100 | X2 GearOut |
| 110 | 验证完全解除 |
| 120 | 建立失败回滚 |
| 900 | 故障清理 |
| 910 | 等待故障复位 |

## 12. 与当前PLC程序的衔接

### 12.1 拆分“配置”和“执行”

当前 `联动成员信息[].使能` 同时表示成员配置和GearIn执行条件，需要拆分为：

```text
AxisTopology                成员轴配置
GantryCommand               上位机运行命令
GantryRuntime.InternalStep  当前执行步骤
GearInExecute[]             底层GearIn请求
GearOutExecute[]            底层GearOut请求
```

解除联动后只清理运行关系，不修改 `AxisTopology` 和 `GantryParam`。

### 12.2 修正“是否主动”的含义

当前核心中，SYN0～SYN2是电子齿轮Master，L0～L9是Slave。X1“基准侧”并不表示它是PLCopen电子齿轮的Master。

原有 `是否主动=+1/-1` 实际表达方向，应改为：

```text
Direction
RatioNumerator
RatioDenominator
```

### 12.3 保留静态运动块，增加调度层

如果PLC轴引用不能动态索引，可以保留现有L0～L9静态 `MC_GearIn/MC_GearOut` 实例，在其上增加统一数组：

```iecst
GearInExecute       : ARRAY[0..9] OF BOOL;
GearOutExecute      : ARRAY[0..9] OF BOOL;
GearMasterAxisIndex : ARRAY[0..9] OF INT;
GearDirection       : ARRAY[0..9] OF INT;

GearInGear          : ARRAY[0..9] OF BOOL;
GearInBusy          : ARRAY[0..9] OF BOOL;
GearInError         : ARRAY[0..9] OF BOOL;
GearInErrorCode     : ARRAY[0..9] OF INT;

GearOutDone         : ARRAY[0..9] OF BOOL;
GearOutBusy         : ARRAY[0..9] OF BOOL;
GearOutError        : ARRAY[0..9] OF BOOL;
GearOutErrorCode    : ARRAY[0..9] OF INT;
```

组状态机产生请求，静态运动块执行，反馈数组再返回组状态机汇总。

## 13. 错误码建议

| 错误码 | 含义 |
| ---: | --- |
| 0 | 无错误 |
| 100 | AxisTopology无效 |
| 101 | 分组配置无效 |
| 102 | X1/X2/X角色错误 |
| 103 | PLC轴下标冲突 |
| 110 | 当前状态不允许该命令 |
| 111 | 不支持的命令 |
| 120 | 实体轴未使能 |
| 121 | 实体轴仍在运动 |
| 122 | 实体轴或驱动器故障 |
| 123 | 位置或回零状态无效 |
| 124 | 建立前龙门偏差过大 |
| 130 | X1 GearIn失败 |
| 131 | X2 GearIn失败 |
| 132 | 建立联动超时 |
| 140 | X1 GearOut失败 |
| 141 | X2 GearOut失败 |
| 142 | 解除联动超时 |
| 150 | 联动运行中成员脱离 |
| 151 | 联动运行中龙门超差 |
| 152 | 联动运行中实体轴掉使能 |

`CommandErrorCode` 表示本次命令为何被拒绝或失败；`FaultCode` 表示联动组为何处于故障状态。

## 14. 掉电处理

掉电保持：

```text
AxisTopology
GantryParam
```

不掉电保持：

```text
GantryCommand
GantryStatus
GantryRuntime
GearIn/GearOut执行和反馈
AxisControlledByGantry
```

PLC重启后不能根据上次记忆直接进入状态3。推荐流程：

1. 校验拓扑和联动参数；
2. 解析X、X1、X2下标；
3. 禁止三个轴运动；
4. 确认或清理残留GearIn关系；
5. 确认两个成员均已解除；
6. 配置有效则进入状态1；
7. 配置无效则进入状态0。

## 15. 上位机与PLC的最终约定

```text
PLC反馈DECOUPLED
    → 上位机展示并允许控制X1/X2

PLC反馈COUPLED
    → 上位机展示并允许控制X逻辑轴

PLC反馈其他状态
    → X、X1、X2全部禁止运动
```

上位机界面是否开放控制，以 PLC 输出的 `MemberControlAllowed` 和 `LogicalControlAllowed` 为最终依据。
