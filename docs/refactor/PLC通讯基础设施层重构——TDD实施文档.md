# PLC 通讯基础设施层重构——TDD 实施文档

> 状态：实施基线文档  
> 日期：2026-08-10  
> 前置依据：《PLC通讯基础设施层重构设计.md》、`tools/plc_read_validate.py` 及 `PLC_re/docs` 中的拓扑/联动契约。`PLC变量协议_Modbus最终地址表.md` 当前**不在本仓库**；在该文件补入并评审前，验证脚本是可执行参考，不是可追溯的最终规范。  
> 范围：仅 `infrastructure/plc_vnext` 与其测试；不调整 joystick、logger、QML、presentation、业务 Domain；旧 `infrastructure/plc` 迁移期间保留。

## 0. 实施总览与纪律

### 0.1 TDD 纪律（每步强制）

1. **红**：先写测试，确认它在目标 API 上**编译失败或断言失败**。
2. **绿**：写最小实现使该测试通过，不做多余功能。
3. **重构**：在绿灯前提下整理重复/命名，保持测试全绿。
4. **每步独立可编译、可被 `ctest` 发现**；不依赖旧测试是否恢复。
5. **只读先行**：M1~M3 一律不写 PLC；写路径只在 M4 起按命令类别受控切换。
6. **禁止双写**：任何会引起物理运动的命令，新旧两套实现不得同时写。

### 0.2 命名空间与目录隔离

- 新代码一律用命名空间 `plc_vnext`（子模块 `plc_vnext::contracts`、`plc_vnext::codec`、`plc_vnext::layout` …），**不得**与旧 `plc::protocol` 混用。
- 新 `CommunicationResult` 在 `contracts/CommunicationResult.h` 中**独立定义**，不 `#include "infrastructure/ISystemDriver.h"`。这是 transport 与 `ISystemDriver` 解耦的唯一路径。
- 目录统一置于 `infrastructure/plc_vnext/`，测试置于 `tests/infrastructure/plc_vnext/`，一一对应。

### 0.3 独立构建通道（Step 0 一次性搭好）

根 `CMakeLists.txt` 在 `add_subdirectory(tests)` **之前**插入：

```cmake
add_subdirectory(infrastructure/plc_vnext)   # 定义独立 STATIC 库 plc_vnext
```

`tests/CMakeLists.txt` 只追加独立子目录（不复用旧 `unit_tests`）：

```cmake
add_subdirectory(infrastructure/plc_vnext)
```

`tests/infrastructure/plc_vnext/CMakeLists.txt` 定义独立 target：

```cmake
add_executable(plc_vnext_tests
    contracts/test_plc_axis_slot.cpp
    contracts/test_plc_group_index.cpp
    # …… 每一步显式追加对应测试文件
)
target_include_directories(plc_vnext_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(plc_vnext_tests PRIVATE plc_vnext GTest::gtest GTest::gtest_main GTest::gmock)
gtest_discover_tests(plc_vnext_tests TEST_PREFIX "plc_vnext.")
```

`gtest_discover_tests` 应增加 `TEST_PREFIX "plc_vnext."`，因此验收命令使用 `ctest --test-dir build -R "^plc_vnext\."`。`ctest` 发现的是具体测试名，不是可执行 target 名；`ctest -R plc_vnext_tests` 不能证明测试真的被发现。

不要使用 `file(GLOB_RECURSE ...)` 自动收集测试。每个 Step 在 CMake 中显式加入新增测试文件，能让代码评审准确看到该 PR 增加了哪些行为，也避免未重新 configure 时测试悄然遗漏。

### 0.4 里程碑 ↔ 步骤映射

| 里程碑 | 本实施步骤 | 验收信号 |
| --- | --- | --- |
| M1 协议基础 | Step 1 contracts、Step 2 codec、Step 3 layout、Step 4 ReadPlanBuilder、Step 5 transport/Fake | 各 step 测试全绿，旧控制路径零改动 |
| M2 启动发现 | Step 6 topology | 能读/解码/校验拓扑并输出 TopologySnapshot（只读） |
| M3-A PLC 协议现场验证 | Step 7 的前置证据 | 16 槽位地址、数量、字序与 PLC 现场值一致 |
| M3-B vNext 影子反馈 | Step 7 telemetry | 能读 16 槽位+龙门状态并给出可信度；并排对比日志 |
| M4 单轴写入 | Step 8 command（单轴） | 一个命令类别受控切换；禁止双写运动触发 |
| M5 联动通讯 | Step 9 command（龙门）、Step 10 Gateway | 仅请求/确认通路 |
| M6 上层迁移准备 | Step 11 fake 收尾 + 真实 PLC 只读验收、Step 12 影子迁移与交接 | Gateway 输出、Fake 与证据可供上层迁移使用；legacy 保留 |

### 0.5 与 `plc_read_validate.py` 的对拍方法

每个只读 step（layout / topology / telemetry）完成后，用验证脚本先落一份**带日期、PLC 程序版本和 Revision 的人工审查文本证据**：

```bash
python tools/plc_read_validate.py --only topology > evidence\topology-YYYYMMDD.txt
python tools/plc_read_validate.py --only axis     > evidence\axis-YYYYMMDD.txt
python tools/plc_read_validate.py --only status   > evidence\status-YYYYMMDD.txt
```

脚本当前输出的是格式化文本而非 JSON；因此字段级对拍必须由固定测试 fixture + 明确字段清单完成，人工文本只作为现场证据。发现脚本、PLC 文档和现场结果不一致时，停止迁移并先补充版本化地址表与 fixture；不得让任一方静默成为“猜测来源”。

### 0.6 已冻结的拓扑 ABI 与待关闭的 telemetry 阻塞项

已由 PLC 变量监控确认（2026-08-10）：`ST_GroupAxisMap.Role` 的 PLC ABI 容量为 **8**（`ST_RoleAxisBinding[8]`），组头为 3 D、每个 role 为 10 D、每组步长为 83 D。业务角色仅使用前 6 项：

