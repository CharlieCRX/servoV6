import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import servoV6

Window {
    id: mainWindow
    width: 1280
    height: 720
    visible: true
    title: qsTr("servoV6 - UI Integration Test")
    color: Theme.bgDark

    // 🌟 核心：判定当前是否为小屏幕（移动端环境）
    readonly property bool isMobile: mainWindow.width < 900

    // 🌟 核心：根据屏幕尺寸动态计算边距和间距
    readonly property int dynamicMargin: isMobile ? 10 * Theme.scale : 40 * Theme.scale
    readonly property int dynamicSpacing: isMobile ? 10 * Theme.scale : 40 * Theme.scale

    RowLayout {
        anchors.fill: parent
        anchors.margins: dynamicMargin
        spacing: dynamicSpacing

        // 1. 左侧：轴选择与状态概览
        AxisSelectorBlock {
            // 小屏幕时适当压窄
            Layout.preferredWidth: isMobile ? 180 * Theme.scale : 260 * Theme.scale 
            Layout.fillHeight: true
            onAxisChanged: (name) => {
                console.log("切换到轴:", name)
            }
        }

        // 2. 中央遥测看板
        TelemetryBlock {
            Layout.fillWidth: true
            Layout.fillHeight: true
            viewModel: axisX1VM 
        }

        // 3. 右侧多功能控制面板
        ActionControlBlock {
            // 小屏幕时适当压窄
            Layout.preferredWidth: isMobile ? 220 * Theme.scale : 300 * Theme.scale
            Layout.fillHeight: true
            viewModel: axisX1VM
        }
    }
}