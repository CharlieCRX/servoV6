------

# servoV6

> **现代 C++ 驱动的通用伺服控制框架**  
> 支持多种伺服电机、通信协议与控制模式的模块化运动控制系统，适用于测试平台、设备开发和自动化控制场景。

---

## 🧱 项目结构

```
servoV6/
 ├── app/                 # 程序入口与 QML 界面
 ├── application/         # 业务逻辑调度层
 ├── domain/              # 核心接口与命令模型（无具体实现）
 ├── adapters/            # 各类电机、IO模块、协议的具体适配器
 │   ├── motors/            # 电机类型（如 P100S）
 │   ├── protocol/          # 通信协议实现（如串口）
 │   └── servos/            # 高阶伺服控制逻辑（含减速比换算等）
 ├── utils/               # 日志等通用工具
 ├── tests/               # 模块测试
 ├── CMakeLists.txt       # CMake 构建配置
 └── README.md            # 本说明文档
```

---

## ✨ 功能亮点

- ✅ **抽象解耦架构**：采用 `domain → application → adapters` 分层设计，逻辑与实现完全解耦
- ✅ **类型安全命令模型**：使用 `std::variant` 与访问者模式组合实现命令分发与执行
- ✅ **支持多种电机/协议**：通过接口适配器机制支持不同电机型号与通信协议
- ✅ **可测试性强**：各模块支持单元测试与 mock 注入
- ✅ **界面友好**：集成 QML 实现控制界面（可选）

---

## ⚙️ 架构概览

### 核心模块说明

| 模块           | 说明                                                         |
| -------------- | ------------------------------------------------------------ |
| `domain/`      | 定义接口与命令模型（`IMotor`, `IServoAdapter`, `MovementCommand` 等）<br>是系统中所有交互的“契约层”，不包含任何实现 |
| `application/` | 包含 `BusinessLogic` 和 `MotorCommandExecutor`，是命令调度与控制的“大脑”，基于 `CommandVisitor` 模式运行 |
| `adapters/`    | 实现硬件适配，包括：<br>• `motors/`: 如 `P100S`<br>• `protocol/`: 如串口协议<br>• `servos/`: 高阶伺服策略（如带减速比换算） |
| `tests/`       | 针对各个模块的单元测试                                       |
| `utils/`       | 提供日志功能等通用模块                                       |

---

## 🚀 快速开始

### 构建要求

- CMake 3.16+
- C++17 编译器（如 GCC 9+ / Clang 10+ / MSVC 2019+）
- Qt 6（用于 GUI，可选）

### 构建项目

```bash
git clone https://github.com/your-org/servoV6.git
cd servoV6
mkdir build && cd build
cmake ..
cmake --build .
```

### 运行程序

```bash
./servoV6
```

------

## 📦 命令模型与下发流程

1. 用户构建一个命令序列 `CommandSequence`（如：旋转90度 + 设置速度）
2. 调用 `BusinessLogic::executeCommandSequence(motorID, commands)`
3. 使用 `std::visit` 结合 `MotorCommandExecutor` 实现命令分发
4. 分发至 `IServoAdapter` 实现类（如 `GearRotaryAdapter`），进行单位转换等处理
5. 调用 `IMotor` → `ICommProtocol`，最终下发至电机

> 示例命令：

```cpp
CommandSequence sequence = {
  RelativeAngularMove{90.0},
  SetPositionSpeed{30.0}
};
businessLogic.executeCommandSequence("MOTOR_1", sequence);
```

------

## 🧪 测试方法

```bash
cd build
ctest --verbose
```

或直接运行测试可执行文件：

```bash
./tests/test_motorregisteraccessor
```

------

## 📁 示例：P100S 电机驱动适配器

路径：`adapters/motors/P100S/`

职责：

- 实现 `IMotor` 接口，控制 P100S 电机
- 将目标位置、速度等业务数据转换为寄存器指令
- 使用 `MotorRegisterAccessor` 写入寄存器，通过 `SerialCommProtocol` 通信

------

## 📁 示例：串口通信协议实现

路径：`adapters/protocol/SerialCommProtocol.{h,cpp}`

职责：

- 实现 `ICommProtocol` 接口
- 将电机寄存器命令打包为数据帧
- 使用串口 API 完成数据发送与接收

------

## 🔩 可扩展性设计

添加新电机型号，只需：

1. 创建新类实现 `IMotor`
2. 在 `ServoAdapterFactory` 中注册该类
3. 无需修改其他模块（符合开放-封闭原则）

添加新命令，只需：

1. 在 `MovementCommand.h` 中定义新结构
2. 在 `Command` variant 中添加新类型
3. 在 `MotorCommandExecutor` 中实现对应 `visit()` 方法

------

## 📚 参考设计模式

- 抽象工厂模式（适配器创建）
- 命令模式（命令结构体 + 执行）
- 访问者模式（CommandVisitor）
- 依赖倒置原则（仅依赖接口）
- 开放-封闭原则（扩展不修改）

------

## 📞 联系与贡献

如需贡献，请 Fork 后提交 PR，或联系维护者。

------

© 2025 servoV6 项目组。保留所有权利。