| ABI role 下标 | 业务含义 |
| ---: | :-- |
| 0 | X1 |
| 1 | X2 |
| 2 | Y |
| 3 | Z |
| 4 | R |
| 5 | X 逻辑轴 |
| 6 | 预留，不创建业务轴 |
| 7 | 预留，不创建业务轴 |

因此 vNext 的 `TopologyLayout` 必须解码 8 个物理 role，`TopologySnapshot` 必须保留 8 项原始数据；后续 application 才按 schema 将 `Role[0..5]` 编译为业务角色。预留项的最低条件是 `Valid=false`；在 PLC 未明确初始化其余字段前，客户端不得强制要求 `PlcAxisIndex=-1` 或为其余字段编造语义。

#### M3-A 状态：已完成（2026-08-10）

`tools/plc_read_validate.py` 已修复为按 `element_count × word_width` 计算 FC03 寄存器数量，并由离线自测覆盖“16 个 REAL 读取 32 个 D”和“slot15 使用最后一对 D”。真实 PLC 的完整只读输出已确认：

- 所有 REAL 块均完整输出 `i=0..15`；例如绝对位置为 `D64..D95`，slot15 为 `D94..D95`。
- 标准 16 槽位的 INT/WORD/REAL 地址、0 基址与低字在前的字序，均与 PLC 当前配置值一致。
- 拓扑、龙门参数、GantryCommand 和 GantryStatus 的字段地址均可读出；`Group[1]` 或 `Role.Valid=false` 时，其余字段可为无意义原始值，客户端不得解释为业务配置。

#### M3-B 状态：未开始

M1 可继续完成 contracts/codec/transport 的纯离线 TDD；M2 topology 可按以上已冻结 ABI 实现只读 reader。只有 vNext 自己的 telemetry 实现满足以下条件，M3 才整体通过：完整 16 槽位 `RuntimeSnapshot`、`sampledAt/duration/SnapshotQuality`、Fake 测试，以及与 legacy 的并排只读对比。

## 1. Step 0 —— 脚手架与独立测试通道

**目标**：建立 `plc_vnext` 目录骨架、命名空间约定、独立 CMake target；让"从第一天起测试可执行"成为现实。
**所属里程碑**：M1（前置）

**红**：无业务代码；先验证构建通道本身。
**绿**：
```text
infrastructure/plc_vnext/CMakeLists.txt        # add_library(plc_vnext STATIC PlcVnextAnchor.cpp ...)
infrastructure/plc_vnext/PlcVnextAnchor.cpp    # 空锚点；保证库类型从第一天起稳定
infrastructure/plc_vnext/README.md             # 模块边界、PLC协议版本、接线说明
tests/infrastructure/plc_vnext/CMakeLists.txt   # 独立 plc_vnext_tests target
tests/infrastructure/plc_vnext/.gitkeep         # 空目录占位
```
**验收**：`cmake --build build --target plc_vnext_tests && ctest --test-dir build -R "^plc_vnext\."` 返回 0；`ctest -N` 列出至少一个以 `plc_vnext.` 开头的具体测试。
**禁止**：修改旧 `infrastructure` 库、旧 `unit_tests` 行为、任何 PLC 写操作。

---

## 2. Step 1 —— contracts（强类型槽位 / 组号 / 通讯结果）

**目标**：产出跨 infrastructure/application 的纯 DTO，无 Modbus、无 Qt、无 Domain 依赖；并区分“读操作未能得到快照”和“写操作未能送达 PLC”。
**所属里程碑**：M1

### 2.1 红 —— 测试

`tests/infrastructure/plc_vnext/contracts/test_plc_axis_slot.cpp`：

| 用例 | 断言 |
| --- | --- |
| `ValidSlotRanges_0To15` | `tryCreate(0)`、`tryCreate(15)` 返回有效值 |
| `OutOfRangeSlot_ReturnsEmpty` | `tryCreate(-1)`、`tryCreate(16)` 返回空值，不从 PLC 输入路径抛异常 |
| `SlotEqualityAndOrdering` | `==` 值语义正确、`<` 全序 |
| `SlotValueReturnsIndex` | `PlcAxisSlot(3).value() == 3` |

`tests/infrastructure/plc_vnext/contracts/test_plc_group_index.cpp`：同构覆盖 `0..1`，越界抛异常。

`tests/infrastructure/plc_vnext/contracts/test_communication_result.cpp`：

| 用例 | 断言 |
| --- | --- |
| `Sent_IsOkNotRetryable` | `sent()` → `ok()==true`、`retryable()==false` |
| `Timeout_IsRetryable` | `Timeout` → `retryable()==true`、`isNetworkIssue()==true` |
| `NetworkError_NotRetryable_IsNetworkIssue` | 语义与旧版一致 |
| `ProtocolError_KeepsExceptionCode` | 保留 `exceptionCode` 供诊断 |
| `IndependentFromISystemDriver` | 头文件不 include `ISystemDriver.h`（能编译即验证） |

`tests/infrastructure/plc_vnext/contracts/test_read_result.cpp`：

| 用例 | 断言 |
| --- | --- |
| `Success_ContainsValue` | 成功读取携带不可变快照值 |
| `Failure_ContainsKindAndDiagnostic` | 失败携带 `Transport/Decode/RevisionChanged` 与诊断，不伪造快照 |
| `NoValueOnFailure` | 读取失败时调用方不能误取默认值 |

### 2.2 绿 —— 实现

```text
infrastructure/plc_vnext/contracts/PlcAxisSlot.h
infrastructure/plc_vnext/contracts/PlcGroupIndex.h
infrastructure/plc_vnext/contracts/CommunicationResult.h
infrastructure/plc_vnext/contracts/ReadResult.h
```

关键草案：

