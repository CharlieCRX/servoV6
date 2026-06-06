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
    property bool systemLocked: {
        if (emergencyViewModel && emergencyViewModel.isSystemLocked) return true
        return false
    }

    // ── 龙门操作锁定（已移除：联动已由 AxisViewModelCore 的 GantryMotionOrchestrator 自动编排）──
    // X 轴选中时不再需要手动启用；X1/X2 在龙门耦合时仍然锁定
    readonly property bool gantryOperationLocked: {
        if (!gantryViewModel) return false
        if ((currentAxis === "X1" || currentAxis === "X2") && gantryViewModel.isCoupled) return true
        return false
    }

    readonly property string gantryLockReason: {
        if (!gantryOperationLocked) return ""
        if ((currentAxis === "X1" || currentAxis === "X2") && gantryViewModel && gantryViewModel.isCoupled)
            return "受龙门控制"
        return ""
    }

    property bool jogEnabled: !systemLocked && !gantryOperationLocked && viewModel !== null

    // ★ 定位模式下触发是否就绪：仅 Modal 错误阻断操作
    property bool isReadyForTrigger: !systemLocked && !gantryOperationLocked && viewModel ?
        (!viewModel.hasBlockingError && viewModel.state <= 2 && !viewModel.isLoading) : false

    // ★ 设置目标是否就绪：仅 Modal 错误阻断操作
    property bool isReadyForSetTarget: !systemLocked && !gantryOperationLocked && viewModel ?
        (!viewModel.hasBlockingError && viewModel.state <= 2) : false

        color: "transparent"

    ColumnLayout {
        anchors.fill: parent
        spacing: 6 * Theme.scale

        // ==========================================
        // 0. 紧急急停状态横幅
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
        // 0.5 龙门操作锁定横幅（仅 X1/X2 受龙门控制时显示）
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

                Item { Layout.preferredHeight: 4 * Theme.scale }

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

                Item { Layout.fillHeight: true }

                IndustrialButton {
                    text: "JOG +"
                    isCircle: false
                    buttonSize: 170 * Theme.scale
                    Layout.alignment: Qt.AlignHCenter
                    enabled: root.jogEnabled
                    onPressed: if(viewModel && root.jogEnabled) viewModel.jogPositivePressed()
                    onReleased: if(viewModel && root.jogEnabled) viewModel.jogPositiveReleased()
                }

                Item { Layout.preferredHeight: 8 * Theme.scale }

                IndustrialButton {
                    text: "JOG -"
                    isCircle: false
                    buttonSize: 170 * Theme.scale
                    Layout.alignment: Qt.AlignHCenter
                    enabled: root.jogEnabled
                    onPressed: if(viewModel && root.jogEnabled) viewModel.jogNegativePressed()
                    onReleased: if(viewModel && root.jogEnabled) viewModel.jogNegativeReleased()
                }

                Item { Layout.fillHeight: true }
            }

            // --- B. 定位控制面板 ---
            ColumnLayout {
                anchors.fill: parent
                spacing: 8 * Theme.scale
                visible: root.currentMode === 1

                Item { Layout.preferredHeight: 4 * Theme.scale }

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

                // 绝对/相对 单选切换器
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 200 * Theme.scale
                    Layout.preferredHeight: 36 * Theme.scale
                    radius: 8 * Theme.scale
                    color: Theme.bgDark
                    border.color: Theme.borderMain
                    border.width: 1
                    opacity: root.isReadyForSetTarget ? 1.0 : 0.4

                    RowLayout {
                        anchors.fill: parent
                        spacing: 0

                        // 绝对选项
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: root.isAbsolute ? Theme.panelBg : "transparent"
                            radius: 8 * Theme.scale
                            clip: true

                            RowLayout {
                                anchors.centerIn: parent
                                spacing: 6 * Theme.scale

                                // 指示器圆点
                                Rectangle {
                                    width: 10 * Theme.scale
                                    height: 10 * Theme.scale
                                    radius: 5 * Theme.scale
                                    color: root.isAbsolute ? Theme.colorIdle : Theme.colorDisabled
                                }

                                Text {
                                    text: "绝对"
                                    color: root.isAbsolute ? Theme.colorIdle : Theme.textDim
                                    font.bold: root.isAbsolute
                                    font.pixelSize: Theme.fontSmall
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                enabled: root.isReadyForSetTarget
                                onClicked: root.isAbsolute = true
                            }
                        }

                        // 相对选项
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: !root.isAbsolute ? Theme.panelBg : "transparent"
                            radius: 8 * Theme.scale
                            clip: true

                            RowLayout {
                                anchors.centerIn: parent
                                spacing: 6 * Theme.scale

                                // 指示器圆点
                                Rectangle {
                                    width: 10 * Theme.scale
                                    height: 10 * Theme.scale
                                    radius: 5 * Theme.scale
                                    color: !root.isAbsolute ? Theme.colorMoving : Theme.colorDisabled
                                }

                                Text {
                                    text: "相对"
                                    color: !root.isAbsolute ? Theme.colorMoving : Theme.textDim
                                    font.bold: !root.isAbsolute
                                    font.pixelSize: Theme.fontSmall
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                enabled: root.isReadyForSetTarget
                                onClicked: root.isAbsolute = false
                            }
                        }
                    }
                }

                // ── ★ 绝对定位组 ──
                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 6 * Theme.scale
                    visible: root.isAbsolute

                    Item { Layout.fillHeight: true }

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

                    Item { Layout.fillHeight: true }
                }

                // ── ★ 相对定位组 ──
                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 6 * Theme.scale
                    visible: !root.isAbsolute

                    Item { Layout.fillHeight: true }

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

                    Item { Layout.fillHeight: true }
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: viewModel ? viewModel.moveStep : ""
                    visible: viewModel && viewModel.isLoading
                    color: "gray"
                    font.pixelSize: Theme.fontSmall
                    font.family: "Monospace"
                }

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

            text: {
                if (!emergencyViewModel) return "急 停"
                if (emergencyViewModel.isNotSynchronized)    return "急 停"
                if (emergencyViewModel.isEmergencyStopped)   return "解除急停"
                if (emergencyViewModel.isTransitioning)      return emergencyViewModel.safetyStateText
                return "急 停"
            }

            baseColor: {
                if (!emergencyViewModel) return Theme.colorError
                if (emergencyViewModel.isNotSynchronized)    return Theme.colorDisabled
                if (emergencyViewModel.isEmergencyStopped)   return "#FF5252"
                if (emergencyViewModel.isTransitioning)      return Theme.colorDisabled
                return Theme.colorError
            }

            activeColor: {
                if (!emergencyViewModel) return "#FF8A80"
                if (emergencyViewModel.isEmergencyStopped) return "#FF8A80"
                return "#FF8A80"
            }

            enabled: {
                if (!emergencyViewModel) return false
                if (emergencyViewModel.isNotSynchronized)    return false
                if (emergencyViewModel.isTransitioning)      return false
                return true
            }

            onClicked: {
                if (!emergencyViewModel) return

                if (emergencyViewModel.isEmergencyStopped) {
                    console.log("EmergencyStopButton: 解除急停 -> releaseEmergencyStop()")
                    emergencyViewModel.releaseEmergencyStop()
                } else {
                    console.log("EmergencyStopButton: 触发急停 -> triggerEmergencyStop()")
                    emergencyViewModel.triggerEmergencyStop()
                }
            }
        }
    }

    // ── 点动速度数字键盘 ──
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

    // ── 定位速度数字键盘 ──
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

    // ── 绝对定位目标数字键盘 ──
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
                var ok = root.viewModel.setAbsTarget(parseFloat(value))
                if (!ok) {
                    // ★ 设置被后端拒绝（如超限位），弹出错误提示
                    var errMsg = root.viewModel.errorMessage || "设置失败"
                    absTargetErrorDialog.errorText = errMsg
                    absTargetErrorDialog.open()
                }
            }
        }
    }

    // ── 相对定位目标数字键盘 ──
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
                var ok = root.viewModel.setRelTarget(parseFloat(value))
                if (!ok) {
                    // ★ 设置被后端拒绝（如超限位），弹出错误提示
                    var errMsg = root.viewModel.errorMessage || "设置失败"
                    relTargetErrorDialog.errorText = errMsg
                    relTargetErrorDialog.open()
                }
            }
        }
    }

    // ── ★ 错误提示弹窗（绝对目标 NumPad 被后端拒绝时弹出）──
    Dialog {
        id: absTargetErrorDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 340 * Theme.scale
        height: 220 * Theme.scale
        title: "⚠️ 绝对目标设置失败"

        property string errorText: ""

        background: Rectangle {
            color: Theme.panelBg
            radius: 10 * Theme.scale
            border.color: Theme.borderMain
            border.width: 2 * Theme.scale
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20 * Theme.scale
            spacing: 15 * Theme.scale

            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: absTargetErrorDialog.errorText
                color: Theme.textMain
                font.pixelSize: Theme.fontNormal
            }

            IndustrialButton {
                Layout.alignment: Qt.AlignHCenter
                text: "关 闭"
                baseColor: Theme.colorIdle
                onClicked: absTargetErrorDialog.close()
            }
        }
    }

    // ── ★ 错误提示弹窗（相对目标 NumPad 被后端拒绝时弹出）──
    Dialog {
        id: relTargetErrorDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 340 * Theme.scale
        height: 220 * Theme.scale
        title: "⚠️ 相对目标设置失败"

        property string errorText: ""

        background: Rectangle {
            color: Theme.panelBg
            radius: 10 * Theme.scale
            border.color: Theme.borderMain
            border.width: 2 * Theme.scale
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20 * Theme.scale
            spacing: 15 * Theme.scale

            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: relTargetErrorDialog.errorText
                color: Theme.textMain
                font.pixelSize: Theme.fontNormal
            }

            IndustrialButton {
                Layout.alignment: Qt.AlignHCenter
                text: "关 闭"
                baseColor: Theme.colorIdle
                onClicked: relTargetErrorDialog.close()
            }
        }
    }
}