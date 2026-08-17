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

    /// @brief JOG 点动活跃时阻止轴切换（只读状态，供 UI disable 绑定）
    readonly property bool jogAxisSwitchLocked: motionController ? motionController.jogActiveDirection !== 0 : false

    // ★ 监听 C++ AxisSelectionModel，摇杆切换轴时同步更新 UI
    Connections {
        target: axisSelectionModel
        function onCurrentAxisChanged(axisId) {
            // AxisId enum: Y=0, Z=1, R=2, X=3
            var map = { 0: "Y", 1: "Z", 2: "R", 3: "X" };
            var newAxis = map[axisId] || "Y";
            console.log("[QML] axisSelectionModel.currentAxisChanged  axisId=" + axisId + " → " + newAxis);
            currentAxis = newAxis;
        }
    }

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

    // 根据当前分组动态绑定龙门 ViewModel
    property var currentGantryViewModel: {
        if (currentGroup === "Machine_A") return gantryVM_A;
        if (currentGroup === "Machine_B") return gantryVM_B;
        return gantryVM_A; // fallback
    }

    // ★ P1/P2 新增：根据当前分组动态绑定连接状态 ViewModel
    property var currentConnectionViewModel: {
        if (currentGroup === "Machine_A") return connectionVM_A;
        if (currentGroup === "Machine_B") return connectionVM_B;
        return connectionVM_A; // fallback
    }

    // ===== 垂直布局：分组选择栏 + 三栏 + 底部错误栏 =====
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: dynamicMargin
        spacing: dynamicSpacing / 2

        // ===== 三栏 RowLayout =====
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: dynamicSpacing

            // 1. 左侧：轴选择与状态概览
            AxisSelectorBlock {
                Layout.preferredWidth: isMobile ? 180 * Theme.scale : 260 * Theme.scale
                Layout.fillHeight: true
                currentAxisName: mainWindow.currentAxis   // ★ 反向同步：摇杆切换时高亮对应轴
                emergencyViewModel: currentEmergencyViewModel
                gantryViewModel: currentGantryViewModel
                jogAxisSwitchLocked: mainWindow.jogAxisSwitchLocked  // ★ JOG 点动时禁用轴切换
                onAxisChanged: (axisName) => {
                    currentAxis = axisName;
                    // ★ 通知 C++ AxisSelectionModel，使 MotionController 的 m_currentAxis 同步
                    if (axisSelectionModel) {
                        axisSelectionModel.setCurrentAxisByName(axisName);
                    }
                    console.log("[QML] 切换到组:", currentGroup, ", 轴:", axisName);
                }
            }

            // 2. 中央遥测看板（内置分组选择）
            TelemetryBlock {
                Layout.fillWidth: true
                Layout.fillHeight: true
                viewModel: currentViewModel
                emergencyViewModel: currentEmergencyViewModel
                gantryViewModel: currentGantryViewModel
                connectionViewModel: currentConnectionViewModel
                selectedAxis: currentAxis
                groupName: currentGroup
                onGroupChanged: (newGroup) => {
                    currentGroup = newGroup;
                }
            }

            // 3. 右侧多功能控制面板
            ActionControlBlock {
                Layout.preferredWidth: isMobile ? 220 * Theme.scale : 300 * Theme.scale
                Layout.fillHeight: true
                viewModel: currentViewModel
                emergencyViewModel: currentEmergencyViewModel
                gantryViewModel: currentGantryViewModel
                groupLetter: currentGroup === "Machine_A" ? "A" : "B"
                currentAxis: mainWindow.currentAxis
            }
        }

        // ===== 4. 底部错误信息栏 =====
        ErrorPanelBlock {
            Layout.fillWidth: true
            viewModel: currentViewModel
        }

    }
}