```cpp
// contracts/CommunicationResult.h —— 独立于 ISystemDriver
#pragma once
#include <string>
namespace plc_vnext::contracts {
struct CommunicationResult {
    enum class Status { Sent, NetworkError, Timeout, Busy,
                        ProtocolError, InvalidResponse, Disconnected };
    Status status = Status::Sent;
    int exceptionCode = 0;
    std::string diagnostic;

    [[nodiscard]] bool ok() const { return status == Status::Sent; }
    [[nodiscard]] bool retryable() const {
        return status == Status::Timeout || status == Status::Busy; }
    [[nodiscard]] bool isNetworkIssue() const {
        return status == Status::NetworkError || status == Status::Timeout
            || status == Status::Disconnected; }
    static CommunicationResult sent() { return {}; }
    static CommunicationResult disconnected(const std::string& msg) {
        return {Status::Disconnected, 0, msg}; }
};
}

// contracts/PlcAxisSlot.h —— 构造时校验范围 0..15
#pragma once
#include <optional>
namespace plc_vnext::contracts {
class PlcAxisSlot {
public:
    [[nodiscard]] static std::optional<PlcAxisSlot> tryCreate(int v) noexcept {
        if (v < 0 || v > 15) return std::nullopt;
        return PlcAxisSlot(v);
    }
    [[nodiscard]] int value() const { return v_; }
    friend bool operator==(PlcAxisSlot a, PlcAxisSlot b) { return a.v_ == b.v_; }
    friend bool operator<(PlcAxisSlot a, PlcAxisSlot b) { return a.v_ < b.v_; }
private:
    explicit PlcAxisSlot(int v) noexcept : v_(v) {}
    int v_;
};
}
```

### 2.3 重构 / 完成条件

- 语义与旧 `CommunicationResult`（`ISystemDriver.h`）逐字段一致，但**零 include 依赖**。
- PLC 读出的整数必须经 `tryCreate` 转为槽位；转换失败属于可诊断的解码/拓扑问题，不能以 C++ 异常中止轮询线程。
- `ReadResult<T>` 使用项目自定义实现（C++20 不假定存在 `std::expected`）；它只用于读取/解码失败。PLC 已成功读出的 `ConfigValid=false` 是 `TopologySnapshot` 的内容，不是 `ReadResult` 失败。
- 本 step 不新建 `PlcCommand.h`（命令分类留到 Step 8），避免过早设计。
- **禁止**：include `domain/`、Qt、旧 `plc::protocol`。

---

## 3. Step 2 —— codec（端序与编解码）

**目标**：提供无地址、无业务名称的 `BOOL/INT/DINT/REAL` 编解码，字序/字节序严格受 `EndianPolicy` 驱动；长度/越界错误以 `DecodeError` 表达。
**所属里程碑**：M1

### 3.1 红 —— 测试

`tests/infrastructure/plc_vnext/codec/test_register_codec.cpp`（重点与 `plc_read_validate.py` 自测一致）：

| 用例 | 输入 → 期望 |
| --- | --- |
| `Real_LowWordFirst_1p0` | `{0x0000, 0x3F80}` → `1.0f`（低字在前） |
| `Real_LowWordFirst_25p8` | `{0x6666, 0x41CE}` → `25.8f` |
| `Real_RoundTrip_LowWordFirst` | encode(1.0) → decode == 1.0 |
| `Dint_LowWordFirst_RoundTrip` | `0x12345678` ⇄ `{0x5678, 0x1234}` |
| `Dint_Negative_LowWordFirst` | `{-65536}` ⇄ `{0x0000, 0xFFFF}` |
| `Int16_Signed_RoundTrip` | `-32768` ⇄ `0x8000` |
| `Bool_TrueIsNonZero` | 非 0 即 true |
| `ByteOrder_BigEndian_Int32` | 高字节在前的字内重组 |
| `DecodeError_TooFewRegisters` | 越界/长度不足 → `DecodeError`（非异常外抛） |

`tests/infrastructure/plc_vnext/codec/test_endian_policy.cpp`：`ByteOrder`/`WordOrder` 组合枚举值语义。

### 3.2 绿 —— 实现

```text
infrastructure/plc_vnext/codec/EndianPolicy.h
infrastructure/plc_vnext/codec/RegisterCodec.h / .cpp
infrastructure/plc_vnext/codec/RawRegisterBlock.h
infrastructure/plc_vnext/codec/DecodeError.h
```

`EndianPolicy` 直接对齐旧 `plc::protocol::EndianPolicy`（`ByteOrder::BigEndian` 指字内高字节在前、`WordOrder::LowWordFirst` 指多寄存器低字在前）。`RegisterCodec` 可迁移旧静态算法，但**错误返回统一用 `DecodeError`**（携带原因枚举与越界索引），不再向上层抛未分类异常。

### 3.3 重构 / 完成条件

- 任一 `encode`/`decode` 往返测试全绿。
- `RawRegisterBlock` 只保存原始 `uint16_t`/bit 缓冲与访问边界，无业务语义。
- 与 `plc_read_validate.py` 的 `--selftest` 字序断言完全一致（1.0f / 25.8f / DINT 0x12345678）。
- **禁止**：出现任何 Modbus 地址常量、轴名、`domain` 类型。

---

## 4. Step 3 —— layout（16 槽位 + 拓扑 + 龙门地址公式）

**目标**：用强类型地址与纯函数布局，把"0 基址 + 每个数组基址/步长"落地为可测试的地址公式。这是**唯一允许出现 Modbus 地址公式**的目录。
**所属里程碑**：M1

### 4.1 红 —— 测试

`tests/infrastructure/plc_vnext/layout/test_axis_slot_register_layout.cpp`：

