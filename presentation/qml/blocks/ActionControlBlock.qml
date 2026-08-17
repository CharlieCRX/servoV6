import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import servoV6

Rectangle {
    id: root
    property var viewModel: null
    property var emergencyViewModel: null  // 急停安全 ViewModel
    property var gantryViewModel: null     // 龙门 ViewModel
    property var snapshotAdapter: controlSnapshot
    property var commandAdapter: controlCommand
    property string groupLetter: "A"
    property string currentAxis: ""        // 当前选中的轴名（用于龙门逻辑判断）

    readonly property int snapshotRevision: snapshotAdapter ? snapshotAdapter.revision : 0
    readonly property var vAxis: {
        root.snapshotRevision
        return snapshotAdapter ? snapshotAdapter.axisFor(groupLetter, currentAxis) : ({})
    }
    readonly property bool vnextActive: viewModel === null
                                        && snapshotAdapter
                                        && commandAdapter
                                        && commandAdapter.available
    readonly property bool vnextControlAxis: currentAxis === "Y" || currentAxis === "Z"
                                             || currentAxis === "R" || currentAxis === "X"
                                             || currentAxis === "X1" || currentAxis === "X2"
    readonly property bool vnextCanControl: vnextActive && vnextControlAxis
                                            && snapshotAdapter.connected
                                            && snapshotAdapter.safetyTrusted
                                            && !snapshotAdapter.emergencyStop
                                            && !snapshotAdapter.globallyLocked
                                            && vAxis.bound && vAxis.trusted
                                            && vAxis.hmiVisible && !vAxis.leased
    readonly property int effectiveState: viewModel ? viewModel.state : (vAxis.motionState ?? 0)
    readonly property double effectiveJogVelocity: viewModel ? viewModel.jogVelocity : (vAxis.manualSpeed ?? 0.0)
    readonly property double effectiveMoveVelocity: viewModel ? viewModel.moveVelocity : (vAxis.positioningSpeed ?? 0.0)
    readonly property double effectiveAbsTarget: viewModel ? viewModel.absMoveTarget : (vAxis.absMoveTarget ?? 0.0)
    readonly property double effectiveRelTarget: viewModel ? viewModel.relMoveTarget : (vAxis.relMoveTarget ?? 0.0)
    readonly property bool effectiveLoading: viewModel ? viewModel.isLoading : (vAxis.leased ?? false)
    readonly property bool effectiveBlockingError: viewModel ? viewModel.hasBlockingError : false

    // ★ 绑定到 C++ MotionController，摇杆操作根据此模式自动分发到 JOG 或 Position
    // 内部状态：0 = 点动模式 (Jog), 1 = 定位模式 (Position)
    property int currentMode: motionController ? motionController.controlMode : 0
    // 定位模式下的子状态：true = 绝对, false = 相对
    property bool isAbsolute: motionController ? motionController.isAbsolute : true

    // ── 系统锁定 = 安全锁定 + 轴本身不可用 ──
    property bool systemLocked: {
        if (emergencyViewModel && emergencyViewModel.isSystemLocked) return true
        if (root.vnextActive && snapshotAdapter && snapshotAdapter.globallyLocked) return true
        return false
    }

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

    property bool jogEnabled: !systemLocked && !gantryOperationLocked
                              && (viewModel !== null || root.vnextCanControl)

    // R轴（旋转轴）判定
    readonly property bool isRAxis: currentAxis === "R"

    // ★ 定位模式下触发是否就绪：仅 Modal 错误阻断操作
    property bool isReadyForTrigger: !systemLocked && !gantryOperationLocked
        && (viewModel ? (!viewModel.hasBlockingError && viewModel.state <= 2 && !viewModel.isLoading)
                      : root.vnextCanControl)

    // ★ 设置目标是否就绪：仅 Modal 错误阻断操作
    property bool isReadyForSetTarget: !systemLocked && !gantryOperationLocked
        && (viewModel ? (!viewModel.hasBlockingError && viewModel.state <= 2)
                      : root.vnextCanControl)

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
        // 0.5 龙门操作锁定横幅
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
                anchors.margins: 2 * Theme.scale
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.currentMode === 0 ? "#1A3A2A" : "transparent"
                    radius: 6 * Theme.scale
                    border.color: root.currentMode === 0 ? Theme.colorIdle : "transparent"
                    border.width: root.currentMode === 0 ? 1.5 * Theme.scale : 0
                    Text {
                        anchors.centerIn: parent
                        text: "点动"
                        color: root.currentMode === 0 ? Theme.colorIdle : Theme.textDim
                        font.bold: root.currentMode === 0
                        font.pixelSize: Theme.fontSmall
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: !systemLocked
                        onClicked: {
                            if (motionController) motionController.controlMode = 0
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.currentMode === 1 ? "#1A2A3A" : "transparent"
                    radius: 6 * Theme.scale
                    border.color: root.currentMode === 1 ? Theme.colorMoving : "transparent"
                    border.width: root.currentMode === 1 ? 1.5 * Theme.scale : 0
                    Text {
                        anchors.centerIn: parent
                        text: "定位"
                        color: root.currentMode === 1 ? Theme.colorMoving : Theme.textDim
                        font.bold: root.currentMode === 1
                        font.pixelSize: Theme.fontSmall
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: !systemLocked
                        onClicked: {
                            if (motionController) motionController.controlMode = 1
                        }
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
                        text: root.effectiveJogVelocity.toFixed(1)
                        color: Theme.colorIdle
                        font.pixelSize: Theme.fontNormal
                        font.bold: true
                        font.family: "Monospace"
                    }
                    Text {
                        text: root.isRAxis ? "°/s" : "mm/s"
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
                            jogVelocityNumPad.inputText = root.effectiveJogVelocity.toString()
                            jogVelocityNumPad.open()
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                IndustrialButton {
                    id: jogPositiveButton
                    text: "前进 +"
                    isCircle: false
                    buttonSize: 170 * Theme.scale
                    Layout.alignment: Qt.AlignHCenter
                    // ★ 按钮互斥：前进活跃时不允许后退操作，反之亦然。
                    // 关键：自身方向活跃时按钮必须保持 enabled（不被禁用），否则 Qt 会把按下的
                    // 鼠标触发 onCanceled -> 立刻 StopJog，导致“按住无法持续点动”。
                    enabled: (motionController && motionController.jogActiveDirection === 1)
                             || (root.jogEnabled && (motionController ? motionController.jogActiveDirection !== -1 : true))
                    isActive: motionController ? motionController.jogActiveDirection === 1 : false
                    onPressed: {
                        if(motionController && root.jogEnabled) motionController.jogActiveDirection = 1
                    }
                    onReleased: {
                        // 松开/取消必须无条件复位（复位是安全兜底，不能因 jogEnabled 在点动期间
                        // 因轴占用变 false 而被吞掉），否则点动停不下来、按钮卡在按下态。
                        if(motionController) motionController.jogActiveDirection = 0
                    }
                }

                Item { Layout.preferredHeight: 8 * Theme.scale }

                IndustrialButton {
                    id: jogNegativeButton
                    text: "后退 -"
                    isCircle: false
                    buttonSize: 170 * Theme.scale
                    Layout.alignment: Qt.AlignHCenter
                    enabled: (motionController && motionController.jogActiveDirection === -1)
                             || (root.jogEnabled && (motionController ? motionController.jogActiveDirection !== 1 : true))
                    isActive: motionController ? motionController.jogActiveDirection === -1 : false
                    onPressed: {
                        if(motionController && root.jogEnabled) motionController.jogActiveDirection = -1
                    }
                    onReleased: {
                        // 松开/取消必须无条件复位（复位是安全兜底，不能因 jogEnabled 在点动期间
                        // 因轴占用变 false 而被吞掉），否则点动停不下来、按钮卡在按下态。
                        if(motionController) motionController.jogActiveDirection = 0
                    }
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
                        text: root.effectiveMoveVelocity.toFixed(1)
                        color: Theme.colorIdle
                        font.pixelSize: Theme.fontNormal
                        font.bold: true
                        font.family: "Monospace"
                    }
                    Text {
                        text: root.isRAxis ? "°/s" : "mm/s"
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
                            moveVelocityNumPad.inputText = root.effectiveMoveVelocity.toString()
                            moveVelocityNumPad.open()
                        }
                    }
                }

                // ── ★ 绝对目标设置行（紧接定位速度下方）──
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 8 * Theme.scale
                    visible: root.isAbsolute

                    Text {
                        text: "目标:"
                        color: Theme.textDim
                        font.pixelSize: Theme.fontNormal
                        font.family: "Monospace"
                    }
                    Text {
                        text: root.effectiveAbsTarget.toFixed(1)
                        color: Theme.colorIdle
                        font.pixelSize: Theme.fontNormal
                        font.bold: true
                        font.family: "Monospace"
                    }
                    Text {
                        text: root.isRAxis ? "°" : "mm"
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
                            absTargetNumPad.inputText = root.effectiveAbsTarget.toFixed(2)
                            absTargetNumPad.open()
                        }
                    }
                }

                // ── ★ 相对距离设置行（紧接定位速度下方）──
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 8 * Theme.scale
                    visible: !root.isAbsolute

                    Text {
                        text: "距离:"
                        color: Theme.textDim
                        font.pixelSize: Theme.fontNormal
                        font.family: "Monospace"
                    }
                    Text {
                        text: root.effectiveRelTarget.toFixed(1)
                        color: Theme.colorIdle
                        font.pixelSize: Theme.fontNormal
                        font.bold: true
                        font.family: "Monospace"
                    }
                    Text {
                        text: root.isRAxis ? "°" : "mm"
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
                            relTargetNumPad.inputText = root.effectiveRelTarget.toFixed(2)
                            relTargetNumPad.open()
                        }
                    }
                }

                // 绝对/相对 单选切换器（在目标设置行下方）
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
                        anchors.margins: 2 * Theme.scale
                        spacing: 0

                        // 绝对选项
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: root.isAbsolute ? "#1A3A2A" : "transparent"
                            radius: 6 * Theme.scale
                            border.color: root.isAbsolute ? Theme.colorIdle : "transparent"
                            border.width: root.isAbsolute ? 1.5 * Theme.scale : 0
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
                                onClicked: {
                                    if (motionController) motionController.isAbsolute = true
                                }
                            }
                        }

                        // 相对选项
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: !root.isAbsolute ? "#1A2A3A" : "transparent"
                            radius: 6 * Theme.scale
                            border.color: !root.isAbsolute ? Theme.colorMoving : "transparent"
                            border.width: !root.isAbsolute ? 1.5 * Theme.scale : 0
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
                                onClicked: {
                                    if (motionController) motionController.isAbsolute = false
                                }
                            }
                        }
                    }
                }

                // ── 弹性空间：将参数设置区与执行按钮区分开 ──
                Item { Layout.fillHeight: true }

                // ── ★ 绝对定位 GO 按钮（底部，远离参数设置区）──
                IndustrialButton {
                    Layout.alignment: Qt.AlignHCenter
                    visible: root.isAbsolute
                    text: root.isReadyForTrigger ? "绝对定位" : (
                        root.effectiveLoading ? "运行中..." : "不可用"
                    )
                    isCircle: false
                    buttonSize: 170 * Theme.scale
                    enabled: root.isReadyForTrigger
                    baseColor: root.isReadyForTrigger ? Theme.colorIdle : Theme.colorDisabled
                    onClicked: {
                        if (!root.isReadyForTrigger) return
                        if (viewModel) {
                            viewModel.triggerAbsMove()
                        } else if (root.vnextCanControl) {
                            commandAdapter.startAbsMove(root.groupLetter, root.currentAxis,
                                                        root.effectiveAbsTarget,
                                                        root.effectiveMoveVelocity)
                        }
                    }
                }

                // ── ★ 相对定位 GO 按钮（底部，远离参数设置区）──
                IndustrialButton {
                    Layout.alignment: Qt.AlignHCenter
                    visible: !root.isAbsolute
                    text: root.isReadyForTrigger ? "相对定位" : (
                        root.effectiveLoading ? "运行中..." : "不可用"
                    )
                    isCircle: false
                    buttonSize: 170 * Theme.scale
                    enabled: root.isReadyForTrigger
                    baseColor: root.isReadyForTrigger ? Theme.colorIdle : Theme.colorDisabled
                    onClicked: {
                        if (!root.isReadyForTrigger) return
                        if (viewModel) {
                            viewModel.triggerRelMove()
                        } else if (root.vnextCanControl) {
                            commandAdapter.startRelMove(root.groupLetter, root.currentAxis,
                                                        root.effectiveRelTarget,
                                                        root.effectiveMoveVelocity)
                        }
                    }
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: viewModel ? viewModel.moveStep : ""
                    visible: false
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
                if (!emergencyViewModel && root.vnextActive) {
                    return snapshotAdapter.emergencyStop ? "解除急停" : "急 停"
                }
                if (!emergencyViewModel) return "急 停"
                if (emergencyViewModel.isNotSynchronized)    return "急 停"
                if (emergencyViewModel.isEmergencyStopped)   return "解除急停"
                if (emergencyViewModel.isTransitioning)      return emergencyViewModel.safetyStateText
                return "急 停"
            }

            baseColor: {
                if (!emergencyViewModel && root.vnextActive) {
                    return snapshotAdapter.emergencyStop ? "#FF5252" : Theme.colorError
                }
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
                if (!emergencyViewModel && root.vnextActive) return true
                if (!emergencyViewModel) return false
                if (emergencyViewModel.isNotSynchronized)    return false
                if (emergencyViewModel.isTransitioning)      return false
                return true
            }

            onClicked: {
                if (!emergencyViewModel && root.vnextActive) {
                    if (snapshotAdapter.emergencyStop) {
                        commandAdapter.requestEmergencyStopRelease()
                    } else {
                        commandAdapter.triggerEmergencyStop()
                    }
                    return
                }
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
        unit: root.isRAxis ? "°/s" : "mm/s"
        allowNegative: false
        allowDecimal: true
        maxValue: 1000.0
        maxDecimals: 2
        inputText: "0.00"
        onConfirmed: (value) => {
            if (root.viewModel) {
                root.viewModel.setJogVelocity(parseFloat(value))
            } else if (root.vnextCanControl) {
                root.commandAdapter.setManualSpeed(root.groupLetter, root.currentAxis, parseFloat(value))
            }
        }
    }

    // ── 定位速度数字键盘 ──
    NumPad {
        id: moveVelocityNumPad
        title: "定位速度"
        unit: root.isRAxis ? "°/s" : "mm/s"
        allowNegative: false
        allowDecimal: true
        maxValue: 1000.0
        maxDecimals: 2
        inputText: "0.00"
        onConfirmed: (value) => {
            if (root.viewModel) {
                root.viewModel.setMoveVelocity(parseFloat(value))
            } else if (root.vnextCanControl) {
                root.commandAdapter.setPositioningSpeed(root.groupLetter, root.currentAxis, parseFloat(value))
            }
        }
    }

    // ── 绝对定位目标数字键盘 ──
    NumPad {
        id: absTargetNumPad
        title: "绝对目标"
        unit: root.isRAxis ? "°" : "mm"
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
            } else if (root.vnextCanControl) {
                root.commandAdapter.setAbsTarget(root.groupLetter, root.currentAxis, parseFloat(value))
            }
        }
    }

    // ── 相对定位目标数字键盘 ──
    NumPad {
        id: relTargetNumPad
        title: "相对距离"
        unit: root.isRAxis ? "°" : "mm"
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
            } else if (root.vnextCanControl) {
                root.commandAdapter.setRelTarget(root.groupLetter, root.currentAxis, parseFloat(value))
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
