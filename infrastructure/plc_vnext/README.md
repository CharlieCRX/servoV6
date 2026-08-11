# plc_vnext —— PLC 通讯基础设施层（重构新实现）

> 状态：Step 0 脚手架、Step 1 (contracts)、Step 2 (codec)、Step 3 (layout)、Step 4 (ReadPlanBuilder)、Step 5 (transport/Fake)、Step 6 (topology)、Step 7 (telemetry) 与 Step 8 (command，单轴写路径离线 TDD) 已完成。后续自 Step 9 (command 龙门请求) 起逐步加入。
> 注：Step 8 仅开放 Fake 下的写入设计（测试注入 `FakeModbusClient`）；**真机写入保持关闭**——未接入 `AsioModbusTcpClient` 的写路径，不对运行设备产生任何写操作。真实 PLC 写验收属于受控上线活动（Step 8/9/11 完成条件）。触发/终止线圈由 PLC 当前版本自动复位：客户端只写 ON、无需配对 OFF，已移除客户端 ON→OFF 边沿脉冲机制。`PlcAxisCommandWriter` 只负责提交（`CommunicationResult::ok()` 仅证明写到达）；"读回确认"由 Step 10/11 的 telemetry/ack reader 异步完成，writer 不做读回。
> 依据：《docs/refactor/PLC通讯基础设施层重构——TDD实施文档.md》与《PLC通讯基础设施层重构设计.md》。

## 模块边界

- 所有新代码统一使用命名空间 `plc_vnext`（子模块 `plc_vnext::contracts`、`plc_vnext::codec`、`plc_vnext::layout` …）。
- **禁止**：include 旧 `plc::protocol`、`domain/`、Qt。
- 新 `CommunicationResult` 在 `contracts/CommunicationResult.h` 中**独立定义**，不 `#include "infrastructure/ISystemDriver.h"`，是 transport 与 `ISystemDriver` 解耦的唯一路径。
- 旧 `infrastructure/plc` 在迁移期间保留；不得在同一目录混用新旧两种模型。

## 目录职责速览

| 目录 | 职责 |
| --- | --- |
| `contracts/` | 跨 infrastructure/application 的纯 DTO（强类型槽位/组号、结果类型）；无 Modbus/Qt/Domain |
| `codec/` | 基础二进制/寄存器转换（端序、编解码），完全无地址和业务名称 |
| `layout/` | PLC 协议布局；唯一允许出现 Modbus 地址公式的目录 |
| `topology/` | 启动配置读取/解码/校验；只读 |
| `telemetry/` | 运行时只读反馈；不注入领域对象 |
| `command/` | 协议写入；不决定“是否应该运动”（自 M4 起按命令类别受控引入） |
| `transport/` | TCP/Modbus 帧与连接生命周期；不认识槽位和轴 |
| `fake/` | vNext 测试替身，模拟 PLC 契约而不是旧 Domain |

## PLC 协议版本（当前基线）

- 拓扑 ABI（2026-08-10 现场确认）：`ST_GroupAxisMap.Role` 容量为 **8**（`ST_RoleAxisBinding[8]`），组头 3 D、每个 role 10 D、每组步长 83 D。
- 业务角色仅使用前 6 项：`X1(0) / X2(1) / Y(2) / Z(3) / R(4) / X逻辑轴(5)`；下标 6 为预留，不创建业务轴。
- 组号范围：当前 `0..1`。
- 槽位范围：`0..15`。
- 端序约定：REAL/DINT 字序受 `EndianPolicy` 严格驱动（见 Step 2）。

> 说明：`PLC变量协议_Modbus最终地址表.md` 当前不在本仓库；在补入并评审前，`tools/plc_read_validate.py` 是可执行参考，不是可追溯的最终规范。

## 构建与测试（独立通道）

根 `CMakeLists.txt` 在 `add_subdirectory(tests)` 之前引入：

```cmake
add_subdirectory(infrastructure/plc_vnext)   # 定义独立 STATIC 库 plc_vnext
```

`tests/CMakeLists.txt` 只追加独立子目录（不复用旧 `unit_tests`）：

```cmake
add_subdirectory(infrastructure/plc_vnext)
```

测试 target 为 `plc_vnext_tests`，通过 `gtest_discover_tests(... TEST_PREFIX "plc_vnext.")` 注册，
因此验收命令用正则过滤具体测试名：

```bash
cmake --build build --target plc_vnext_tests
ctest --test-dir build -R "^plc_vnext\."
ctest --test-dir build -N
```

> 注意：`ctest -R plc_vnext_tests` 匹配的是可执行文件名而非被发现的测试名，
> 不能证明测试真的被发现；请始终用 `^plc_vnext\.` 正则验证。

### 测试文件登记约定

不使用 `file(GLOB_RECURSE ...)` 自动收集。每个 Step 在
`tests/infrastructure/plc_vnext/CMakeLists.txt` 中**显式追加**新增测试文件，
让代码评审准确看到该 PR 增加了哪些行为。

## 接线说明

- 本模块仅通过窄接口（如 `transport/IModbusClient.h`）访问设备 I/O；
  业务层（application）只依赖 `plc_vnext` 暴露的高层只读/写通道，不接触 Modbus 细节。
- M1~M3 一律**只读先行，不写 PLC**；写路径自 M4 起按命令类别受控切换。
- **禁止双写**：任何会引起物理运动的命令，新旧两套实现不得同时写。
