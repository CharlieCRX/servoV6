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

    // 🌟 每次打开弹窗时，从底层读取最新目标值填充到输入框（只读显示）
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

        Item { Layout.fillHeight: true }

        // 目标显示组（只读，值来自反馈）
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
                readOnly: true
                background: Rectangle { color: Theme.bgDark; border.color: Theme.borderMain; radius: 4 }
            }
            Text { text: "mm"; color: Theme.textDim }
        }

        Item { Layout.fillHeight: true }

        // 底部按钮区（仅关闭）
        RowLayout {
            Layout.fillWidth: true
            spacing: 20 * Theme.scale

            IndustrialButton {
                Layout.fillWidth: true
                text: "关 闭"
                baseColor: Theme.colorIdle
                onClicked: root.close()
            }
        }
    }
}