| 用例 | 断言 |
| --- | --- |
| `ManualSpeed_Base0_Stride2` | 槽位 i → `D(0+2i)`，i=0..15 |
| `PositioningSpeed_Base32_Stride2` | i → `D(32+2i)` |
| `AbsPosition_Base64_Stride2` | i → `D(64+2i)` |
| `MotionState_Base128_Stride1` | i → `D(128+i)` |
| `Slot0AndSlot15_Boundary` | 槽位 0 与槽位 15 首地址不重叠 |
| `NoOverlap_AllFields` | 相邻数组字段无地址重叠 |
| `RealFieldsSpanTwoWords` | `manualSpeed(i)` 覆盖 2 个 D |
| `AbsPosTargetSlot13` | `D(1122..1123)`（§9.3 SYN0 公共地址交叉验证） |

`tests/infrastructure/plc_vnext/layout/test_axis_topology_layout.cpp`：

| 用例 | 断言 |
| --- | --- |
| `GroupBase_1408_Stride83` | `groupBase(0)=1408`、`groupBase(1)=1491` |
| `RoleBase_Formula` | `roleBase(g,r)=groupBase(g)+3+10r`；`Role[5]` A 组 = 1461 |
| `HeadFields_Offsets` | Magic=1400、SchemaVersion=1402、ConfigValid=1576、ConfigErrorCode=1577 |
| `ConfigValid_IsBoolBit0` | 位取址正确 |

`tests/infrastructure/plc_vnext/layout/test_gantry_layout.cpp`：

| 用例 | 断言 |
| --- | --- |
| `Command_Base180_Stride4` | `command(0).cmd=180`、`RequestSeq=181..182` |
| `Status_Base190_Stride18` | `status(0)` A 组 = 190..207、B 组 = 208..225 |
| `Param_Base1600_Stride22` | `param(0)=1600..1621`、`param(1)=1622..1643` |

### 4.2 绿 —— 实现

```text
infrastructure/plc_vnext/layout/RegisterAddress.h        # 强类型 coil/holding 地址
infrastructure/plc_vnext/layout/AxisSlotRegisterLayout.h # 槽位 0..15 全部公式
infrastructure/plc_vnext/layout/AxisTopologyLayout.h      # 拓扑 Header/Group/Role
infrastructure/plc_vnext/layout/GantryLayout.h            # 龙门 Command/Status/Param
```

草案（地址公式统一 0 基址，直接取已验证常量）：

```cpp
// layout/AxisSlotRegisterLayout.h
namespace plc_vnext::layout {
// 地址表 3.1：REAL 每项 2 D，INT/WORD 每项 1 D
constexpr int manualSpeed(int slot)      { return 0    + 2 * slot; }
constexpr int positioningSpeed(int slot) { return 32   + 2 * slot; }
constexpr int absPosition(int slot)      { return 64   + 2 * slot; }
constexpr int relPosition(int slot)      { return 96   + 2 * slot; }
constexpr int motionState(int slot)      { return 128  + 1 * slot; }
constexpr int motionLimit(int slot)      { return 144  + 1 * slot; }
constexpr int alarmWord(int slot)        { return 160  + 1 * slot; }
constexpr int absPosTarget(int slot)     { return 1096 + 2 * slot; }
constexpr int relPosTarget(int slot)     { return 1128 + 2 * slot; }
}
```

> 每个函数都要有"槽位越界由 `PlcAxisSlot` 保证，公式本身为 `constexpr`"的约束；`plc_read_validate.py` 中 `_addr_of` 的计算即本 step 的参考实现。

### 4.3 重构 / 完成条件

- 全部公式与地址表、`plc_read_validate.py` 输出逐一 diff 一致。
- 用第 0.5 节对拍方法落一份 `layout_truth.txt` 作为回归锚点。
- **禁止**：出现 `x_axis/y_axis/z_axis/r_axis` 业务轴名；出现 Modbus 库/网络代码。

---

## 5. Step 4 —— ReadPlanBuilder（批量读计划）

**目标**：把"需要轮询的全部地址"合并为最少且合法的批量读取请求（FC03 ≤125 / FC01 ≤2000），纯函数、无 I/O。
**所属里程碑**：M1（为 M3 服务，可在 Step 5/6 前完成）

### 4.1 红 —— 测试

`tests/infrastructure/plc_vnext/layout/test_read_plan_builder.cpp`：

| 用例 | 断言 |
| --- | --- |
| `MergesAdjacentHoldingRegions` | 相邻 D 区间合并为单请求 |
| `KeepsGapSeparate` | 不连续区间各自成请求 |
| `SplitsOver125Registers` | 超过 125 寄存器自动分片 |
| `DeduplicatesAndSorts` | 重复地址去重、结果有序 |
| `CoilsMergeIndependent` | 线圈区独立合并、FC01 分片 |

### 4.2 绿 —— 实现

```text
infrastructure/plc_vnext/layout/ReadPlan.h
infrastructure/plc_vnext/layout/ReadPlanBuilder.h / .cpp
```

`ReadPlan` 为 `std::vector<ReadRange>`（含 area/start/count）。算法复用旧 `PlcPoller` 的合并思路，但改为纯函数输入输出、不依赖 `RegisterRegistry`。

### 4.3 重构 / 完成条件

- 给定 16 槽位 + 龙门状态全量地址，生成的请求数最少且每片 ≤ 上限。
- 可与 `plc_read_validate.py` 实际请求数（D 区分片、M 区一次读完）交叉核对。

---

## 6. Step 5 —— transport 与 FakeModbus（I/O 抽象）

**目标**：抽出窄 `IModbusClient` 接口与请求/响应值对象、单通道串行化 I/O、连接监控；并提供可脚本化的 `FakeModbusClient`。**transport 不认识槽位/轴/业务**。这是 Step 6/7/10 的依赖。
**所属里程碑**：M1

### 5.1 红 —— 测试

`tests/infrastructure/plc_vnext/transport/test_imodbus_client_contract.cpp`（对 `FakeModbusClient` 走通接口契约）：

