import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    width: 400
    height: 300
    visible: true
    title: "蓝牙继电器控制器"

    // 访问 C++ 对象的别名
    // RelayController 的实例在 main.cpp 中被注册为 "relayCtrl"
    readonly property var controller: relayCtrl

    // 状态变量
    property bool isConnected: false
    property string statusText: "未连接"
    property string errorText: ""

    // 监听 C++ 对象发出的信号
    Connections {
        target: controller
        function onPortOpened(success) {
            isConnected = success;
            if (success) {
                statusText = "已连接到蓝牙设备";
            } else {
                statusText = "连接失败";
            }
        }
        function onErrorOccurred(msg) {
            errorText = msg;
            console.log("错误信息: " + msg);
        }
    }

    ColumnLayout {
        id: mainLayout
        anchors.fill: parent
        anchors.margins: 20
        spacing: 10

        Label {
            text: "蓝牙连接状态:"
            font.pixelSize: 16
        }

        Label {
            text: statusText
            font.pixelSize: 20
            font.bold: true
            color: isConnected ? "green" : "red"
        }

        Button {
            text: isConnected ? "断开连接" : "连接蓝牙设备"
            Layout.fillWidth: true
            onClicked: {
                if (isConnected) {
                    controller.closePort();
                    isConnected = false;
                    statusText = "已断开连接";
                } else {
                    // 调用 openPort 方法，测试使用默认的 MAC 地址
                    controller.openPort("A0:DD:6C:02:06:AE");
                    statusText = "正在尝试连接...";
                }
            }
        }

        Label {
            text: "继电器控制:"
            font.pixelSize: 16
            Layout.topMargin: 20
        }

        // 继电器通道控制按钮
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Button {
                text: "打开通道 1"
                Layout.fillWidth: true
                enabled: isConnected
                onClicked: {
                    controller.openChannel(1);
                }
            }

            Button {
                text: "关闭通道 1"
                Layout.fillWidth: true
                enabled: isConnected
                onClicked: {
                    controller.closeChannel(1);
                }
            }
        }
    }
}
