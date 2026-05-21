# QML 界面设计方案 ---- 基于 ViewModel 层结构分析

---

## 一、ViewModel 层能力全景

分析完 `AxisViewModelCore` -> `QtAxisViewModel` -> `ErrorTranslator` 链路后，ViewModel 暴露给 QML 的完整契约如下：

### 1. 状态投射 (Properties)

| 属性                         | 类型   | 说明                    | 当前 QML 覆盖 |
| ---------------------------- | ------ | ----------------------- | :-----------: |
| `state`                      | int    | AxisState 枚举值        |       ✅       |
| `absPos`                     | double | 绝对位置                |       ✅       |
| `relPos`                     | double | 相对位置                |   ❌ 未展示    |
| `isEnabled`                  | bool   | 轴是否已使能            |  ⚠️ 间接使用   |
| `stateText`                  | string | 状态机真人可读文本      | ❌ 硬编码替代  |
| `jogVelocity`                | double | 点动速度                |  ✅ 只读展示   |
| `moveVelocity`               | double | 定位速度                |  ✅ 只读展示   |
| `posLimit` / `negLimit`      | double | 软限位                  |       ✅       |
| `hasError`                   | bool   | 是否有待处理错误        | ⚠️ 未 UI 展示  |
| `errorCode` / `errorMessage` | string | 最后一条错误            |   ❌ 无面板    |
| `errorCategory`              | string | Inline / Modal / Silent |   ❌ 无路由    |
| `errorCount`                 | int    | 未确认错误总数          |   ❌ 无徽标    |

### 2. 控制指令 (Q_INVOKABLE)

| 方法                                              | 说明             | 当前 QML 调用 |
| ------------------------------------------------- | ---------------- | :-----------: |
| `jogPositivePressed()` / `jogPositiveReleased()`  | 正向点动         |       ✅       |
| `jogNegativePressed()` / `jogNegativeReleased()`  | 负向点动         |       ✅       |
| `moveAbsolute(target)` / `moveRelative(distance)` | 定位             |       ✅       |
| `stop()`                                          | 急停             |       ✅       |
| `setJogVelocity(v)` / `setMoveVelocity(v)`        | 速度设置         |   ✅ (弹窗)    |
| `zeroAbsolutePosition()`                          | 绝对零位归零     |   ❌ 无入口    |
| `setRelativeZero()`                               | 设置相对零点     |   ❌ 无入口    |
| `clearRelativeZero()`                             | 清除相对零点     |   ❌ 无入口    |
| `clearError()`                                    | 清除所有错误     |    ⚠️ 无 UI    |
| `getAllErrors()`                                  | 获取全部错误列表 |   ❌ 无面板    |
| `acknowledgeError(index)`                         | 确认单条错误     |   ❌ 无面板    |
| `acknowledgeAllErrors()`                          | 确认全部错误     |   ❌ 无面板    |

### 3. 信号

| 信号                                  | 触发时机                               |
| ------------------------------------- | -------------------------------------- |
| `stateChanged()`                      | 轴状态变更 (连带 isEnabled, stateText) |
| `absPosChanged()` / `relPosChanged()` | 位置变更                               |
| `limitsChanged()`                     | 限位变更                               |
| `velocityChanged()`                   | 速度变更                               |
| `errorChanged()`                      | 错误出现/消失                          |
| `errorCountChanged()`                 | 错误数量变更                           |

---

## 二、当前 QML 与 ViewModel 的差距分析

```
┌─────────────────────────────────────────────────────────────────┐
│  模块              现有状态               ViewModel 已有但未用到  │
├─────────────────────────────────────────────────────────────────┤
│  TelemetryBlock    ✅ absPos 大字显示     ❌ relPos 未展示       │
│                    ✅ 状态指示灯           ❌ stateText 未绑定    │
│                    ✅ 限位进度条           ❌ isEnabled 未驱动UI │
│                    硬编码 getStateText()                         │
│                                                                  │
│  ActionControlBlock ✅ JOG + / JOG -      ❌ 缺少 Enable/Disable │
│                    ✅ Abs / Rel 定位      ❌ 缺少零位操作入口    │
│                    ✅ 速度弹窗                                    │
│                    ✅ 急停按钮                                    │
│                                                                  │
│  (缺失)            --                     ❌ 错误提示面板        │
│                                          ❌ 错误列表/确认       │
│                                          ❌ Inline/Modal 路由   │
│                                          ❌ 底部错误计数徽标    │
│                                                                  │
│  AxisSelectorBlock ✅ 轴列表静态占位      ❌ 切换未动态绑定 VM   │
│  Main.qml          ❌ 全局 VM 硬编码      ❌ 轴切换联动          │
│                    viewModel: axisX1VM                           │
└─────────────────────────────────────────────────────────────────┘
```

---

## 三、推荐 QML 界面设计方案

