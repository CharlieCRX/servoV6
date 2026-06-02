import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import servoV6

Popup {
    id: root

    // 接收外部传入的 ViewModel
    property var viewModel: null

    // ⭐ 目标类型选择："abs" = 绝对目标, "rel" = 相对距离
    property string targetType: "abs"

    // ⭐ 正负号：true = +, false = -
    property bool signPositive: true

    // 弹窗基本属性
    modal: true
    dim: true
    anchors.centerIn: Overlay.overlay

    width: 380 * Theme.scale
    height: 320 * Theme.scale
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // 🌟 每次打开弹窗时，从底层读取最新目标值（反馈值）填充到输入框
    onOpened: {
        if (viewModel) {
            var currentValue = 0.0
            if (targetType === "abs") {
                popupTitle.text = "⚙️ 绝对目标设置"
                targetLabel.text = "绝对目标 (Abs):"
                currentValue = viewModel.absMoveTarget
            } else {
                popupTitle.text = "⚙️ 相对距离设置"
                targetLabel.text = "相对距离 (Rel):"
                currentValue = viewModel.relMoveTarget
            }
            // 根据当前值自动设置正负号和输入框
            signPositive = (currentValue >= 0.0)
            numPad.inputText = Math.abs(currentValue).toFixed(2)
        }
    }

    // 弹窗背景样式
    background: Rectangle {
        color: Theme.panelBg
        radius: 12 * Theme.scale
        border.color: Theme.borderMain
        border.width: 2 * Theme.scale
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 25 * Theme.scale
        spacing: 20 * Theme.scale

        Text {
            id: popupTitle
            text: "⚙️ 目标设置"
            color: Theme.textMain
            font.pixelSize: Theme.fontLarge
            font.bold: true
            Layout.alignment: Qt.AlignHCenter
        }

        // ⭐ 正负号切换
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 4 * Theme.scale

            Text {
                text: "方向:"
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
            }

            Rectangle {
                Layout.preferredWidth: 50 * Theme.scale
                Layout.preferredHeight: 32 * Theme.scale
                radius: 6 * Theme.scale
                color: root.signPositive ? Theme.colorIdle : Theme.panelBg
                border.color: Theme.borderMain
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "+"
                    color: root.signPositive ? "#FFFFFF" : Theme.textMain
                    font.pixelSize: Theme.fontLarge
                    font.bold: true
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.signPositive = true
                }
            }

            Rectangle {
                Layout.preferredWidth: 50 * Theme.scale
                Layout.preferredHeight: 32 * Theme.scale
                radius: 6 * Theme.scale
                color: !root.signPositive ? Theme.colorIdle : Theme.panelBg
                border.color: Theme.borderMain
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "−"
                    color: !root.signPositive ? "#FFFFFF" : Theme.textMain
                    font.pixelSize: Theme.fontLarge
                    font.bold: true
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.signPositive = false
                }
            }
        }

        // 目标输入组（伪输入框：点击弹出 NumPad，彻底阻止安卓系统键盘）
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 15 * Theme.scale
            Text {
                id: targetLabel
                text: "目标:"
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
            }
            // 伪输入框：Rectangle + Text + MouseArea
            Rectangle {
                id: targetInputBox
                Layout.preferredWidth: 120 * Theme.scale
                Layout.preferredHeight: 40 * Theme.scale
                radius: 4 * Theme.scale
                color: Theme.bgDark
                border.color: Theme.borderMain
                border.width: 1.5 * Theme.scale

                Text {
                    anchors.centerIn: parent
                    text: numPad.inputText
                    color: Theme.textMain
                    font.pixelSize: Theme.fontLarge
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: numPad.open()
                }
            }
            Text { text: "mm"; color: Theme.textDim }
        }

        Item { Layout.fillHeight: true } // 弹簧

        // 底部按钮区
        RowLayout {
            Layout.fillWidth: true
            spacing: 20 * Theme.scale

            IndustrialButton {
                Layout.fillWidth: true
                text: "取 消"
                baseColor: "transparent"
                onClicked: root.close()
            }

            IndustrialButton {
                Layout.fillWidth: true
                text: "保 存"
                baseColor: Theme.colorIdle
                onClicked: {
                    if (viewModel) {
                        var absValue = Math.abs(parseFloat(numPad.inputText))
                        var value = root.signPositive ? absValue : -absValue
                        var ok = false
                        if (targetType === "abs") {
                            ok = viewModel.setAbsTarget(value)
                        } else {
                            ok = viewModel.setRelTarget(value)
                        }
                        if (!ok) {
                            // ★ 设置被后端拒绝（如超限位），弹出错误提示，弹窗不关闭
                            var errMsg = viewModel.errorMessage || "设置失败"
                            errorDialog.errorText = errMsg
                            errorDialog.open()
                            return
                        }
                    }
                    root.close()
                }
            }
        }
    }

    // ── NumPad 自定义数字键盘 ──
    NumPad {
        id: numPad
        title: targetType === "abs" ? "绝对目标" : "相对距离"
        unit: "mm"
        allowNegative: false   // 正负号由外部按钮控制
        allowDecimal: true
        maxValue: 10000.0
        maxDecimals: 2
        inputText: "0.00"
    }

    // ── ★ 错误提示弹窗（Modal 错误时弹出，告知用户具体错误信息）──
    Dialog {
        id: errorDialog
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 340 * Theme.scale
        height: 220 * Theme.scale
        title: "⚠️ 设置失败"

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
                text: errorDialog.errorText
                color: Theme.textMain
                font.pixelSize: Theme.fontNormal
            }

            IndustrialButton {
                Layout.alignment: Qt.AlignHCenter
                text: "关 闭"
                baseColor: Theme.colorIdle
                onClicked: errorDialog.close()
            }
        }
    }
}
