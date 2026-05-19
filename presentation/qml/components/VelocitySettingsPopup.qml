import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import servoV6 // 引入 Theme

Popup {
    id: root
    
    // 接收外部传入的 ViewModel
    property var viewModel: null

    // ⭐ 速度类型选择："jog" = 点动速度, "move" = 定位速度
    property string speedType: "jog"

    // 弹窗基本属性
    modal: true
    dim: true
    anchors.centerIn: Overlay.overlay
    
    width: 380 * Theme.scale
    height: 280 * Theme.scale
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // 🌟 每次打开弹窗时，从底层读取最新速度填充到输入框
    onOpened: {
        if (viewModel) {
            if (speedType === "jog") {
                popupTitle.text = "⚙️ 点动速度设置"
                speedInput.text = viewModel.jogVelocity.toString()
                speedLabel.text = "点动速度 (Jog):"
            } else {
                popupTitle.text = "⚙️ 定位速度设置"
                speedInput.text = viewModel.moveVelocity.toString()
                speedLabel.text = "定位速度 (Pos):"
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
            text: "⚙️ 速度设置"
            color: Theme.textMain
            font.pixelSize: Theme.fontLarge
            font.bold: true
            Layout.alignment: Qt.AlignHCenter
        }

        Item { Layout.fillHeight: true } // 弹簧

        // 速度输入组
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 15 * Theme.scale
            Text {
                id: speedLabel
                text: "速度:"
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
            }
            TextField {
                id: speedInput
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
                onClicked: root.close()
            }

            IndustrialButton {
                Layout.fillWidth: true
                text: "保 存"
                baseColor: Theme.colorIdle
                onClicked: {
                    if (viewModel) {
                        var value = parseFloat(speedInput.text)
                        if (speedType === "jog") {
                            viewModel.setJogVelocity(value)
                        } else {
                            viewModel.setMoveVelocity(value)
                        }
                    }
                    root.close()
                }
            }
        }
    }
}
