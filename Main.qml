import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
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

    // ===== 分组与轴选择 =====
    property string currentGroup: "Machine_A"
    property string currentAxis: "Y"

    // 根据当前选择动态绑定 ViewModel
    property var currentViewModel: {
        if (currentGroup === "Machine_A") {
            if (currentAxis === "Y")  return group_A_Y;
            if (currentAxis === "Z")  return group_A_Z;
            if (currentAxis === "R")  return group_A_R;
            if (currentAxis === "X")  return group_A_X;
            if (currentAxis === "X1") return group_A_X1;
            if (currentAxis === "X2") return group_A_X2;
        } else if (currentGroup === "Machine_B") {
            if (currentAxis === "Y")  return group_B_Y;
            if (currentAxis === "Z")  return group_B_Z;
            if (currentAxis === "R")  return group_B_R;
            if (currentAxis === "X")  return group_B_X;
            if (currentAxis === "X1") return group_B_X1;
            if (currentAxis === "X2") return group_B_X2;
        }
        return group_A_Y; // fallback
    }

    // 根据当前分组动态绑定急停 ViewModel
    property var currentEmergencyViewModel: {
        if (currentGroup === "Machine_A") return emergencyVM_A;
        if (currentGroup === "Machine_B") return emergencyVM_B;
        return emergencyVM_A; // fallback
    }

    // ===== 垂直布局：分组选择栏 + 三栏 + 底部错误栏 =====
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: dynamicMargin
        spacing: dynamicSpacing / 2

        // ===== 0. 顶部分组选择栏 =====
        RowLayout {
            Layout.fillWidth: true
            spacing: 10 * Theme.scale

            Text {
                text: "分组:"
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
            }

            ComboBox {
                id: groupSelector
                model: ["Machine_A", "Machine_B"]
                currentIndex: 0
                onCurrentTextChanged: {
                    currentGroup = currentText;
                    // 切换分组时，默认选择该组的第一个轴
                    if (currentGroup === "Machine_A") {
                        currentAxis = "Y";
                    } else {
                        currentAxis = "X1";
                    }
                }
            }
        }

        // ===== 三栏 RowLayout =====
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: dynamicSpacing

            // 1. 左侧：轴选择与状态概览
            AxisSelectorBlock {
                Layout.preferredWidth: isMobile ? 180 * Theme.scale : 260 * Theme.scale
                Layout.fillHeight: true
                onAxisChanged: (axisName) => {
                    currentAxis = axisName;
                    console.log("切换到组:", currentGroup, ", 轴:", axisName);
                }
            }

            // 2. 中央遥测看板
            TelemetryBlock {
                Layout.fillWidth: true
                Layout.fillHeight: true
                viewModel: currentViewModel
            }

            // 3. 右侧多功能控制面板
            ActionControlBlock {
                Layout.preferredWidth: isMobile ? 220 * Theme.scale : 300 * Theme.scale
                Layout.fillHeight: true
                viewModel: currentViewModel
                emergencyViewModel: currentEmergencyViewModel
            }
        }

        // ===== 4. 底部错误信息栏 =====
        ErrorPanelBlock {
            Layout.fillWidth: true
            viewModel: currentViewModel
        }
    }
}
