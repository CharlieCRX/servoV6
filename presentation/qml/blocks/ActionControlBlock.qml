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
                        text: "点动速度: " + (viewModel ? viewModel.jogVelocity.toFixed(1) : "0.0") + " mm/s"
                        color: Theme.textMain
                        font.pixelSize: Theme.fontNormal
                        font.family: "Monospace"
                    }

                    IndustrialButton {
                        text: "⚙️"
                        buttonSize: 30 * Theme.scale
                        isCircle: true
                        baseColor: Theme.panelBg
                        enabled: root.jogEnabled
                        onClicked: jogVelocityPopup.open()
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

            // --- B. 定位控制面板（★ v2 重新设计：独立按钮映射） ---
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
                        text: "定位速度: " + (viewModel ? viewModel.moveVelocity.toFixed(1) : "0.0") + " mm/s"
                        color: Theme.textMain
                        font.pixelSize: Theme.fontNormal
                        font.family: "Monospace"
                    }

                    IndustrialButton {
                        text: "⚙️"
                        buttonSize: 30 * Theme.scale
                        isCircle: true
                        baseColor: Theme.panelBg
                        enabled: root.isReadyForSetTarget
                        onClicked: moveVelocityPopup.open()
                    }
                }

                // 绝对/相对 单选（紧凑，紧贴速度行）
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

                // 上半弹簧
                Item { Layout.fillHeight: true }

                // ── ★ 绝对定位组 ──
                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 6 * Theme.scale
                    visible: root.isAbsolute

                    // 目标值输入
                    TextField {
                        id: absTargetInput
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 140 * Theme.scale
                        text: "100.0"
                        enabled: root.isReadyForSetTarget
                        opacity: enabled ? 1.0 : 0.5
                        font.pixelSize: Theme.fontLarge
                        font.family: "Monospace"
                        color: Theme.textMain
                        horizontalAlignment: TextInput.AlignHCenter
                        background: Rectangle {
                            color: Theme.bgDark
                            border.color: absTargetInput.activeFocus ? Theme.colorMoving : Theme.borderMain
                            border.width: 2
                            radius: 6 * Theme.scale
                        }
                        validator: DoubleValidator { bottom: -9999.9; top: 9999.9; decimals: 2 }
                    }

                    // 按钮组：设置目标 + 触发移动
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 8 * Theme.scale

                        // ★ 按钮 1：设置绝对目标
                        IndustrialButton {
                            text: root.isReadyForSetTarget ? "设置目标" : "不可用"
                            isCircle: false
                            buttonSize: 110 * Theme.scale
                            enabled: root.isReadyForSetTarget
                            baseColor: root.isReadyForSetTarget ? Theme.panelBg : Theme.colorDisabled
                            onClicked: {
                                if (!root.isReadyForSetTarget) return
                                let target = parseFloat(absTargetInput.text)
                                if (!isNaN(target) && viewModel) {
                                    viewModel.setAbsTarget(target)
                                }
                            }
                        }

                        // ★ 按钮 2：触发绝对定位
                        IndustrialButton {
                            text: root.isReadyForTrigger ? "绝对定位 GO" : (
                                viewModel && viewModel.isLoading ? "运行中..." : "不可用"
                            )
                            isCircle: false
                            buttonSize: 110 * Theme.scale
                            enabled: root.isReadyForTrigger
                            baseColor: root.isReadyForTrigger ? Theme.colorIdle : Theme.colorDisabled
                            onClicked: {
                                if (!root.isReadyForTrigger) return
                                if (viewModel) {
                                    viewModel.triggerAbsMove()
                                }
                            }
                        }
                    }
                }

                // ── ★ 相对定位组 ──
                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 6 * Theme.scale
                    visible: !root.isAbsolute

                    // 距离值输入
                    TextField {
                        id: relTargetInput
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 140 * Theme.scale
                        text: "50.0"
                        enabled: root.isReadyForSetTarget
                        opacity: enabled ? 1.0 : 0.5
                        font.pixelSize: Theme.fontLarge
                        font.family: "Monospace"
                        color: Theme.textMain
                        horizontalAlignment: TextInput.AlignHCenter
                        background: Rectangle {
                            color: Theme.bgDark
                            border.color: relTargetInput.activeFocus ? Theme.colorMoving : Theme.borderMain
                            border.width: 2
                            radius: 6 * Theme.scale
                        }
                        validator: DoubleValidator { bottom: -9999.9; top: 9999.9; decimals: 2 }
                    }

                    // 按钮组：设置距离 + 触发移动
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 8 * Theme.scale

                        // ★ 按钮 1：设置相对距离
                        IndustrialButton {
                            text: root.isReadyForSetTarget ? "设置距离" : "不可用"
                            isCircle: false
                            buttonSize: 110 * Theme.scale
                            enabled: root.isReadyForSetTarget
                            baseColor: root.isReadyForSetTarget ? Theme.panelBg : Theme.colorDisabled
                            onClicked: {
                                if (!root.isReadyForSetTarget) return
                                let distance = parseFloat(relTargetInput.text)
                                if (!isNaN(distance) && viewModel) {
                                    viewModel.setRelTarget(distance)
                                }
                            }
                        }

                        // ★ 按钮 2：触发相对定位
                        IndustrialButton {
                            text: root.isReadyForTrigger ? "相对定位 GO" : (
                                viewModel && viewModel.isLoading ? "运行中..." : "不可用"
                            )
                            isCircle: false
                            buttonSize: 110 * Theme.scale
                            enabled: root.isReadyForTrigger
                            baseColor: root.isReadyForTrigger ? Theme.colorIdle : Theme.colorDisabled
                            onClicked: {
                                if (!root.isReadyForTrigger) return
                                if (viewModel) {
                                    viewModel.triggerRelMove()
                                }
                            }
                        }
                    }
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

    // Jog 模式下使用的点动速度弹窗
    VelocitySettingsPopup {
        id: jogVelocityPopup
        viewModel: root.viewModel
        speedType: "jog"
    }

    // POS 模式下使用的定位速度弹窗
    VelocitySettingsPopup {
        id: moveVelocityPopup
        viewModel: root.viewModel
        speedType: "move"
    }
}
