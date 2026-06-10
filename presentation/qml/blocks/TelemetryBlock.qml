import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import servoV6

Rectangle {
    id: root

    // === 核心接口：接收外部注入的 ViewModel ===
    property var viewModel: null
    property var emergencyViewModel: null
    property var gantryViewModel: null
    property var connectionViewModel: null
    property string selectedAxis: ""
    property string groupName: ""

    // 急停锁定状态
    readonly property bool locked: emergencyViewModel && emergencyViewModel.isSystemLocked

    // 龙门控制区是否可见（仅在选中 X 轴且有龙门 ViewModel 时显示）
    readonly property bool gantryAreaVisible: selectedAxis === "X" && gantryViewModel !== null

    // 龙门耦合状态快捷属性
    readonly property bool gantryCoupled: {
        if (!gantryViewModel) return false
        return gantryViewModel.isCoupled || false
    }
    readonly property bool gantryEnabled: {
        if (!gantryViewModel) return false
        return gantryViewModel.isEnabled || false
    }
    readonly property bool gantryCoupling: {
        if (!gantryViewModel) return false
        return gantryViewModel.isOrchestratorBusy || false
    }
    // 龙门是否已启用（使能 ON + 联动 ON）
    readonly property bool gantryActivated: root.gantryEnabled && root.gantryCoupled

    // 分组切换信号
    signal groupChanged(string newGroup)

    // 相对零点位置 = 绝对位置 - 相对位置（即设置零点时的绝对坐标）
    readonly property double relZeroPosition: {
        if (!viewModel) return 0.0
        return viewModel.absPos - viewModel.relPos
    }

    // 相对零点位置不为 0 时才展示清除行
    readonly property bool hasRelativeZero: Math.abs(root.relZeroPosition) > 0.0005

    // R轴（旋转轴）判定
    readonly property bool isRAxis: selectedAxis === "R"

    color: Theme.panelBg
    radius: 12 * Theme.scale
    border.color: Theme.borderMain
    border.width: 2 * Theme.scale

    // --- 状态颜色函数（保留用于指示灯）---
    function getStateColor(stateCode) {
        if (!viewModel) return Theme.colorDisabled;
        switch(stateCode) {
            case 1: return Theme.colorDisabled;   // Disabled
            case 2: return Theme.colorIdle;        // Idle / Standstill
            case 3:
            case 4: return Theme.colorMoving;      // Jogging / Moving
            case 6: return Theme.colorError;       // Error
            default: return Theme.textDim;
        }
    }


    // --- 上部分：可滚动内容区 ---
    Flickable {
        id: flickArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: root.isRAxis ? parent.bottom : limitBar.top
        anchors.leftMargin: 6 * Theme.scale
        anchors.rightMargin: 6 * Theme.scale
        anchors.topMargin: 6 * Theme.scale
        anchors.bottomMargin: 2 * Theme.scale
        contentWidth: width
        contentHeight: contentColumn.implicitHeight
        clip: true
        maximumFlickVelocity: 600 * Theme.scale
        flickDeceleration: 800 * Theme.scale

        ColumnLayout {
            id: contentColumn
            width: parent.width
            spacing: 6 * Theme.scale

        // ===== 1. 顶部标题栏（内置分组选择下拉框）=====
        RowLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter

            Item { Layout.fillWidth: true }

            Text {
                text: "组["
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
                font.bold: true
            }

            ComboBox {
                id: groupCombo
                model: ["Machine_A", "Machine_B"]
                currentIndex: root.groupName === "Machine_B" ? 1 : 0
                enabled: !root.locked
                opacity: enabled ? 1.0 : 0.4

                // 自定义样式适配工业深色主题
                background: Rectangle {
                    color: Theme.bgDark
                    border.color: Theme.borderMain
                    border.width: 1
                    radius: 4 * Theme.scale
                }
                contentItem: Text {
                    text: groupCombo.currentText
                    color: Theme.textMain
                    font.pixelSize: Theme.fontNormal
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onCurrentTextChanged: {
                    if (root.groupName !== currentText) {
                        root.groupChanged(currentText)
                    }
                }
            }

            Text {
                text: "] 运动数据看板"
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
                font.bold: true
            }

            Item { Layout.fillWidth: true }
        }

        // ===== 1.4 连接状态指示行（★ P1/P2 新增）=====
        // 显示 TCP 连接状态：指示灯（绿/红）+ 状态文本 + 重连按钮
        RowLayout {
            Layout.fillWidth: true
            spacing: 8 * Theme.scale
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 4 * Theme.scale
            Layout.bottomMargin: 2 * Theme.scale

            // 连接指示灯
            Rectangle {
                width: 12 * Theme.scale
                height: 12 * Theme.scale
                radius: width / 2
                color: connectionViewModel && connectionViewModel.connected
                       ? Theme.colorIdle : Theme.colorError
                border.color: Qt.lighter(color, 1.5)
                border.width: 1

                // 断连时闪烁动画
                SequentialAnimation on opacity {
                    running: connectionViewModel && !connectionViewModel.connected
                    loops: Animation.Infinite
                    NumberAnimation { from: 1.0; to: 0.2; duration: 500 }
                    NumberAnimation { from: 0.2; to: 1.0; duration: 500 }
                }
            }

            Text {
                text: "PLC连接状态：" + (connectionViewModel ? connectionViewModel.statusText : "未知")
                color: connectionViewModel && connectionViewModel.connected
                       ? Theme.colorIdle : Theme.colorError
                font.pixelSize: Theme.fontSmall
                font.bold: true
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                maximumLineCount: 2
            }

            // 重连按钮（仅在断连时可见）
            IndustrialButton {
                text: "\u27F3 重连"
                visible: connectionViewModel && !connectionViewModel.connected
                buttonSize: 60 * Theme.scale
                baseColor: root.locked ? Theme.colorDisabled : "#D84315"
                enabled: !root.locked
                opacity: enabled ? 1.0 : 0.4
                border.color: "#BF360C"
                border.width: 1
                onClicked: {
                    if (connectionViewModel) connectionViewModel.reconnect()
                }
            }
        }

        // ===== 1.5 龙门耦合控制区（仅在选中 X 轴时显示） =====
        Rectangle {
            Layout.fillWidth: true
            height: root.gantryAreaVisible ? 44 * Theme.scale : 0
            visible: root.gantryAreaVisible
            color: root.gantryCoupled ? "#1F2F1F" : "#1F1F2F"
            radius: 8 * Theme.scale
            border.color: root.gantryCoupled ? Theme.colorIdle : Theme.borderMain
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 6 * Theme.scale
                spacing: 8 * Theme.scale

                // 左侧：耦合状态指示灯 + 文本
                Rectangle {
                    width: 12 * Theme.scale
                    height: 12 * Theme.scale
                    radius: width / 2
                    color: {
                        if (root.gantryCoupling) return Theme.colorWarning
                        if (root.gantryCoupled) return Theme.colorIdle
                        return Theme.colorDisabled
                    }
                    border.color: Qt.lighter(color, 1.5)
                    border.width: 1
                    // 耦合过渡中闪烁
                    SequentialAnimation on opacity {
                        running: root.gantryCoupling
                        loops: Animation.Infinite
                        NumberAnimation { from: 1.0; to: 0.3; duration: 400 }
                        NumberAnimation { from: 0.3; to: 1.0; duration: 400 }
                    }
                }

                Text {
                    text: {
                        if (root.gantryCoupling) return "启用中..."
                        if (root.gantryActivated) return "已启用"
                        if (root.gantryCoupled) return "龙门已耦合"
                        return "未启用"
                    }
                    color: root.gantryActivated ? Theme.colorIdle : Theme.textDim
                    font.pixelSize: Theme.fontSmall
                    font.bold: true
                    Layout.fillWidth: true
                }

                // 右侧：启用 / 停用按钮
                IndustrialButton {
                    text: root.gantryActivated ? "停用" : "启用"
                    buttonSize: 60 * Theme.scale
                    baseColor: root.locked ? Theme.colorDisabled : (root.gantryActivated ? "#5D4037" : "#2E7D32")
                    enabled: !root.locked && !root.gantryCoupling
                    opacity: enabled ? 1.0 : 0.4
                    border.color: root.gantryActivated ? "#795548" : "#4CAF50"
                    border.width: 1
                    onClicked: {
                        if (!root.gantryViewModel) return
                        if (root.gantryActivated) {
                            root.gantryViewModel.stopCouplingAndDisable()
                        } else {
                            root.gantryViewModel.startCoupling()
                        }
                    }
                }
            }
        }

        // ===== 2. 运动状态行 =====
        RowLayout {
            Layout.fillWidth: true
            spacing: 12 * Theme.scale
            Layout.alignment: Qt.AlignHCenter

            // 运动状态指示灯 + 文本
            RowLayout {
                spacing: 4 * Theme.scale
                Rectangle {
                    width: 14 * Theme.scale
                    height: 14 * Theme.scale
                    radius: width / 2
                    color: getStateColor(viewModel ? viewModel.state : 0)
                    border.color: Qt.lighter(color, 1.5)
                    border.width: 1
                }
                Text {
                    text: "电机状态：" + (viewModel ? viewModel.stateText : "--")
                    color: getStateColor(viewModel ? viewModel.state : 0)
                    font.pixelSize: Theme.fontNormal
                    font.bold: true
                }
            }

            Item { Layout.fillWidth: true }
        }

        // 分隔线
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.borderMain
        }

        // ===== 3. 绝对位置行：靠左标签+数值 + 右侧清零按钮 =====
        RowLayout {
            Layout.fillWidth: true
            spacing: 8 * Theme.scale

            ColumnLayout {
                spacing: 1
                Layout.alignment: Qt.AlignLeft

                Text {
                    text: root.isRAxis ? "绝对位置 (°):" : "绝对位置 (mm):"
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }

                Text {
                    text: viewModel ? viewModel.absPos.toFixed(3) : "0.000"
                    color: Theme.textMain
                    font.pixelSize: Theme.fontLarge
                    font.family: "Monospace"
                    font.bold: true
                    fontSizeMode: Text.Fit
                    minimumPixelSize: Theme.fontSmall
                }
            }

            Item { Layout.fillWidth: true }

            // ⚡ 清零按钮暂时禁用：绝对位置清除零点操作存在问题，待修复后恢复
            // IndustrialButton {
            //     text: "⚡ 清零"
            //     buttonSize: 70 * Theme.scale
            //     baseColor: root.locked ? Theme.colorDisabled : Theme.panelBg
            //     enabled: !root.locked
            //     opacity: enabled ? 1.0 : 0.4
            //     border.color: Theme.borderMain
            //     border.width: 1
            //     Layout.alignment: Qt.AlignRight
            //     onClicked: {
            //         if (viewModel) viewModel.zeroAbsolutePosition()
            //     }
            // }
        }

        // ===== 4. 相对位置行：靠左标签+数值 + 右侧设零按钮 =====
        RowLayout {
            Layout.fillWidth: true
            spacing: 8 * Theme.scale

            ColumnLayout {
                spacing: 1
                Layout.alignment: Qt.AlignLeft

                Text {
                    text: root.isRAxis ? "相对位置 (°):" : "相对位置 (mm):"
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }

                Text {
                    text: {
                        if (!viewModel) return "0.000"
                        let r = viewModel.relPos
                        return (r >= 0 ? "+" : "") + r.toFixed(3)
                    }
                    color: Theme.textMain
                    font.pixelSize: Theme.fontLarge
                    font.family: "Monospace"
                }
            }

            Item { Layout.fillWidth: true }

            IndustrialButton {
                text: "⊙ 设零"
                buttonSize: 70 * Theme.scale
                baseColor: root.locked ? Theme.colorDisabled : Theme.panelBg
                enabled: !root.locked
                opacity: enabled ? 1.0 : 0.4
                border.color: Theme.borderMain
                border.width: 1
                Layout.alignment: Qt.AlignRight
                onClicked: {
                    if (viewModel) viewModel.setRelativeZero()
                }
            }
        }

        // ===== 5. 相对零点位置行（紧凑单行，仅在已设置时显示）=====
        RowLayout {
            Layout.fillWidth: true
            spacing: 6 * Theme.scale
            visible: root.hasRelativeZero

            Text {
                text: "零点: "
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
            }

            Text {
                text: {
                    if (!viewModel) return "0.000"
                    return root.relZeroPosition.toFixed(3)
                }
                color: Theme.colorWarning
                font.pixelSize: Theme.fontSmall
                font.family: "Monospace"
                Layout.alignment: Qt.AlignLeft
            }

            Item { Layout.fillWidth: true }

            IndustrialButton {
                text: "⊗ 清除"
                buttonSize: 65 * Theme.scale
                baseColor: root.locked ? Theme.colorDisabled : Theme.panelBg
                enabled: !root.locked
                opacity: enabled ? 1.0 : 0.4
                border.color: Theme.borderMain
                border.width: 1
                Layout.alignment: Qt.AlignRight
                onClicked: {
                    if (viewModel) viewModel.clearRelativeZero()
                }
            }
        }

        }  // end ColumnLayout
    }  // end Flickable

    // ===== 底部固定：限位进度条（R轴不展示）=====
    ColumnLayout {
        id: limitBar
        visible: !root.isRAxis
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 6 * Theme.scale
        anchors.rightMargin: 6 * Theme.scale
        anchors.bottomMargin: 3 * Theme.scale
        spacing: 3 * Theme.scale

        Rectangle {
            id: trackBar
            Layout.fillWidth: true
            height: 8 * Theme.scale
            radius: height / 2
            color: Theme.bgDark
            border.color: Theme.borderMain
            border.width: 1

            readonly property double safePos: viewModel ? viewModel.absPos : 0.0
            readonly property double safePLim: (viewModel && viewModel.posLimit < 999999) ? viewModel.posLimit : 1000.0
            readonly property double safeNLim: (viewModel && viewModel.negLimit > -999999) ? viewModel.negLimit : -1000.0

            readonly property double progressRatio: {
                let range = safePLim - safeNLim;
                if (range <= 0) return 0.5;
                return Math.max(0.0, Math.min(1.0, (safePos - safeNLim) / range));
            }

            Rectangle {
                width: parent.width * parent.progressRatio
                height: parent.height
                radius: parent.radius
                color: Theme.colorMoving
                Behavior on width {
                    NumberAnimation { duration: 100; easing.type: Easing.OutQuad }
                }
            }

            Rectangle {
                x: parent.width * parent.progressRatio - width / 2
                y: -4 * Theme.scale
                width: 4 * Theme.scale
                height: 16 * Theme.scale
                color: Theme.textMain
                radius: 2
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: viewModel && viewModel.negLimit > -999999 ? "负限位: " + viewModel.negLimit : "负限位: 未设"
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
            }
            Item { Layout.fillWidth: true }
            Text {
                text: viewModel && viewModel.posLimit < 999999 ? "正限位: " + viewModel.posLimit : "正限位: 未设"
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
            }
        }
    }
}