| 用例 | 断言 |
| --- | --- |
| `ReadCoils_ReturnsScriptedBits` | FC01 读回脚本化的位数据 |
| `ReadHolding_ReturnsScriptedWords` | FC03 读回脚本化的字数据 |
| `WriteSingleCoil_RecordsAddressAndValue` | 记录写入地址/值 |
| `WriteMultiple_RecordsSequence` | FC10 写入序列正确 |
| `ScriptedTransportFailure_ReturnsResult` | 注入失败 → `CommunicationResult` 失败态 |
| `ZeroBasedAddress_Passthrough` | 0 基址地址原样透传（不加 1/40001） |

`tests/infrastructure/plc_vnext/transport/test_modbus_io_executor.cpp`：

| 用例 | 断言 |
| --- | --- |
| `SerializesRequests_NoCrossTalk` | 并发提交 → 单通道串行、事务不交叉 |
| `Failure_PreservesResponseOrder` | 失败请求不吞掉后续响应 |

`tests/infrastructure/plc_vnext/transport/test_connection_monitor.cpp`：

| 用例 | 断言 |
| --- | --- |
| `Disconnect_ReportsState` | 断线 → 连接状态更新 |
| `ManualReconnect_Delegates` | `requestReconnect()` 委托底层 |
| `NeverReplaysMotionCommand` | 监控层不重放任何命令 |

### 5.2 绿 —— 实现

```text
infrastructure/plc_vnext/transport/IModbusClient.h        # 窄接口，返回 contracts::CommunicationResult
infrastructure/plc_vnext/transport/ModbusRequest.h / ModbusResponse.h
infrastructure/plc_vnext/transport/ModbusIoExecutor.h / .cpp
infrastructure/plc_vnext/transport/ConnectionMonitor.h / .cpp
infrastructure/plc_vnext/transport/AsioModbusTcpClient.h / .cpp   # 迁移/复现旧实现，加测试
infrastructure/plc_vnext/fake/FakeModbusClient.h / .cpp
```

要点：
- 新 `IModbusClient` 形状对齐旧 `plc::protocol::IModbusClient`，但返回值改用 `contracts::CommunicationResult`，**不再依赖 `ISystemDriver`**。
- `FakeModbusClient` 可脚本化原始寄存器与通讯故障，供 topology/telemetry/gateway 全链路 TDD。

### 5.3 重构 / 完成条件

- 旧 `AsioModbusTcpClient` 迁移前后行为不变（用旧测试作回归锚点），或在新 transport 下补齐等价测试。
- **禁止**：transport 出现轴/槽位/拓扑/联动语义；在 transport 层重放运动命令。

---

## 7. Step 6 —— topology（启动拓扑发现，只读）

**目标**：按 §4.1 双读流程读、解码、校验 `AxisTopology` 并产出 `TopologySnapshot`；无写路径。
**所属里程碑**：M2

### 6.1 红 —— 测试

`tests/infrastructure/plc_vnext/topology/test_topology_decoder.cpp`：

| 用例 | 断言 |
| --- | --- |
| `DecodesHeader_AndGroups` | Magic/版本/Revision/ConfigValid/ConfigErrorCode 正确 |
| `DecodesRole_Bindings` | A 组 Role[0] X1、Role[5] SYN0 绑定正确 |
| `DecodesGroup_ValidFlag` | `Group[1].Valid` 解析正确 |
| `RawBlockTooShort_ReturnsError` | 长度不足 → `TopologyReadError` |

`tests/infrastructure/plc_vnext/topology/test_topology_validator.cpp`（只验证客户端的解码安全不变量）：

| 用例 | 期望错误码 |
| --- | --- |
| `ValidTopology_Passes` | 无错误 |
| `MagicMismatch` | `TopologyIssue::MagicMismatch` |
| `SchemaVersionUnsupported` | `TopologyIssue::UnsupportedSchema` |
| `InvalidSlotInValidRole` | `TopologyIssue::SlotOutOfRange` |
| `DuplicateSlot_AcrossValidRoles` | `TopologyIssue::DuplicateSlot` |
| `InvalidGroupCode` | `TopologyIssue::GroupCodeMismatch` |
| `ReservedFieldNonZero` | `TopologyIssue::ReservedFieldUnexpected`（仅在当前 schema 要求为 0 时） |
| `SecondGroupValid_IsNotRejected` | 第二组有效本身不得构成客户端错误 |

`tests/infrastructure/plc_vnext/topology/test_topology_reader.cpp`（用 FakeModbus）：

| 用例 | 断言 |
| --- | --- |
| `StableRevision_TwoReads` | Header→Body→Header，Revision 一致 → 成功 |
| `RevisionChanged_ReturnsChanged` | 两次 Header Revision 不同 → `TopologyReadError::Changed` |
| `ConfigValidFalse_ReturnsCoherentInvalidSnapshot` | 读取成功，快照保留 PLC 的 `ConfigValid=false` 与 `ConfigErrorCode` |

### 6.2 绿 —— 实现

```text
infrastructure/plc_vnext/contracts/TopologySnapshot.h
infrastructure/plc_vnext/topology/PlcTopologyReader.h / .cpp
infrastructure/plc_vnext/topology/TopologyDecoder.h / .cpp
infrastructure/plc_vnext/topology/TopologyValidator.h / .cpp
infrastructure/plc_vnext/topology/TopologyReadError.h
```

`TopologySnapshot` 只表达原始配置与校验结果，**不创建 Domain 轴**。`TopologyValidator` 输出 `std::vector<TopologyIssue{kind, message, context}>`，只用于发现客户端的解码错误和内存安全风险。

PLC 的 `ConfigValid` 与 `ConfigErrorCode` 必须原样保留在成功读取的快照中；客户端不得复刻 PLC 的完整业务校验，也不得为本地 `TopologyIssue` 编造或覆盖 PLC 错误码。上层以 PLC 的有效性决定是否开放普通控制。

### 6.3 重构 / 完成条件