### 方案 A：增量增强（推荐，与现有架构最契合）

在现有三栏布局基础上，补全缺失模块：

```
┌────────────┬─────────────────────────────┬──────────────────┐
│ ① 轴选择器 │  ② 遥测与状态看板          │  ③ 控制面板     │
│ (增强)     │  (增强)                     │  (增强)          │
│            │                             │                  │
│ ● Y 轴 🔵  │  STATE: IDLE (就绪) ●      │  [点动 | 定位]  │
│   控制中   │                             │                  │
│ ○ Z 轴     │  当前绝对位置               │  ┌─ 使能 ──────┐│
│ ○ R 轴     │     +125.430 mm            │  │ [ENABLE]     ││
│ ○ X1X2联动 │                             │  │ [DISABLE]    ││
│            │  当前相对位置 (新增)         │  └─────────────┘│
│            │     +25.430 mm              │                 │
│            │                             │  JOG [+] [-]   │
│            │  ├──────────●──────────┤    │                 │
│            │  -500                +500   │  速度: 50 mm/s⚙│
│            │                             │                 │
│            │  ④ 零位操作 (新增)           │  GO 绝对定位    │
│            │  [清零绝对] [设相对零]       │  GO 相对定位    │
│            │  [清相对零]                 │                 │
│            │                             │  [ 急 停 ]     │
│            │                             │                 │
│            │                             │  ⑤ 零位操作    │
│            │                             │  (备选位置)     │
│            │                             │                 │
├────────────┴─────────────────────────────┴──────────────────┤
│ ⑥ 错误信息栏 (新增，固定在底部)                             │
│  ⚠ 2 条未确认  │  [E1] 轴已到达正向限位 (Inline)        [×] │
│                 │  [E2] 通讯中断 (Modal)             [确认全部]│
└─────────────────────────────────────────────────────────────┘
```

### 各模块详细设计

#### ① AxisSelectorBlock（增强）

**改动**：将静态列表改为**动态驱动** `QtAxisViewModel` 实例。

```
属性变更：
  - 新增 property list<QtAxisViewModel*> axisViewModels
  - 新增 property int currentIndex
  - axisChanged -> 触发 Main.qml 切换 TelemetryBlock/ActionControlBlock 的 viewModel 绑定

每个 AxisItemDelegate 新增：
  - 显示该轴的 isEnabled 状态 (绿灯/灰灯)
  - 显示该轴的 hasError 红色徽标
  - statusText 自动从 viewModel.stateText 读取
```

**注意**：当前单 ViewModel 架构暂时支持 Y 轴，可直接在 Main.qml 中根据 axisChanged 字符串创建/切换 `AxisViewModelCore` 实例。

#### ② TelemetryBlock（增强）

**改动**：

1. **状态显示**：用 `viewModel.stateText` 替代本地 `getStateText()`，消除硬编码
2. **新增相对位置**：在大数字下方展示 `relPos`，格式 `相对: +25.430 mm`
3. **新增 isEnabled 指示灯**：标题栏右侧增加 "已使能/未使能" 徽标
4. **零位操作区**：3 个按钮内嵌到 TelemetryBlock 底部空白区（与遥测数据强关联）

```
新增属性绑定：
  Text { text: viewModel ? viewModel.stateText : "--" }
  Text { text: viewModel ? "+" + viewModel.relPos.toFixed(3) : "0.000" }
  
零位按钮：
  IndustrialButton { text: "⚡ 清零"; onClicked: viewModel.zeroAbsolutePosition() }
  IndustrialButton { text: "⊙ 设零"; onClicked: viewModel.setRelativeZero() }
  IndustrialButton { text: "⊗ 清除"; onClicked: viewModel.clearRelativeZero() }
```

#### ③ ActionControlBlock（增强）

**改动**：

1. **新增使能/去使能按钮**（放置在模式切换器上方）：
   ```
   RowLayout {
       IndustrialButton {
           text: viewModel && viewModel.isEnabled ? "⚡ 已使能" : "⚡ 使能"
           baseColor: viewModel && viewModel.isEnabled ? Theme.colorIdle : Theme.panelBg
           onClicked: viewModel.enable() // 需要一个 Q_INVOKABLE enable()
       }
       IndustrialButton {
           text: "断电"
           baseColor: Theme.colorError
           enabled: viewModel && viewModel.isEnabled
           onClicked: viewModel.disable() // 需要一个 Q_INVOKABLE disable()
       }
   }
   ```
   ⚠️ 注意：`enable()`/`disable()` 当前在 `AxisViewModelCore` 中，但 `QtAxisViewModel` 未暴露。需要在 `QtAxisViewModel` 中添加：
   ```cpp
   Q_INVOKABLE void enable()  { if (m_core) m_core->enable(true); }
   Q_INVOKABLE void disable() { if (m_core) m_core->disable(); }
   ```

