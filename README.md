拉取完项目主仓库之后（比如 `git clone` 完成后），请运行以下命令：

```bash
git submodule update --init --recursive
```

含义解释：

- `--init`: 初始化子模块（即 `.gitmodules` 中定义的路径）
- `--recursive`: 如果子模块中还有子模块，也会一并拉取（`spdlog` 没有，但好习惯）

------

## 🔷 顶层目录结构

```bash
servoV6/
├── adapters/            # ✅ 适配器层，组合功能+型号+协议
├── app/                 # 🚀 程序入口 + Qt前端
├── application/         # 🧠 核心业务逻辑（调度/执行）
├── build/               # ⚙️ 构建输出目录（由 Qt Creator 生成）
├── domain/              # 📐 领域模型定义（接口、抽象命令）
├── external/            # 📦 外部依赖（spdlog 等）
├── tests/               # ✅ 单元测试
├── utils/               # 🔧 工具类（日志等）
├── CMakeLists.txt*      # 构建配置
├── README.md            # 项目说明
```

------

## 🧩 模块功能详解

### 1. `adapters/`：**适配器分层组合模块**

```bash
adapters/
├── ServoAdapterFactory.*  # 工厂类：根据功能+型号+协议创建 ServoAdapter
├── motors/                # 电机型号（IMotor）相关实现
├── protocol/              # 通信协议实现（Modbus/Bluetooth...）
└── servos/                # ServoAdapter 的功能实现（旋转/线性等）
```

#### ✅ 模块职责

- 解耦「型号（硬件特性）」←→「功能（业务需求）」←→「协议（通讯方式）」
- 将三者组合成统一的 `IServoAdapter` 实例

------

### 2. `app/`：**程序入口 + Qt前端**

```bash
app/
├── main.cpp       # Qt 应用程序入口
└── qml/           # QML 界面（可能用于配置或显示电机状态）
```

#### ✅ 模块职责

- 初始化 Qt 应用
- 创建 GUI 界面
- 调用 `BusinessLogic` 实现系统功能

------

### 3. `application/`：**业务控制逻辑**

```bash
application/
├── BusinessLogic.*         # 系统调度器，执行多个电机动作的逻辑入口
├── MotorCommandExecutor.*  # 具体执行控制逻辑，驱动 ServoAdapter
```

#### ✅ 模块职责

- 管理指令序列（命令队列/系统状态机）
- 执行单个或批量电机动作（控制速度、位置等）

------

### 4. `domain/`：**核心领域接口**

```bash
domain/
├── IMotor.h           # 电机型号接口（定义圈数、速度等硬件特性）
├── IServoAdapter.h    # 功能接口（线性/旋转等）
├── ICommProtocol.h    # 通信接口（读写寄存器）
├── MovementCommand.h  # 电机动作指令
├── CommandVisitor.h   # 指令访问器（Visitor 模式）
```

#### ✅ 模块职责

- 所有业务接口定义于此，**不依赖任何实现**
- 上层逻辑通过接口驱动系统，确保解耦与可测试性

------

### 5. `utils/`：**工具模块**

```bash
utils/
├── Logger.*           # 封装 spdlog 日志接口
```

#### ✅ 模块职责

- 提供系统通用工具，如日志、时间戳、配置读取等

------

### 6. `tests/`：**测试模块**

```bash
tests/
├── mocks/                  # 模拟接口，用于脱离硬件测试
├── test_adapter.cpp        # 测试 servos/motors/protocol 的组合
├── test_motor.cpp          # 测试单个电机的接口行为
├── test_businesslogic.cpp  # 测试业务逻辑
├── test_main.cpp           # 启动测试（可能为集成测试）
```

#### ✅ 模块职责

- 按照模块拆分测试内容
- 使用 mock 实现与硬件解耦
- 测试覆盖 adapter / application / domain 等核心路径

------

### 7. `build/`：**构建输出目录（由 Qt Creator 自动生成）**

```bash
build/
└── Desktop_Qt_6_9_0_MinGW_64_bit-Debug/
```

> ⚠️ 不建议将 `build/` 放入版本控制（可添加 `.gitignore`）

------

### 8. 顶层文件

```
CMakeLists.txt        # 总构建文件
CMakeLists.txt.user   # Qt Creator IDE 配置文件（可忽略）
README.md             # 项目说明文档
```

------

## 🔚 总结建议

| 模块名                  | 描述                                  | 建议                     |
| ----------------------- | ------------------------------------- | ------------------------ |
| `adapters/`             | 组合协议 + 功能 + 型号                | 结构优，已工厂封装       |
| `application/`          | 调度器 + 执行器，连接业务与接口       | 可扩展为支持更多命令     |
| `domain/`               | 接口抽象层                            | 保持纯净不引入依赖       |
| `tests/`                | 分模块测试                            | 可继续扩展 mock 自动生成 |
| `utils/`                | 通用代码复用模块                      | 可加入配置/时间工具等    |
| `infrastructure/`（缺） | 串口/蓝牙等平台相关实现               | 建议加入此目录放底层依赖 |
| `systems/`（可选）      | MotorSystemManager 之类的系统配置模块 | 用于封装初始化流程       |