- 使用 `--only topology` 与真实 PLC 对拍：Magic、SchemaVersion、Revision、所有配置角色、`ConfigValid` 和 `ConfigErrorCode` 全一致。角色数量必须来自当前 schema/layout 常量，测试不得把 6 或 8 写死为跨版本事实。
- **禁止**：任何写操作；UI 按新拓扑创建轴（留待 M6）。

---

## 8. Step 7 —— telemetry（运行快照，只读）

**目标**：执行 `ReadPlan` → 生成 16 槽位 + 龙门状态的 `RuntimeSnapshot`，明确每条数据的可信度；不注入领域对象。
**所属里程碑**：M3

### 7.1 红 —— 测试

`tests/infrastructure/plc_vnext/telemetry/test_axis_snapshot_decoder.cpp`：

| 用例 | 断言 |
| --- | --- |
| `DecodesSingleSlotFeedback` | 手动/定位速度、绝对/相对位置、状态、限制、告警正确 |
| `RealLowWordFirst_Decode` | 槽位 REAL 字段按低字在前解析 |
| `MissingBlock_MarksUntrusted` | 缺数据 → `trusted=false`，不填 0 冒充正常 |
| `InvalidEnum_KeepsRawValue` | 未知运动状态保留原值 |

`tests/infrastructure/plc_vnext/telemetry/test_snapshot_reader.cpp`：

| 用例 | 断言 |
| --- | --- |
| `ExecutesPlan_ProduceRuntimeSnapshot` | 一次轮询产出 16 槽位集合 |
| `PartialReadFailure_QualityPartial` | 部分请求失败 → `SnapshotQuality::Partial` |
| `TransportFailure_QualityFailed` | 全部失败 → `transport-failed`，不带可信数据 |
| `SnapshotHasSampledAt_AndDuration` | 含采样时间与读取耗时 |

`tests/infrastructure/plc_vnext/telemetry/test_gantry_status_reader.cpp`：

| 用例 | 断言 |
| --- | --- |
| `DecodesState_AndAckSeq` | `State`、`InternalStep`、`AckSeq`、`CommandResult` 正确 |
| `DecodesControlAllowBits` | `ReadyToCouple`/`MemberControlAllowed`/`LogicalControlAllowed` 等位取址正确 |
| `DecodesSkew_AndPositions` | `X1Position`/`X2Position`/`Skew` REAL 正确 |
| `DecodesFault` | `Fault`、`FaultCode` 正确 |

### 7.2 绿 —— 实现

```text
infrastructure/plc_vnext/contracts/AxisRuntimeSnapshot.h
infrastructure/plc_vnext/contracts/RuntimeSnapshot.h
infrastructure/plc_vnext/contracts/GantryStatusSnapshot.h
infrastructure/plc_vnext/telemetry/PlcSnapshotReader.h / .cpp
infrastructure/plc_vnext/telemetry/AxisSnapshotDecoder.h / .cpp
infrastructure/plc_vnext/telemetry/GantryStatusReader.h / .cpp
infrastructure/plc_vnext/telemetry/SnapshotQuality.h
```

`SnapshotQuality` 枚举：`Trusted / Stale / Partial / TransportFailed`。一次连续读失败**不得**把字段置 0 或"正常"，必须标 `trusted=false`。

### 7.3 重构 / 完成条件（影子运行）

#### M3-A：PLC 协议现场验证（已完成）

- [x] `plc_read_validate.py` 的 REAL 块按 16 项 × 2 D 读取，所有 REAL 区均输出 `i=0..15`。
- [x] 标准 16 槽位的 REAL/INT/WORD 地址、0 基址和低字在前字序与现场 PLC 值一致。
- [x] topology、龙门参数、命令和状态字段地址可完整读取。

#### M3-B：vNext telemetry 影子运行（未开始，M3 总门禁）

- [ ] `PlcSnapshotReader`、`AxisSnapshotDecoder`、`GantryStatusReader` 的 TDD 测试通过。
- [ ] 每次读取形成 16 槽位 `RuntimeSnapshot`，包含 `sampledAt`、duration 和 `SnapshotQuality`。
- [ ] 在诊断接线中**并排**运行新 reader（只读）与旧 `ModbusSystemDriver`，输出字段级对比日志。
- [ ] 空闲、运动、限位、报警、断线/重连场景均无未解释差异。
- [ ] **禁止**：用新快照控制运动；用新快照驱动 UI。

---

## 9. Step 8 —— command（单轴写入）

**目标**：把"槽位参数/目标/停止/使能/点动"编码为 Modbus 写入；只报告写入是否到达 PLC，不决定是否运动。
**所属里程碑**：M4

### 8.1 红 —— 测试

`tests/infrastructure/plc_vnext/command/test_plc_axis_command_writer.cpp`：

| 用例 | 断言 |
| --- | --- |
| `WriteManualSpeed_EncodesRealLowWordFirst` | 写 `D(0+2s)` REAL，低字在前 |
| `WriteAbsTarget_SeparateFromTrigger` | 目标与触发是两次独立调用 |
| `WriteCoil_EnableAxis` | `M(0+i)` 保持电平写 ON/OFF |
| `WriteCommand_ReportsCommResult` | 返回 `CommunicationResult`，不伪造执行成功 |

`tests/infrastructure/plc_vnext/command/test_command_write_policy.cpp`：

| 用例 | 断言 |
| --- | --- |
| `LevelHold_Classified` | 保持电平命令可重复写（使能/点动/心跳） |
| `SelfReset_Classified` | PLC 自复位命令只写 ON（触发/终止/清除） |

### 8.2 绿 —— 实现

```text
infrastructure/plc_vnext/contracts/PlcCommand.h
infrastructure/plc_vnext/command/PlcAxisCommandWriter.h / .cpp
infrastructure/plc_vnext/command/CommandWritePolicy.h
```

### 8.3 重构 / 完成条件（受控切换）

