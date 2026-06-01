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

    // 弹窗基本属性
    modal: true
    dim: true
    anchors.centerIn: Overlay.overlay

    width: 380 * Theme.scale
    height: 280 * Theme.scale
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // 🌟 每次打开弹窗时，从底层读取最新目标值（反馈值）填充到输入框
    onOpened: {
        if (viewModel) {
            if (targetType === "abs") {
                popupTitle.text = "⚙️ 绝对目标设置"
                targetLabel.text = "绝对目标 (Abs):"
                targetInput.text = viewModel.absMoveTarget.toFixed(2)
            } else {
                popupTitle.text = "⚙️ 相对距离设置"
                targetLabel.text = "相对距离 (Rel):"
                targetInput.text = viewModel.relMoveTarget.toFixed(2)
            }
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

        Item { Layout.fillHeight: true } // 弹簧

        // 目标输入组（可编辑，初始值来自反馈）
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 15 * Theme.scale
            Text {
                id: targetLabel
                text: "目标:"
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
            }
            TextField {
                id: targetInput
                Layout.preferredWidth: 120 * Theme.scale
                color: Theme.textMain
                font.pixelSize: Theme.fontLarge
                horizontalAlignment: TextInput.AlignHCenter
                background: Rectangle { color: Theme.bgDark; border.color: Theme.borderMain; radius: 4 }
                validator: DoubleValidator { bottom: 0.1; top: 10000.0 }
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
                        var value = parseFloat(targetInput.text)
                        if (targetType === "abs") {
                            viewModel.setAbsTarget(value)
                        } else {
                            viewModel.setRelTarget(value)
                        }
                    }
                    root.close()
                }
            }
        }
    }
}
