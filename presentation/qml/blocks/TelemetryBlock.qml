import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import servoV6

Rectangle {
    id: root

    // === 核心接口：接收外部注入的 ViewModel ===
    property var viewModel: null
    property var emergencyViewModel: null
    property string groupName: ""

    // 急停锁定状态
    readonly property bool locked: emergencyViewModel && emergencyViewModel.isSystemLocked

    // 分组切换信号
    signal groupChanged(string newGroup)

    // 相对零点位置 = 绝对位置 - 相对位置（即设置零点时的绝对坐标）
    readonly property double relZeroPosition: {
        if (!viewModel) return 0.0
        return viewModel.absPos - viewModel.relPos
    }

    // 相对零点位置不为 0 时才展示清除行
    readonly property bool hasRelativeZero: Math.abs(root.relZeroPosition) > 0.0005

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

    // --- UI 布局 (Flickable 包裹以适配小屏幕) ---
    Flickable {
        anchors.fill: parent
        anchors.margins: 6 * Theme.scale
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

        // ===== 2. 使能状态 + 运动状态行 =====
        RowLayout {
            Layout.fillWidth: true
            spacing: 12 * Theme.scale
            Layout.alignment: Qt.AlignHCenter

            // 使能状态
            RowLayout {
                spacing: 4 * Theme.scale
                Rectangle {
                    width: 10 * Theme.scale
                    height: 10 * Theme.scale
                    radius: width / 2
                    color: viewModel && viewModel.isEnabled ? Theme.colorIdle : Theme.colorDisabled
                }
                Text {
                    text: viewModel && viewModel.isEnabled ? "已使能" : "未使能"
                    color: viewModel && viewModel.isEnabled ? Theme.colorIdle : Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }
            }

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
                    text: viewModel ? viewModel.stateText : "--"
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
                    text: "绝对位置 (mm):"
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

            IndustrialButton {
                text: "⚡ 清零"
                buttonSize: 70 * Theme.scale
                baseColor: root.locked ? Theme.colorDisabled : Theme.panelBg
                enabled: !root.locked
                opacity: enabled ? 1.0 : 0.4
                border.color: Theme.borderMain
                border.width: 1
                Layout.alignment: Qt.AlignRight
                onClicked: {
                    if (viewModel) viewModel.zeroAbsolutePosition()
                }
            }
        }

        // ===== 4. 相对位置行：靠左标签+数值 + 右侧设零按钮 =====
        RowLayout {
            Layout.fillWidth: true
            spacing: 8 * Theme.scale

            ColumnLayout {
                spacing: 1
                Layout.alignment: Qt.AlignLeft

                Text {
                    text: "相对位置 (mm):"
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

        // ===== 6. 限位进度条 =====
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 8 * Theme.scale
            spacing: 5 * Theme.scale

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

        }  // end ColumnLayout
    }  // end Flickable
}
