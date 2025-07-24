# 架构

现在需要重新设计`servo`的Qt驱动程序。此文档记录了自己关于`servo`驱动架构的思考。

## 项目特点

现在要开发的`servo`驱动程序，有以下特点：

- 使用Qt6作为后端语言+Qtquick前端展示界面交互开发
- 基于`TDD`的开发逻辑。测试先于开发。
- 函数体短小精炼，不允许出现冗杂函数；函数名恰如其功能，简洁明了。
- 分层架构，独立抽象层级。
- 完善的日志系统，可以完整打印日志。
- 支持windows和Android设备多平台。
- 此平台根据配置项（Db），可以操作不同电机完成相同的电机功能（例如点动、位置移动等）

### 分层思考

针对后端分层思想，有更多的思考：（以下分层是从底层到高层的顺序）

- 通讯层：负责和设备建立物理连接，封装协议格式。支持的协议有`MODBUS`和`Bluetooth`
- 寄存器读写：对寄存器读写做统一封装。所有型号的伺服，都是通过约定好的寄存器地址来进行读写的。读写的位数在16、32和64位。
- 电机功能：这是每个伺服通过不同时序的寄存器进行读写，就可以实现的操作。每个电机的功能，均由不同寄存器的操作通过时序逻辑来完成。
- 业务功能：业务功能就是根据提供的电机功能，进行组合调整来的。 

这里着重分析下电机功能。这是比较重要的部分，是业务功能正常运转的支柱。

但是现在存在一个问题：

现在A电机和B电机属于不同型号的电机，但是需要做相同的电机操作：例如业务需要A和B同时下降30mm（位置移动），然后回到原点（位置移动）。这时候为了代码复用性和简化性，应该怎么设计这个分层思路呢？

### 抽象电机功能

上述分层问题本质是**电机差异对上层业务逻辑带来的侵入性**。

解决这个问题的关键是：**将电机型号差异屏蔽在底层，把“抽象的电机功能”上提成标准接口**，这样上层业务只调用统一接口，不关心电机具体型号。

```scss
通讯层（Modbus, Bluetooth）
  ↑
寄存器访问层（通用读写、对齐、掩码、打包）
  ↑
设备驱动适配层（每种伺服型号的适配器）
  ↑
统一电机功能接口层（“MoveTo(x)”, “Home()”）
  ↑
业务逻辑层（“下降30mm”, “原点复位”）
```

这里将电机功能，又抽出了一层”设备驱动适配层“。原因是：

> 分层的核心目的，是：
>
> > **将变化隔离、将稳定提取、将不同关注点分开**。
>
> 你提到的“电机功能”是业务的支柱，但不同电机型号的实现差异（寄存器地址、时序逻辑）是不可避免的。如果你直接把这部分逻辑写进“电机功能层”，你就很难做到：
>
> - 代码复用（每种电机功能都要写一遍）
> - 单元测试（要 mock 很多底层逻辑）
> - 维护扩展（增加一种新电机，需要修改已有逻辑）
>
> 所以我们**必须抽出“设备驱动适配层”**，把“电机型号相关的实现差异”集中在一个地方。

## 后端分层

根据上述指导意见，重新设计的分层逻辑为（从底向上）：

### 1.通讯层（Communication）

> **职责：** 负责和设备建立物理连接，封装协议格式

- 支持 Modbus RTU、Bluetooth BLE
- 提供统一接口：如 `sendRequest(bytes)`, `receiveResponse()`
- 屏蔽不同通信方式的差异

业务不应该关心你是串口、蓝牙还是USB。

### 2.寄存器访问层（Register I/O）

> **职责：** 对寄存器读写做统一封装

- 提供如 `read16(addr)`、`write32(addr, value)` 接口
- 屏蔽字节对齐、大小端、数据宽度等硬件细节
- 加入读写日志、失败重试、延迟等控制机制

上层不应自己计算寄存器偏移或拼接数据。

### 3.设备驱动适配层（Servo Adapter Layer）

> **职责：** 将“某种电机”的所有寄存器细节封装为统一的功能接口实现。

- 每种电机型号一个类，如 `ServoAdapter_A`、`ServoAdapter_B`

- 每个类内部知道该型号电机的寄存器分布和控制时序

- 实现统一接口：

  ```CPP
  interface IServoAdapter {
      bool moveToPosition(double mm);
      bool home();
      bool stop();
  }
  ```

