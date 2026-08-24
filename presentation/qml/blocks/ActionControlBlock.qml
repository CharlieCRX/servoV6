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
                                        && !!snapshotAdapter
                                        && !!commandAdapter
                                        && !!commandAdapter.available
    readonly property bool vnextControlAxis: currentAxis === "Y" || currentAxis === "Z"
                                             || currentAxis === "R" || currentAxis === "X"
                                             || currentAxis === "X1" || currentAxis === "X2"

    // ★ PLC 实时运动状态回显（依据快照 motionState = PLC 反馈 D128）：
    //   0=未使能 1=轴控开电机关 2=电机空闲 3=正向点动 4=反向点动 5=绝对定位中 6=相对定位中。
    //   这些是「PLC 侧真实状态」，无论运动由谁发起（UI/摇杆/UDP/PLC 本地点动），
    //   UI 都据此回显按钮按下态并在运动期间锁定普通操作，实现 UI 状态与 PLC 统一。
    readonly property bool vJogForwardActive:  root.vnextActive && (root.vAxis.motionState ?? 0) === 3
    readonly property bool vJogBackwardActive: root.vnextActive && (root.vAxis.motionState ?? 0) === 4
    readonly property bool vPositioningActive: root.vnextActive
                                               && ((root.vAxis.motionState ?? 0) === 5
                                                   || (root.vAxis.motionState ?? 0) === 6)
    // 轴正在运动（点动或定位中）：期间普通操作一律锁定，仅停止/急停可用。
    readonly property bool vMoving: root.vnextActive && (root.vAxis.motionState ?? 0) >= 3

    readonly property bool vnextCanControl: vnextActive && vnextControlAxis
                                            && !!snapshotAdapter.connected
                                            && !!snapshotAdapter.safetyTrusted
                                            && !snapshotAdapter.emergencyStop
                                            && !snapshotAdapter.globallyLocked
                                            && !!vAxis.bound && !!vAxis.trusted
                                            && !!vAxis.hmiVisible && !vAxis.leased
                                            && !root.vMoving          // PLC 正在移动 → 锁定普通控制
                                            && !root.alarmActive
    // ★ 当前轴是否有告警（D160+slot 位集合非零：JogPolicy 会以 "axis alarm" 拒绝运动）
    readonly property bool alarmActive: root.vnextActive && (root.vAxis.alarmWord ?? 0) > 0
    // 当前告警是否已弹窗提示（避免每次快照刷新重复弹出）
    property bool alarmDialogShown: false
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
        && !root.limitLocked

    // ★ 设置目标是否就绪：仅 Modal 错误阻断操作
    property bool isReadyForSetTarget: !systemLocked && !gantryOperationLocked
        && (viewModel ? (!viewModel.hasBlockingError && viewModel.state <= 2)
                      : root.vnextCanControl)
        && !root.limitLocked

    // ★ 到达任意限位（motionLimit != 0）：禁止定位（位置移动），仅允许点动撤离。
    readonly property bool limitLocked: root.vnextActive && (root.vAxis.motionLimit ?? 0) > 0

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
        // 0.4 轴告警横幅（alarmWord != 0：JogPolicy 会拒绝运动）
        // ==========================================
        Rectangle {
            Layout.fillWidth: true
            height: root.alarmActive ? 36 * Theme.scale : 0
            visible: root.alarmActive
            color: "#B71C1C"
            radius: 4 * Theme.scale

            Text {
                anchors.centerIn: parent
                text: "⚠️ 轴告警 (0x" + (root.vAxis.alarmWord ?? 0).toString(16).toUpperCase()
                      + ")  已锁定运动，请确认后清除"
                color: "#FFFFFF"
                font.pixelSize: Theme.fontSmall
                font.bold: true
                font.family: "Monospace"
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    alarmDialog.alarmCodeText = (root.vAxis.alarmWord ?? 0)
                    alarmDialog.axisName = root.groupLetter + "." + root.currentAxis
                    alarmDialog.open()
                }
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
            opacity: (systemLocked || root.limitLocked) ? 0.4 : 1.0

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
                    // ★ 到达限位：锁定定位模式，显示小锁标志。
                    Text {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 4 * Theme.scale
                        text: "🔒"
                        visible: root.limitLocked
                        color: Theme.colorWarning
                        font.pixelSize: Theme.fontSmall
                        font.bold: true
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: !systemLocked && !root.limitLocked
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
                    // ★ PLC 状态回显：无论点动由谁发起（UI/摇杆/UDP/PLC 本地点动），
                    //   只要 PLC 反馈正在正向点动(motionState==3)，前进按钮即点亮为"按下"视觉。
                    isActive: (motionController ? motionController.jogActiveDirection === 1 : false)
                              || root.vJogForwardActive
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
                    // ★ PLC 状态回显：只要 PLC 反馈正在反向点动(motionState==4)，后退按钮即点亮。
                    isActive: (motionController ? motionController.jogActiveDirection === -1 : false)
                              || root.vJogBackwardActive
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
                        (root.effectiveLoading || root.vPositioningActive) ? "运行中..." : "不可用"
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
                        (root.effectiveLoading || root.vPositioningActive) ? "运行中..." : "不可用"
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

    // ── ★ 轴告警确认弹窗：告警时提示并请求用户确认清理告警码 ──
    Dialog {
        id: alarmDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 380 * Theme.scale
        height: 300 * Theme.scale
        title: "⚠️ 轴告警"

        property string axisName: ""
        property int alarmCodeText: 0

        background: Rectangle {
            color: Theme.panelBg
            radius: 10 * Theme.scale
            border.color: "#B71C1C"
            border.width: 2 * Theme.scale
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20 * Theme.scale
            spacing: 12 * Theme.scale

            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "轴 " + alarmDialog.axisName + " 检测到告警码 0x"
                      + alarmDialog.alarmCodeText.toString(16).toUpperCase()
                      + "。告警会阻止所有运动（点动/定位），"
                      + "请确认排除故障后清除告警码。"
                color: Theme.textMain
                font.pixelSize: Theme.fontNormal
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "⚠️ 清除告警码 = PLC 写 M(208+槽位) ON（PLC 自复位）。"
                      + "仅在确认故障已排除后操作。"
                color: Theme.colorWarning
                font.pixelSize: Theme.fontSmall
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 12 * Theme.scale

                IndustrialButton {
                    text: "取 消"
                    baseColor: Theme.colorDisabled
                    onClicked: alarmDialog.close()
                }

                IndustrialButton {
                    text: "确认清除告警"
                    baseColor: "#B71C1C"
                    onClicked: {
                        if (root.commandAdapter) {
                            root.commandAdapter.clearAlarmWord(root.groupLetter, root.currentAxis)
                        }
                        alarmDialog.close()
                    }
                }
            }
        }
    }

    // ── ★ 告警自动弹出检测：告警出现时自动弹窗（每次只弹一次，清除后重置）──
    Connections {
        target: snapshotAdapter
        function onStateChanged() {
            if (!root.vnextActive) { root.alarmDialogShown = false; return }
            if (root.alarmActive) {
                if (!root.alarmDialogShown) {
                    root.alarmDialogShown = true
                    alarmDialog.alarmCodeText = root.vAxis.alarmWord ?? 0
                    alarmDialog.axisName = root.groupLetter + "." + root.currentAxis
                    alarmDialog.open()
                }
            } else {
                // 告警已清除：复位弹窗状态，下次告警可重新弹出
                root.alarmDialogShown = false
            }
        }
    }
}