- 先只对**一个安全命令类别**（例如"手动速度"参数写）从旧路径受控切到新 writer。
- 完成条件：写入顺序、自复位只写 ON、断线后不重放测试全绿；`--selftest` 对拍。
- 触发/终止线圈（M48/M64/M144/M160）由 PLC 当前版本自动复位：客户端只写 ON、无需配对 OFF，不实现客户端 ON→OFF 边沿脉冲。
- **职责边界**：`PlcAxisCommandWriter` 只负责提交——`CommunicationResult::ok()` 仅证明写请求到达 PLC，**不证明 PLC 已执行并自动复位**。触发/终止/清除线圈的"读回确认"由 Step 10/11 的 telemetry/ack reader 异步完成，不属于 writer 职责；writer 层测试只验证"提交一次/断线无重放/无读回"，不实现读回确认。
- **禁止**：任何运动触发双写；跨类别一次性全量切换。

---

## 10. Step 9 —— command（龙门请求写入）

**目标**：实现 `Command 后 RequestSeq` 的有序写入；只建立请求/确认通路，不直接控齿轮/许可。
**所属里程碑**：M5

### 9.1 红 —— 测试

`tests/infrastructure/plc_vnext/command/test_plc_gantry_command_writer.cpp`：

| 用例 | 断言 |
| --- | --- |
| `WriteCouple_CommandThenSeq` | 先写 `D180 Command=1`，最后在独立事务写 `D181..182 RequestSeq=N+1` |
| `WriteOrder_CommandBeforeSeq` | FakeModbus 的调用记录证明 `Command` 响应成功后才写 `RequestSeq` |
| `ProvidedSeq_IsWrittenExactly` | writer 原样写入 application 提供的 `N+1`，不自行生成或递增序号 |
| `GroupIndex_AddressesGroup` | `command(1)` 落在 B 组槽位 |
| `AckSeqAlignment_NotDecidedHere` | 确认/超时判定交给 application |

`tests/infrastructure/plc_vnext/command/test_gantry_request.cpp`：`Couple/Decouple/Reset` 与序号意图值语义。

### 9.2 绿 —— 实现

```text
infrastructure/plc_vnext/contracts/GantryRequest.h        # Couple/Decouple/Reset + application 提供的 requestSeq
infrastructure/plc_vnext/command/PlcGantryCommandWriter.h / .cpp
```

### 9.3 重构 / 完成条件

- 按 PLC 契约使用两次**有序事务**：先写 `Command`，待正常 Modbus 响应后再写 `RequestSeq`。不能用一次 FC10 把二者合并，因为它无法表达“序号最后提交”的提交屏障。
- writer 不维护跨请求的序号状态；序号属于 application/session 的请求身份。基础设施仅保证传入的序号与写入顺序正确。
- `None=0` 是 PLC 寄存器的“无命令状态”，不是有效事务：writer 本地拒绝（`isCommandCodeValid` 仅接受 1..3），零写入，不得提交带新序号的 `None`。
- **提交不确定性**：`submit()` 保留返回 `CommunicationResult`（兼容旧契约）；新增 `submitDetailed()` 返回 `GantrySubmitResult{GantrySubmitState}`。其中 RequestSeq 写失败 → `CommitUncertain`（PLC 可能已收 Command 也可能没收到），由上层/ack reader 按 AckSeq 判定最终结果；断线期间绝不自动重发。
- `plc_read_validate.py` 当前为只读工具，没有 `--command` 参数；真实 PLC 的命令验收必须使用单独的、维护模式下的受控写入工具和操作记录，不能伪称由该脚本对拍。
- **禁止**：写 `GearIn/GearOut`、写控制许可、判定联动成功。

---

## 11. Step 10 —— PlcRuntimeGateway（组合门面）

**目标**：组合 readers/writers/connection，对外暴露唯一的 `IPlcRuntimeGateway` 高层接口；不含业务决策、不依赖 `SystemContext`。
**所属里程碑**：M5/M6 前置

### 10.1 红 —— 测试

`tests/infrastructure/plc_vnext/integration/test_plc_runtime_gateway.cpp`（用 FakeModbus）：

| 用例 | 断言 |
| --- | --- |
| `ReadTopology_ReturnsSnapshot` | 走通 reader+decoder+validator |
| `ReadRuntime_ReturnsSnapshotWithQuality` | 走通 plan+io+decode |
| `WriteAxis_ReportsCommResult` | 单轴写入走通 |
| `SubmitGantryRequest_OrderedWrite` | 龙门请求走通 |
| `ConnectionState_Exposed` | `connectionState()` 返回连接状态 |
| `RequestReconnect_Delegated` | `requestReconnect()` 委托 transport |
| `DoesNotTouchSystemContext` | 接口签名不含 `SystemContext`/`Axis`/ViewModel |
| `SubmitGantryRequest_Group1_DefaultRejected` | 缺省 gate 只放行 Group 0（B 组默认拒绝、零写入） |
| `ConcurrentIo_DoesNotInterleaveGantryCommit` | 单轴写/telemetry 读并发时，`Command` 后必须紧跟 `RequestSeq`（共享通道成组原子） |

### 10.2 绿 —— 实现

```text
infrastructure/plc_vnext/IPlcRuntimeGateway.h
infrastructure/plc_vnext/PlcRuntimeGateway.h / .cpp
```

接口草案（与设计文档 §5 一致）：

```cpp
// IPlcRuntimeGateway.h
namespace plc_vnext {
class IPlcRuntimeGateway {
public:
    virtual ~IPlcRuntimeGateway() = default;
    virtual contracts::ReadResult<contracts::TopologySnapshot> readTopology() = 0;
    virtual contracts::ReadResult<contracts::RuntimeSnapshot> readRuntime() = 0;
    virtual contracts::CommunicationResult writeAxis(
        contracts::PlcAxisSlot slot, const contracts::PlcAxisCommand& cmd) = 0;
    virtual contracts::CommunicationResult submitGantryRequest(
        contracts::PlcGroupIndex g, const contracts::GantryRequest& req) = 0;
    virtual contracts::GantrySubmitResult submitGantryRequestDetailed(
        contracts::PlcGroupIndex g, const contracts::GantryRequest& req) = 0;
    virtual contracts::ConnectionState connectionState() const = 0;
    virtual void requestReconnect() = 0;
};
}
```