#### 驱动适配层存在的理由

1. 将“型号差异”集中隔离

   电机型号之间：

   - 控制寄存器地址不同
   - 运动指令的执行顺序/时序不同
   - 一些支持位置控制，一些不支持

   **这不是“电机功能层”应该关心的**，否则它必须 if else 判断“是不是 A 型号”，这会导致“功能逻辑”和“硬件实现”耦合得很紧。

2. 提供稳定的上层接口

   上层希望看到的是：

   ```cpp
   motor->moveToPosition(30);
   ```

   不希望看到：

   ```cpp
   if (motor.type == A) {
       writeReg(0x01, 30);
       writeReg(0x02, 1);
       delay(100);
   } else if (motor.type == B) {
       ...
   }
   ```

3. 单元测试友好

   接口可 mock，如：

   ```cpp
   Mock<IServoAdapter> mock;
   EXPECT_CALL(mock, moveToPosition(30)).WillOnce(Return(true));
   ```

4. 可扩展

   要增加一个新型号电机：

   - 只需实现一个新的 `ServoAdapter_C`
   - 无需修改业务层、电机功能层、通讯层

### 4.电机功能层（Motor Functional Layer）

> **职责：** 统一定义“电机应该提供的功能”，不关心型号、寄存器。

- 定义接口，如 `moveToPosition(mm)`, `home()`, `jog(direction)`
- 持有一个 `IServoAdapter*`
- 负责调度、状态管理，但不负责具体寄存器读写

示例：

```CPP
class Motor {
    std::unique_ptr<IServoAdapter> adapter;
public:
    bool moveTo(double mm) {
        return adapter->moveToPosition(mm);
    }
};
```

“伺服功能的抽象定义”，是真正可复用的核心。

### 5. 业务功能层（Business Logic）

> **职责：** 把多个电机功能组合成完整的业务流程

- 控制多个电机同时移动/复位/回原点
- 执行如“下降30mm→停3s→回原点”
- 实现 TDD 的测试逻辑

其总结的UML图为

```scss
                      +------------------------+
                      |    ICommInterface      |  <-- 通讯层接口
                      +------------------------+
                      | + send(data): bool     |
                      | + receive(): QByteArray|
                      +------------------------+

                      +----------------------------+
                      |    RegisterAccessor        |  <-- 寄存器读写层
                      +----------------------------+
                      | - comm: ICommInterface*    |
                      +----------------------------+
                      | + read16(addr): quint16    |
                      | + write32(addr, val): void |
                      +----------------------------+

                      +------------------------------+
                      |       IServoAdapter          |  <-- 适配器接口层（关键）
                      +------------------------------+
                      | + moveToPosition(mm): bool   |
                      | + home(): bool               |
                      | + stop(): bool               |
                      +------------------------------+

          +--------------------+      +----------------------+
          |  ServoAdapter_A    |      |   ServoAdapter_B     | <-- 不同型号驱动实现
          +--------------------+      +----------------------+
          | - reg: RegisterAccessor*  | - reg: RegisterAccessor*
          +--------------------+      +----------------------+
          | + moveToPosition() |      | + moveToPosition()   |
          | + home()           |      | + home()             |
          | + stop()           |      | + stop()             |
          +--------------------+      +----------------------+

                      +------------------------+
                      |        Motor           |  <-- 电机功能层
                      +------------------------+
                      | - adapter: IServoAdapter* |
                      +------------------------+
                      | + moveTo(mm): bool     |
                      | + home(): bool         |
                      | + stop(): bool         |
                      +------------------------+

                      +--------------------------+
                      |      BusinessLogic       |  <-- 业务逻辑层
                      +--------------------------+
                      | - motorMap: Map<string, Motor> |
                      +--------------------------+
                      | + runDropThenReturn()    |
                      | + jogAll()               |
                      +--------------------------+
```

## 开发流程

基于TDD开发流程，推荐是自顶向下：从业务出发、设计接口、写测试、再逐层实现。

> “业务驱动、接口主导、底层验证”—— 以业务目标为核心，驱动接口设计，再自底向上实现验证。

### 阶段 1：确定业务目标和关键电机功能

> 目标：确定核心业务行为 + 电机抽象接口。明确业务使用的动作模型 → 建立统一结构 `MovementCommand`

