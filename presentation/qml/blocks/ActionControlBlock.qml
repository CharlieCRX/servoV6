import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import servoV6
Rectangle {
    id: root
    property var viewModel: null
    property var emergencyViewModel: null  // 急停安全 ViewModel
    property var gantryViewModel: null     // 龙门 ViewModel
    property string currentAxis: ""        // 当前选中的轴名（用于龙门逻辑判断）

    // 内部状态：0 = 点动模式 (Jog), 1 = 定位模式 (Position)
    property int currentMode: 0 
    // 定位模式下的子状态：true = 绝对, false = 相对
    property bool isAbsolute: true 

    // ── 系统锁定 = 安全锁定 + 轴本身不可用 ──
    // 安全锁定：NotSynchronized / EmergencyStopping / EmergencyStopped / ReleasingEmergencyStop
    // 轴本身不可用：未绑定 viewModel
    property bool systemLocked: {
        if (emergencyViewModel && emergencyViewModel.isSystemLocked) return true
        return false
    }

    // ── 龙门操作锁定 ──
    // 规则1：选中 X（逻辑龙门轴）但龙门未耦合 → 禁止操作（需先耦合）
    // 规则2：选中 X1/X2（物理轴）但龙门已耦合 → 禁止操作（物理轴受龙门控制）
    readonly property bool gantryOperationLocked: {
        if (!gantryViewModel) return false
        if (currentAxis === "X" && !gantryViewModel.isCoupled) return true   // 逻辑轴未耦合
        if ((currentAxis === "X1" || currentAxis === "X2") && gantryViewModel.isCoupled) return true  // 物理轴受龙门控制
        return false
    }

    // ── 龙门锁定提示文本 ──
    readonly property string gantryLockReason: {
        if (!gantryOperationLocked) return ""
        if (currentAxis === "X" && gantryViewModel && !gantryViewModel.isCoupled)
            return "龙门未耦合"
        if ((currentAxis === "X1" || currentAxis === "X2") && gantryViewModel && gantryViewModel.isCoupled)
            return "受龙门控制"
        return ""
    }

    // 点动模式可用条件：非系统锁定 + viewModel 绑定
    property bool jogEnabled: !systemLocked && viewModel !== null

    // ★ 定位模式下触发是否就绪：
    //    - 系统未锁定
    //    - 无故障
    //    - 非运动中（state ≤ Idle）
    //    - Policy 未运行中
    property bool isReadyForTrigger: !systemLocked && viewModel ? 
        (!viewModel.hasError && viewModel.state <= 2 && !viewModel.isLoading) : false

    // ★ 设置目标是否就绪（同触发条件，但 loading 时仍可设置新目标覆盖旧目标）：
    property bool isReadyForSetTarget: !systemLocked && viewModel ? 
        (!viewModel.hasError && viewModel.state <= 2) : false

        color: "transparent"

    ColumnLayout {
        anchors.fill: parent
        spacing: 6 * Theme.scale

        // ==========================================
        // 0. 紧急急停状态横幅（危险状态时显示）
        // ==========================================
        Rectangle {
            Layout.fillWidth: true
            height: emergencyViewModel && emergencyViewModel.safetyStateText !== "" ? 36 * Theme.scale : 0
            visible: emergencyViewModel && emergencyViewModel.safetyStateText !== ""
            color: "#D32F2F"
            radius: 4 * Theme.scale

            Rectangle {
                anchors.fill: parent
                color: "#D32F2F"
                radius: 4 * Theme.scale
                visible: emergencyViewModel && emergencyViewModel.isEmergencyStopped
                SequentialAnimation on opacity {
                    running: emergencyViewModel && emergencyViewModel.isEmergencyStopped
                    loops: Animation.Infinite
                    NumberAnimation { from: 1.0; to: 0.4; duration: 600 }
                    NumberAnimation { from: 0.4; to: 1.0; duration: 600 }
                }
            }

            Text {
                anchors.centerIn: parent
                text: emergencyViewModel ? emergencyViewModel.safetyStateText : ""
                color: "#FFFFFF"
                font.pixelSize: Theme.fontSmall
                font.bold: true
                font.family: "Monospace"
            }
        }

        // ==========================================
        // 0.5 龙门操作锁定横幅（非急停但龙门锁定操作时显示）
        // ==========================================
        Rectangle {
            Layout.fillWidth: true
            height: root.gantryOperationLocked && !root.systemLocked ? 30 * Theme.scale : 0
            visible: root.gantryOperationLocked && !root.systemLocked
            color: "#5D4037"
            radius: 4 * Theme.scale

            Text {
                anchors.centerIn: parent
                text: "🔒 " + root.gantryLockReason
                color: "#FFCC80"
                font.pixelSize: Theme.fontSmall
                font.bold: true
                font.family: "Monospace"
            }
        }

        // ==========================================
        // 1. 顶部：模式切换器
        // ==========================================
        Rectangle {
            Layout.fillWidth: true
            height: 34 * Theme.scale
            radius: 8 * Theme.scale
            color: Theme.bgDark
            border.color: Theme.borderMain
            border.width: 1
            opacity: systemLocked ? 0.4 : 1.0

            RowLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.currentMode === 0 ? Theme.panelBg : "transparent"
                    radius: 8 * Theme.scale
                    Text {
                        anchors.centerIn: parent
                        text: "点动"
                        color: root.currentMode === 0 ? Theme.textMain : Theme.textDim
                        font.bold: root.currentMode === 0
                        font.pixelSize: Theme.fontSmall
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: !systemLocked
                        onClicked: root.currentMode = 0
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.currentMode === 1 ? Theme.panelBg : "transparent"
                    radius: 8 * Theme.scale
                    Text {
                        anchors.centerIn: parent
                        text: "定位"
                        color: root.currentMode === 1 ? Theme.textMain : Theme.textDim
                        font.bold: root.currentMode === 1
                        font.pixelSize: Theme.fontSmall
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: !systemLocked
                        onClicked: root.currentMode = 1
                    }
                }
            }
        }

        // ==========================================
        // 2. 中间：动态控制面板
        // ==========================================
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            opacity: systemLocked ? 0.4 : 1.0

            // --- A. 点动控制面板 ---
            ColumnLayout {
                anchors.fill: parent
                spacing: 8 * Theme.scale
                visible: root.currentMode === 0

                // 顶部留空
                Item { Layout.preferredHeight: 4 * Theme.scale }

                // 点动速度设定
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 8 * Theme.scale

                    Text {
                        text: "点动速度:"
                        color: Theme.textDim
                        font.pixelSize: Theme.fontNormal
                        font.family: "Monospace"
                    }
                    Text {
                        text: viewModel ? viewModel.jogVelocity.toFixed(1) : "0.0"
                        color: Theme.colorIdle
                        font.pixelSize: Theme.fontNormal
                        font.bold: true
                        font.family: "Monospace"
                    }
                    Text {
                        text: "mm/s"
                        color: Theme.textDim
                        font.pixelSize: Theme.fontNormal
                        font.family: "Monospace"
                    }

                    IndustrialButton {
                        text: "⚙️"
                        buttonSize: 30 * Theme.scale
                        isCircle: true
                        baseColor: Theme.panelBg
                        enabled: root.jogEnabled
                        onClicked: {
                            jogVelocityNumPad.inputText = viewModel ? viewModel.jogVelocity.toString() : "0.00"
                            jogVelocityNumPad.open()
                        }
                    }
                }

                // 上半弹簧
                Item { Layout.fillHeight: true }

                // JOG+ 按钮
                IndustrialButton {
                    text: "JOG +"
                    isCircle: false
                    buttonSize: 170 * Theme.scale
                    Layout.alignment: Qt.AlignHCenter
                    enabled: root.jogEnabled
                    onPressed: if(viewModel && root.jogEnabled) viewModel.jogPositivePressed()
                    onReleased: if(viewModel && root.jogEnabled) viewModel.jogPositiveReleased()
                }

                // JOG+ / JOG- 间隙
                Item { Layout.preferredHeight: 8 * Theme.scale }

                // JOG- 按钮
                IndustrialButton {
                    text: "JOG -"
                    isCircle: false
                    buttonSize: 170 * Theme.scale
                    Layout.alignment: Qt.AlignHCenter
                    enabled: root.jogEnabled
                    onPressed: if(viewModel && root.jogEnabled) viewModel.jogNegativePressed()
                    onReleased: if(viewModel && root.jogEnabled) viewModel.jogNegativeReleased()
                }

                // 下半弹簧
                Item { Layout.fillHeight: true }
            }

            // --- B. 定位控制面板（★ v3 重新设计：反馈式目标显示 + ⚙️设置 + GO居中放大） ---
            ColumnLayout {
                anchors.fill: parent
                spacing: 8 * Theme.scale
                visible: root.currentMode === 1

                // 顶部留空
                Item { Layout.preferredHeight: 4 * Theme.scale }

                // 定位速度设定
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 8 * Theme.scale

                    Text {
                        text: "定位速度:"
                        color: Theme.textDim
                        font.pixelSize: Theme.fontNormal
                        font.family: "Monospace"
                    }
                    Text {
                        text: viewModel ? viewModel.moveVelocity.toFixed(1) : "0.0"
                        color: Theme.colorIdle
                        font.pixelSize: Theme.fontNormal
                        font.bold: true
                        font.family: "Monospace"
                    }
                    Text {
                        text: "mm/s"
                        color: Theme.textDim
                        font.pixelSize: Theme.fontNormal
                        font.family: "Monospace"
                    }

                    IndustrialButton {
                        text: "⚙️"
                        buttonSize: 30 * Theme.scale
                        isCircle: true
                        baseColor: Theme.panelBg
                        enabled: root.isReadyForSetTarget
                        onClicked: {
                            moveVelocityNumPad.inputText = viewModel ? viewModel.moveVelocity.toString() : "0.00"
                            moveVelocityNumPad.open()
                        }
                    }
                }

                // 绝对/相对 单选
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 4 * Theme.scale

                    RadioButton {
                        text: "绝对"
                        checked: root.isAbsolute
                        enabled: root.isReadyForSetTarget
                        opacity: enabled ? 1.0 : 0.5
                        onClicked: root.isAbsolute = true
                        contentItem: Text {
                            text: parent.text
                            color: Theme.textMain
                            font.pixelSize: Theme.fontSmall
                            leftPadding: parent.indicator.width + 2
                        }
                    }

                    RadioButton {
                        text: "相对"
                        checked: !root.isAbsolute
                        enabled: root.isReadyForSetTarget
                        opacity: enabled ? 1.0 : 0.5
                        onClicked: root.isAbsolute = false
                        contentItem: Text {
                            text: parent.text
                            color: Theme.textMain
                            font.pixelSize: Theme.fontSmall
                            leftPadding: parent.indicator.width + 2
                        }
                    }
                }

                // ── ★ 绝对定位组 ──
                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 6 * Theme.scale
                    visible: root.isAbsolute

                    // 上半弹簧
                    Item { Layout.fillHeight: true }

                    // 反馈式目标显示 + ⚙️ 设置按钮
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 8 * Theme.scale

                        Text {
                            text: "目标:"
                            color: Theme.textDim
                            font.pixelSize: Theme.fontNormal
                            font.family: "Monospace"
                        }
                        Text {
                            text: viewModel ? viewModel.absMoveTarget.toFixed(1) : "0.0"
                            color: Theme.colorIdle
                            font.pixelSize: Theme.fontNormal
                            font.bold: true
                            font.family: "Monospace"
                        }
                        Text {
                            text: "mm"
                            color: Theme.textDim
                            font.pixelSize: Theme.fontNormal
                            font.family: "Monospace"
                        }

                        IndustrialButton {
                            text: "⚙️"
                            buttonSize: 30 * Theme.scale
                            isCircle: true
                            baseColor: Theme.panelBg
                            enabled: root.isReadyForSetTarget
                            onClicked: {
                                absTargetNumPad.inputText = viewModel ? viewModel.absMoveTarget.toFixed(2) : "0.00"
                                absTargetNumPad.open()
                            }
                        }
                    }

                    // ★ 触发绝对定位 GO（居中放大）
                    IndustrialButton {
                        Layout.alignment: Qt.AlignHCenter
                        text: root.isReadyForTrigger ? "绝对定位 GO" : (
                            viewModel && viewModel.isLoading ? "运行中..." : "不可用"
                        )
                        isCircle: false
                        buttonSize: 170 * Theme.scale
                        enabled: root.isReadyForTrigger
                        baseColor: root.isReadyForTrigger ? Theme.colorIdle : Theme.colorDisabled
                        onClicked: {
                            if (!root.isReadyForTrigger) return
                            if (viewModel) {
                                viewModel.triggerAbsMove()
                            }
                        }
                    }

                    // 下半弹簧
                    Item { Layout.fillHeight: true }
                }

                // ── ★ 相对定位组 ──
                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 6 * Theme.scale
                    visible: !root.isAbsolute

                    // 上半弹簧
                    Item { Layout.fillHeight: true }

                    // 反馈式目标显示 + ⚙️ 设置按钮
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 8 * Theme.scale

                        Text {
                            text: "距离:"
                            color: Theme.textDim
                            font.pixelSize: Theme.fontNormal
                            font.family: "Monospace"
                        }
                        Text {
                            text: viewModel ? viewModel.relMoveTarget.toFixed(1) : "0.0"
                            color: Theme.colorIdle
                            font.pixelSize: Theme.fontNormal
                            font.bold: true
                            font.family: "Monospace"
                        }
                        Text {
                            text: "mm"
                            color: Theme.textDim
                            font.pixelSize: Theme.fontNormal
                            font.family: "Monospace"
                        }

                        IndustrialButton {
                            text: "⚙️"
                            buttonSize: 30 * Theme.scale
                            isCircle: true
                            baseColor: Theme.panelBg
                            enabled: root.isReadyForSetTarget
                            onClicked: {
                                relTargetNumPad.inputText = viewModel ? viewModel.relMoveTarget.toFixed(2) : "0.00"
                                relTargetNumPad.open()
                            }
                        }
                    }

                    // ★ 触发相对定位 GO（居中放大）
                    IndustrialButton {
                        Layout.alignment: Qt.AlignHCenter
                        text: root.isReadyForTrigger ? "相对定位 GO" : (
                            viewModel && viewModel.isLoading ? "运行中..." : "不可用"
                        )
                        isCircle: false
                        buttonSize: 170 * Theme.scale
                        enabled: root.isReadyForTrigger
                        baseColor: root.isReadyForTrigger ? Theme.colorIdle : Theme.colorDisabled
                        onClicked: {
                            if (!root.isReadyForTrigger) return
                            if (viewModel) {
                                viewModel.triggerRelMove()
                            }
                        }
                    }

                    // 下半弹簧
                    Item { Layout.fillHeight: true }
                }

                // ── ★ Loading 状态指示（可选，调试用）──
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: viewModel ? viewModel.moveStep : ""
                    visible: viewModel && viewModel.isLoading
                    color: "gray"
                    font.pixelSize: Theme.fontSmall
                    font.family: "Monospace"
                }

                // 下半弹簧
                Item { Layout.fillHeight: true }
            }
        }

        // ==========================================
        // 3. 底部：紧急急停按钮
        // ==========================================
        IndustrialButton {
            id: emergencyStopButton
            isCircle: false
            buttonSize: 150 * Theme.scale
            Layout.alignment: Qt.AlignHCenter

            // ── 文字由急停状态决定 ──
            text: {
                if (!emergencyViewModel) return "急 停"
                if (emergencyViewModel.isNotSynchronized)    return "急 停"
                if (emergencyViewModel.isEmergencyStopped)   return "解除急停"
                if (emergencyViewModel.isTransitioning)      return emergencyViewModel.safetyStateText  // "急停处理中..." / "急停解除中..."
                return "急 停"
            }

            // ── 颜色由急停状态决定 ──
            // 工业惯例：急停按钮红色 #D32F2F，解除按钮橙红色 #FF5252
            baseColor: {
                if (!emergencyViewModel) return Theme.colorError
                if (emergencyViewModel.isNotSynchronized)    return Theme.colorDisabled
                if (emergencyViewModel.isEmergencyStopped)   return "#FF5252"   // 橙红色 -- 表示急停锁定中，点击解除
                if (emergencyViewModel.isTransitioning)      return Theme.colorDisabled
                return Theme.colorError  // Running -- 正常红色
            }

            activeColor: {
                if (!emergencyViewModel) return "#FF8A80"
                if (emergencyViewModel.isEmergencyStopped) return "#FF8A80"
                return "#FF8A80"
            }

            // ── 可点击性 ──
            // Running -> 可以按急停
            // EmergencyStopped -> 可以解除急停
            // 其他过渡态 -> 不可点击
            enabled: {
                if (!emergencyViewModel) return false
                if (emergencyViewModel.isNotSynchronized)    return false
                if (emergencyViewModel.isTransitioning)      return false
                return true  // Running 或 EmergencyStopped
            }

            onClicked: {
                if (!emergencyViewModel) return

                if (emergencyViewModel.isEmergencyStopped) {
                    // 当前已急停 -> 执行解除操作
                    console.log("EmergencyStopButton: 解除急停 -> releaseEmergencyStop()")
                    emergencyViewModel.releaseEmergencyStop()
                } else {
                    // 当前 Running -> 执行急停操作
                    console.log("EmergencyStopButton: 触发急停 -> triggerEmergencyStop()")
                    emergencyViewModel.triggerEmergencyStop()
                }
            }
        }
    }

    // 点动速度数字键盘（直接弹出，无二级嵌套）
    NumPad {
        id: jogVelocityNumPad
        title: "点动速度"
        unit: "mm/s"
        allowNegative: false
        allowDecimal: true
        maxValue: 1000.0
        maxDecimals: 2
        inputText: "0.00"
        onConfirmed: (value) => {
            if (root.viewModel) {
                root.viewModel.setJogVelocity(parseFloat(value))
            }
        }
    }

    // 定位速度数字键盘（直接弹出，无二级嵌套）
    NumPad {
        id: moveVelocityNumPad
        title: "定位速度"
        unit: "mm/s"
        allowNegative: false
        allowDecimal: true
        maxValue: 1000.0
        maxDecimals: 2
        inputText: "0.00"
        onConfirmed: (value) => {
            if (root.viewModel) {
                root.viewModel.setMoveVelocity(parseFloat(value))
            }
        }
    }

    // 绝对定位目标数字键盘（允许负数，直接弹出）
    NumPad {
        id: absTargetNumPad
        title: "绝对目标"
        unit: "mm"
        allowNegative: true
        allowDecimal: true
        maxValue: 10000.0
        maxDecimals: 2
        inputText: "0.00"
        onConfirmed: (value) => {
            if (root.viewModel) {
                root.viewModel.setAbsTarget(parseFloat(value))
            }
        }
    }

    // 相对定位目标数字键盘（允许负数，直接弹出）
    NumPad {
        id: relTargetNumPad
        title: "相对距离"
        unit: "mm"
        allowNegative: true
        allowDecimal: true
        maxValue: 10000.0
        maxDecimals: 2
        inputText: "0.00"
        onConfirmed: (value) => {
            if (root.viewModel) {
                root.viewModel.setRelTarget(parseFloat(value))
            }
        }
    }
}