2. **防呆逻辑升级**：将 `isReadyForPos` 改为直接使用 `viewModel.isEnabled`：
   ```qml
   property bool isReadyForPos: viewModel ? 
       (viewModel.isEnabled && (viewModel.state === 2)) : false
   ```

#### ⑥ 错误信息栏（全新模块 `presentation/qml/blocks/ErrorPanelBlock.qml`）

这是最核心的新增模块，ViewModel 已有完整的错误列表接口但 QML 完全未使用。

**设计要点**：

```
┌─────────────────────────────────────────────────────────────┐
│  ⚠ 2 条提醒            [全部确认]                          │
├─────────────────────────────────────────────────────────────┤
│  ⓘ 轴已到达正向限位 -- 当前位置已超出允许范围  Inline   [×] │
│  ⚡ 通讯中断 -- 与驱动器通讯失败，请检查连接  Modal    [×] │
└─────────────────────────────────────────────────────────────┘
```

**属性接口**：
```qml
// ErrorPanelBlock.qml
property var viewModel: null

// 绑定
visible: viewModel ? viewModel.errorCount > 0 : false
```

**错误分类路由**：
- `Inline` -> 直接在面板内显示，不打断操作
- `Modal` -> 额外弹出 `MessageDialog`，需要显式确认
- `Silent` -> 不显示，仅记录（可通过 `visible: category !== "Silent"` 过滤）

**实现方式**：
```qml
// 使用 getAllErrors() 获取全部错误 -> ListView 展示
// 每行绑定 error.code (图标), error.message (文本), error.category (颜色/行为)
// [×] 按钮调用 viewModel.acknowledgeError(index)
// [全部确认] 调用 viewModel.acknowledgeAllErrors()

ListView {
    model: viewModel ? viewModel.getAllErrors() : []
    delegate: RowLayout {
        // 根据 category 选择图标
        Image { source: modelData.category === "Modal" ? "⚡" : "ⓘ" }
        Text { text: modelData.message }
        IndustrialButton { text: "×"; onClicked: viewModel.acknowledgeError(index) }
    }
}
```

⚠️ 注意：`getAllErrors()` 返回 `QVariantList`，QML 中可以用 `modelData.code`、`modelData.message`、`modelData.category` 访问。

**错误计数徽标**：在 TelemetryBlock 或 ActionControlBlock 状态栏添加：
```qml
Rectangle {
    visible: viewModel && viewModel.errorCount > 0
    Text { text: viewModel ? viewModel.errorCount : 0 }
    color: Theme.colorError
}
```

---

### 方案 B：重新布局（激进）

将 TelemetryBlock 和 ActionControlBlock 合并为一个 **轴详情视图**（AxisDetailView），切换轴时整块替换。优势是多轴管理更清晰，但改动量大，暂不推荐。

---

## 四、需要补充的 QtAxisViewModel 接口

当前 `QtAxisViewModel` 缺少以下 Q_INVOKABLE 以支持完整 UI：

| 缺失方法    | 用途       | 实现                   |
| ----------- | ---------- | ---------------------- |
| `enable()`  | 使能按钮   | `m_core->enable(true)` |
| `disable()` | 去使能按钮 | `m_core->disable()`    |

这两个是必须的。其他接口（零位操作、错误管理）已完整。

---

## 五、实现路径建议

按优先级排序：

| 优先级 | 模块                                              | 工作量 | 说明                  |
| :----: | ------------------------------------------------- | :----: | --------------------- |
| **P0** | QtAxisViewModel 补全 enable/disable               |  极小  | 2 行代码              |
| **P1** | ErrorPanelBlock 新建                              |   中   | 核心缺失，VM 接口完善 |
| **P2** | TelemetryBlock 增强（stateText/relPos/isEnabled） |   小   | 消除硬编码            |
| **P3** | ActionControlBlock 添加 Enable/Disable 按钮       |   小   | 搭配 P0               |
| **P4** | TelemetryBlock 零位操作区                         |   小   | 3 个按钮              |
| **P5** | AxisSelectorBlock 动态化 + Main.qml VM 切换       |   中   | 多轴支持              |

---

## 六、总结

ViewModel 层接口设计得非常规范----**状态投射、控制指令、错误管理三组接口分离清晰**，`QtAxisViewModel` 通过 Q_PROPERTY + signal 将 C++ 数据完整映射到 Qt 属性系统。当前 QML 界面**已覆盖约 60% 的 ViewModel 能力**，主要缺口在于：

1. **错误系统完全沉默** -- 这是最大的功能空白
2. **使能/去使能无 UI 入口** -- 核心安全操作缺失
3. **零位操作无入口** -- 辅机校准功能缺失
4. **状态文本硬编码** -- 维护风险

建议从 ErrorPanelBlock 开始补全，这是投入产出比最高的模块。