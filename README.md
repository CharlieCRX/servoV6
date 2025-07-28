# 架构调整

因为加入了QTest结构，所以现在需要更新出当前的项目结构。

## ServoV6 项目模块结构分析

```text
.
├── app/              ⟶ UI层：应用入口，QML界面展示
│
├── core/             ⟶ 核心逻辑层：运动命令建模、业务逻辑调度、电机接口
│   ├── MovementCommand.h       ⟶ 表达单个运动动作（如设置速度、移动、等待）
│   ├── IMotor.h                ⟶ 电机行为抽象接口（标准运动控制指令）
│   ├── Motor.h/.cpp            ⟶ `IMotor` 实现，聚合 `IServoAdapter` 完成控制流
│   ├── IServoAdapter.h         ⟶ 适配器抽象接口（底层驱动行为，如写寄存器）
│   ├── businesslogic.h/.cpp    ⟶ 高层业务调度器，依赖 IMotor 执行命令序列
│
├── drivers/          ⟶ 设备适配层：实现具体型号电机的底层行为
│   ├── ModbusServoAdapter.h/.cpp     ⟶ 基于 Modbus 控制寄存器的适配器
│   ├── BluetoothServoAdapter.h/.cpp  ⟶ 基于蓝牙设备的控制逻辑
│
├── transport/        ⟶ 通信接口层：封装 Modbus 协议细节、串口读写等
│   ├── ModbusClient.h/.cpp     ⟶ Modbus 协议解析和收发逻辑
│
├── utils/            ⟶ 工具模块：日志等通用功能
│   ├── Logger.h/.cpp           ⟶ 使用 spdlog 统一日志输出
│
├── tests/            ⟶ 单元测试模块：使用 GoogleTest 构建
│   ├── mocks/                 ⟶ 存放 MockMotor、MockAdapter 等模拟依赖
│   ├── test_businesslogic.cpp ⟶ 测试业务调度器 BusinessLogic 的行为
│   ├── test_motor.cpp         ⟶ 测试 Motor 层命令分发逻辑
│   ├── test_adapter.cpp       ⟶ 测试适配器（如寄存器值是否正确设置）
│
├── external/         ⟶ 第三方依赖（如 spdlog）
│
├── CMakeLists.txt    ⟶ 构建系统（支持模块化编译、测试构建、安装等）
```

## 模块依赖关系

展示逻辑层级与依赖方向（箭头表示依赖）：

```csharp
[app UI]
   ↓
[BusinessLogic]  ← 业务流程控制器
   ↓
[IMotor]         ← 高层抽象接口，屏蔽控制细节
   ↓
[Motor]          ← 控制器实现，内部组合 IServoAdapter
   ↓
[IServoAdapter]  ← 适配器接口
   ↓
[ServoAdapterX]  ← 各型号电机具体实现（如写寄存器）
   ↓
[Transport]      ← 通信实现（ModbusClient、串口等）
```

## 模块简表总结

| 模块名          | 所在目录     | 主要职责                                         |
| --------------- | ------------ | ------------------------------------------------ |
| MovementCommand | `core/`      | 表达运动动作的原子命令                           |
| IMotor          | `core/`      | 电机控制的接口抽象                               |
| Motor           | `core/`      | 封装运动命令组合流程，转为低层指令               |
| IServoAdapter   | `core/`      | 电机适配器接口，隐藏寄存器细节                   |
| BusinessLogic   | `core/`      | 高层业务流程执行器，调用 IMotor 执行命令序列     |
| ServoAdapterX   | `drivers/`   | 控制具体硬件设备的寄存器写入逻辑                 |
| ModbusClient    | `transport/` | 底层 Modbus 通信协议封装                         |
| Logger          | `utils/`     | 日志工具，统一日志接口                           |
| test_*.cpp      | `tests/`     | 针对每层模块进行单元测试，使用 Mock 对象解耦依赖 |

