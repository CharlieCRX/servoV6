import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import servoV6 // 引入 Theme

Popup {
    id: root
    
    // 接收外部传入的 ViewModel
    property var viewModel: null

    // 弹窗基础属性
    modal: true // 开启模态（阻挡底层点击）
    dim: true   // 开启背景变暗
    anchors.centerIn: Overlay.overlay // 确保弹窗在整个屏幕的正中央，而不是父组件中央
    
    width: 380 * Theme.scale
    height: 300 * Theme.scale
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside // 允许点击空白处关闭

    // 🌟 核心逻辑：每次打开弹窗时，从底层读取最新速度填充到输入框
    onOpened: {
        if (viewModel) {
            jogSpeedInput.text = viewModel.jogVelocity.toString()
            moveSpeedInput.text = viewModel.moveVelocity.toString()
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
            text: "⚙️ 轴运行速度设置"
            color: Theme.textMain
            font.pixelSize: Theme.fontLarge
            font.bold: true
            Layout.alignment: Qt.AlignHCenter
        }

        Item { Layout.fillHeight: true } // 弹簧

        // 点动速度输入组
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 15 * Theme.scale
            Text { text: "点动速度 (Jog):"; color: Theme.textDim; font.pixelSize: Theme.fontNormal }
            TextField {
                id: jogSpeedInput
                Layout.preferredWidth: 120 * Theme.scale
                color: Theme.textMain
                font.pixelSize: Theme.fontLarge
                horizontalAlignment: TextInput.AlignHCenter
                background: Rectangle { color: Theme.bgDark; border.color: Theme.borderMain; radius: 4 }
                validator: DoubleValidator { bottom: 0.1; top: 1000.0 }
            }
            Text { text: "mm/s"; color: Theme.textDim }
        }

        // 定位速度输入组
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 15 * Theme.scale
            Text { text: "定位速度 (Pos):"; color: Theme.textDim; font.pixelSize: Theme.fontNormal }
            TextField {
                id: moveSpeedInput
                Layout.preferredWidth: 120 * Theme.scale
                color: Theme.textMain
                font.pixelSize: Theme.fontLarge
                horizontalAlignment: TextInput.AlignHCenter
                background: Rectangle { color: Theme.bgDark; border.color: Theme.borderMain; radius: 4 }
                validator: DoubleValidator { bottom: 0.1; top: 1000.0 }
            }
            Text { text: "mm/s"; color: Theme.textDim }
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
                border.color: Theme.borderMain
                border.width: 1
                onClicked: root.close() // 直接关闭，不保存
            }

            IndustrialButton {
                Layout.fillWidth: true
                text: "保 存"
                baseColor: Theme.colorIdle // 绿色确认按钮
                onClicked: {
                    if (viewModel) {
                        // 下发新速度到 C++
                        viewModel.setJogVelocity(parseFloat(jogSpeedInput.text))
                        viewModel.setMoveVelocity(parseFloat(moveSpeedInput.text))
                    }
                    root.close() // 保存后关闭弹窗
                }
            }
        }
    }
}