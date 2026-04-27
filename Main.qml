import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import servoV6

Window {
    width: 1280
    height: 720
    visible: true
    title: qsTr("servoV6 - UI Integration Test")
    color: Theme.bgDark

    // 使用 Layout 进行响应式布局，方便后续横向排版
    RowLayout {
        anchors.fill: parent
        anchors.margins: 40 * Theme.scale
        spacing: 40 * Theme.scale

        // 🌟 1. 左侧：轴选择与状态概览
        AxisSelectorBlock {
            Layout.preferredWidth: 260 * Theme.scale // 稍微加宽一点
            Layout.fillHeight: true
            onAxisChanged: (name) => {
                console.log("切换到轴:", name)
                // 这里以后可以根据 name 切换不同的 ViewModel 实例
            }
        }

        // 🌟 挂载中央遥测看板
        TelemetryBlock {
            Layout.fillWidth: true
            Layout.fillHeight: true
            // 核心操作：把 C++ 的 axisX1VM 注入到看板里
            viewModel: axisX1VM 
        }

        // 🌟 右侧多功能控制面板
        ActionControlBlock {
            Layout.preferredWidth: 300 * Theme.scale
            Layout.fillHeight: true
            viewModel: axisX1VM
        }
    }
}