### 10.3 重构 / 完成条件

- FakeModbus 下全链路端到端通过。
- **共享串行化通道**：所有 reader/writer 统一注入同一个 `transport::ModbusIoExecutor`（实现 `IModbusClient`）；龙门提交用 `executeGroup` 把 `Command→RequestSeq` 包成全局临界区，单轴写 / telemetry 读 / 其它龙门提交不得插入其间（跨组件成组原子）。
- **B 组默认拒绝**：Gateway 缺省 `groupGate` 只放行 Group 0，除非调用方显式注入其它策略——“当前 B 组禁用”不得只依赖调用方记得传 gate。
- **禁止**：在 Gateway 内出现联动业务编排、重试策略、超时判定、ViewModel。

---

## 12. Step 11 —— fake 收尾与真实 PLC 验收准备

**目标**：补齐应用层 TDD 用的高层假实现与测试夹具，并完成真实 PLC 的只读验收。写入验收仍属于 Step 8/9 的受控上线活动，不能混入本步骤。
**所属里程碑**：M2/M3 收尾 + M4/M5 前置

### 11.1 绿 —— 实现

```text
infrastructure/plc_vnext/fake/FakePlcRuntimeGateway.h / .cpp   # 应用层 TDD 用的高层假实现
infrastructure/plc_vnext/fake/PlcFixtureBuilder.h              # 构造有效/无效拓扑与槽位反馈夹具
```

### 11.2 验收步骤（对真实 PLC，只读先行）

1. `readTopology()`：与 `python tools/plc_read_validate.py --only topology` 逐字段 diff（Magic/版本/Revision/Role 绑定/ConfigValid）。
2. `readRuntime()`：与 `--only axis`、`--only status` 对拍，确认 16 槽位 + 龙门状态 + 可信度。
3. 将真实 PLC 读数固化为脱敏 fixture，并带上 PLC 程序版本、SchemaVersion 和 Revision；后续单元测试不依赖在线 PLC。
4. Step 8/9 的写入验收另建维护窗口、目标地址、回读方式和回滚记录；不得为了验证字序向运行设备随意写入 `1.0f`。

### 11.3 完成条件 / 禁止

- 真实只读读数与验证脚本一致；真实写入验收在对应命令步骤、经过维护授权后单独执行。
- **禁止**：在只读验收阶段引入物理运动；直接写 GearIn/GearOut/许可。

---

## 13. Step 12 —— 影子迁移与交接准备（M6）

**目标**：让上层具备逐步消费 Gateway 输出的影子接线和交接条件，不破坏既有功能；legacy 的删除属于后续上层迁移完成后的独立任务。
**所属里程碑**：M6

### 12.1 影子运行

- 在主循环**并排**运行新 `PlcRuntimeGateway` 与旧 `ModbusSystemDriver`（仅读），输出对比日志；确认无差异后进入受控切换。

### 12.2 受控切换（单轴写入）

- 按命令类别、按单轴类型逐个切换；每切换一个类别，旧路径对应停止写入。
- 任何会引起物理运动的命令**不允许双写**；切换期间保持心跳/急停路径完整。

### 12.3 清理的前提与范围

- 本基础设施任务只完成 vNext 的影子接线和交接条件，**不删除** `ModbusSystemDriver`、`ISystemDriver` 或旧 `plc/`；这些类型仍被现有 Domain/Application/Presentation 使用。
- 只有上层迁移完成、所有正式命令均已改走 `IPlcRuntimeGateway`、真实 PLC 回归验收通过且回滚窗口结束后，才开独立清理 PR 删除 legacy。
- 清理 PR 必须保留或迁移仍有价值的协议/特征测试；不能以“旧文件”名义批量删除测试。

### 12.4 完成条件

- vNext 可在影子模式稳定读取，且不影响 legacy 控制；交接给 application/domain 的接口、Fake 和证据齐全。
- 删除 legacy 是后续独立迁移目标，不是本步骤出口条件。
- **禁止**：在本基础设施任务中顺带重写 UI/ViewModel 或改 Domain 轴模型，也禁止提前删除旧通讯路径。

---

## 14. 验收门禁与完成清单

每个 PR 必须满足：

- [ ] 独立 target `plc_vnext_tests` 可编译、可被 `ctest` 发现并全绿。
- [ ] 不改动旧 `infrastructure/plc` 行为（M1~M3）；写路径切换仅在对应 Step 受控进行。
- [ ] 新代码无 `domain/`、Qt、旧 `plc::protocol` 依赖（contracts/layout/codec 子目录强制零业务名）。
- [ ] 只读 Step 与 `plc_read_validate.py` 对拍一致。
- [ ] 无任何"断线后重放运动命令"路径；无双写。
- [ ] 未确认字段标记为"待定"，不以旧 `RegisterAddressAll.h` 猜测填充。

### 里程碑完成检查

| 里程碑 | 入口判据 | 出口判据 |
| --- | --- | --- |
| M1 | Step 0..4 测试红绿 | contracts/codec/layout/ReadPlan 全绿，旧库零改动 |
| M2 | Step 5/6 测试红绿 | topology 可读/解码/校验，真实对拍一致 |
| M3-A | PLC 脚本与现场对拍 | 已完成：完整 16 槽位地址/数量/字序验证 |
| M3-B（M3 总出口） | Step 7 测试红绿 | telemetry 全绿，影子对比无差异 |
| M4 | Step 8 测试红绿 | 一个安全命令类别受控切换通过 |
| M5 | Step 9/10 测试红绿 | 龙门请求/确认通路走通 |
| M6（本任务出口） | Step 11/12 | 上层可消费 Gateway 输出；影子读取稳定；legacy 保留 |
