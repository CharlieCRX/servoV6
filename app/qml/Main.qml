import QtQuick
import QtQuick.Controls

ApplicationWindow {
    visible: true
    width: 400
    height: 200
    title: qsTr("继电器控制")

    Column {
        spacing: 20
        padding: 20

        TextField {
            id: portInput
            placeholderText: "输入串口号 (如 COM17)"
            width: parent.width * 0.8
        }

        Button {
            text: "打开串口"
            onClicked: {
                relayCtrl.openPort(portInput.text)
            }
        }

        Row {
            spacing: 20

            Button {
                id: relayBtn0
                text: "继电器通道1"
                checkable: true
                onPressed: {
                    relayCtrl.openChannel(0)
                }
                onReleased: {
                    relayCtrl.closeChannel(0)
                }
            }

            Button {
                id: relayBtn1
                text: "继电器通道2"
                checkable: true
                onPressed: {
                    relayCtrl.openChannel(1)
                }
                onReleased: {
                    relayCtrl.closeChannel(1)
                }
            }

            Button {
                id: relayBtn2
                text: "继电器通道3"
                checkable: true
                onPressed: {
                    relayCtrl.openChannel(2)
                }
                onReleased: {
                    relayCtrl.closeChannel(2)
                }
            }

            // 可根据需要，多个按钮对应多个通道
        }

        Text {
            id: statusText
            color: "red"
        }
    }

    Connections {
        target: relayCtrl
        onPortOpened: {
            if (portOpened) {
                statusText.text = "串口打开成功"
            } else {
                statusText.text = "串口打开失败"
            }
        }
        onErrorOccurred: {
            statusText.text = errorOccurred
        }
    }
}