- 举例：

  > "设置速度20mm/s → 相对位置移动上升/下降 30mm → 停顿 2s → 回原点”
  >
  > "设置速度30mm/s → 绝对位置移动到 100mm → 停顿 3s → 回原点”
  >
  > "设置速度20mm/s → 点动前进/后退 80mm → 停止"
  >
  > "设置速度30°/s → 点动旋转 300° → 停止"

- 指令示意：

  ```CPP
  enum class MotionType {
      Relative,
      Absolute,
      Jog
  };
  
  enum class MotionUnit {
      Millimeter,
      Degree
  };
  
  struct MovementCommand {
      double speed;                            // 单位取决于 unit：mm/s 或 deg/s
      double value;                            // 相对位移、绝对位置、点动长度（mm/°）
      MotionType type;                         // 相对 / 绝对 / 点动
      MotionUnit unit;                         // 毫米 / 角度
      std::chrono::milliseconds pauseAfter{0}; // 是否需要延迟
      bool returnToHome = false;               // 是否需要回原点
  };
  ```

TDD动作

  - 编写 MovementCommand 的构造测试、序列化 / 反序列化测试（如 JSON）
  - 验证命令是否可以正确表达全部业务行为（上升/下降、点动、绝对定位等）

###  阶段 2：业务执行层接口设计

> 目的：通过 `execute(MovementCommand)` 驱动电机完成完整业务流程

- 编写 `BusinessLogic::execute(Motor&, MovementCommand&)` 方法接口

- 抽象 motor 行为（不依赖具体设备）

- 实现中立逻辑，如“设置速度 → 移动 → 等待 → 回原点”

TDD动作

- 使用 MockMotor 编写如下测试：
  - 测试 execute(cmd) 是否按顺序调用 motor 的接口
  - 测试 pause 时间与回原点逻辑是否正确
  - 测试错误参数抛出/异常流程
  - 使用 GTest 或 Qt Test 模拟调用链：

```Cpp
TEST(BusinessLogicTest, RelativeMotionWithPauseAndHome) {
    MockMotor motor;
    MovementCommand cmd = {20.0, 30.0, MotionType::Relative, MotionUnit::Millimeter, 2000ms, true};

    EXPECT_CALL(motor, setSpeed_mm(20.0));
    EXPECT_CALL(motor, moveRelative(30.0));
    EXPECT_CALL(motor, home());

    BusinessLogic logic;
    logic.execute(motor, cmd);
}
```

### 阶段 3：Motor 层设计

> 目的：提供标准电机操作接口，屏蔽设备差异

- 定义 `Motor` 类作为业务与设备之间的桥梁
- 不处理寄存器，仅组织调用 `IServoAdapter`

TDD动作：

- MockServoAdapter → 测试 `Motor` 是否正确调用对应底层接口

- 例如：

  ```CPP
  motor.setSpeed_mm(20.0) → adapter.writeSpeed_mm(20.0)
  motor.moveTo(100.0) → adapter.writeTargetAbsolute(100.0)
  ```

### 阶段 4：设备适配器 IServoAdapter 实现

> 目的：屏蔽硬件寄存器差异，为 Motor 提供一致的设备调用能力

- 以协议为单位定义子类：
  - `ModbusServoAdapter`：操作具体寄存器地址
  - `BluetoothServoAdapter`：发送 BLE 指令帧
- 每种电机型号提供其具体的 adapter 实现

TDD动作：

- 针对每个 Adapter 实现测试：
  - 检查寄存器写入是否正确（可用虚拟串口 / mock modbus）
  - 检查异常寄存器响应处理
- 示例：

```cpp
EXPECT_EQ(adapter.writeSpeed_mm(20.0), true);
EXPECT_EQ(readRegister(addr), expectedValue);
```

### 阶段 5：通信协议层（可选）

> 目的：统一串口/BLE/网口底层通信

- 提供串口通信管理器、BLE帧封装器
- 发送 / 接收数据字节流

TDD动作：

- 单元测试协议打包、校验、超时重传等
- 可使用虚拟串口测试端到端通信稳定性

```css
[业务场景]
     ↓
MovementCommand 数据结构（可配置/序列化）
     ↓
BusinessLogic::execute(Motor, MovementCommand)
     ↓
Motor 层（组合控制流）
     ↓
IServoAdapter（抽象设备控制接口）
     ↓
ServoAdapterA / ServoAdapterB 实现寄存器逻辑
     ↓
通信接口层（Modbus、BLE、串口…）
```

