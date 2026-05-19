import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import servoV6

Rectangle {
    id: root
    
    // === 核心接口 ===
    property string currentAxisName: "Y" // 默认选中 Y 轴
    property var emergencyViewModel: null
    property var gantryViewModel: null
    signal axisChanged(string axisName)  // 切换轴时发出的信号

    // 急停锁定状态
    readonly property bool locked: emergencyViewModel && emergencyViewModel.isSystemLocked

    // ── 龙门耦合状态快捷属性（从 GantryViewModel 投影） ──
    // isCoupled: 龙门已耦合（允许逻辑 X 轴运动）
    readonly property bool isCoupled: {
        if (!gantryViewModel) return false
        return gantryViewModel.isCoupled || false
    }
    // isCouplingTransition: 耦合过渡中（orchestrator 忙碌 = 解耦/耦合进行中）
    readonly property bool isCouplingTransition: {
        if (!gantryViewModel) return false
        return gantryViewModel.isOrchestratorBusy || false
    }

    // ── X 轴（逻辑龙门）的状态文字 ──
    readonly property string gantryStatusText: {
        if (!gantryViewModel || currentAxisName !== "X") return ""
        if (isCouplingTransition) return "耦合中..."
        if (isCoupled) return "耦合 · 控制中"
        return "已解耦"
    }

    // ── X 轴状态指示灯颜色 ──
    readonly property string gantryStatusColor: {
        if (!gantryViewModel) return Theme.textDim
        if (isCouplingTransition) return Theme.colorWarning
        if (isCoupled) return Theme.colorIdle
        return Theme.textDim
    }

    // ── X1/X2 物理轴是否可独立操作（仅当龙门已解耦时） ──
    readonly property bool physicalAxesAvailable: !gantryViewModel || !gantryViewModel.isCoupled

    color: "transparent"
    border.color: Theme.borderMain
    border.width: 1
    radius: 12 * Theme.scale

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 15 * Theme.scale
        spacing: 20 * Theme.scale

        // 1. 标题
        Text {
            text: "设备轴列表"
            color: Theme.textDim
            font.pixelSize: Theme.fontNormal
            font.bold: true
            Layout.alignment: Qt.AlignHCenter
        }

        // 2. 轴选择列表（6轴全覆盖，可滑动）
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ColumnLayout {
                width: parent.width
                spacing: 10 * Theme.scale

                // --- Y 轴 ---
                AxisItemDelegate {
                    name: "Y 轴 (水平)"
                    isActive: root.currentAxisName === "Y"
                    statusText: isActive ? "控制中" : "待机"
                    enabled: !root.locked
                    opacity: enabled ? 1.0 : 0.4
                    onClicked: {
                        root.currentAxisName = "Y"
                        root.axisChanged("Y")
                    }
                }

                // --- Z 轴 ---
                AxisItemDelegate {
                    name: "Z 轴 (垂直)"
                    isActive: root.currentAxisName === "Z"
                    statusText: isActive ? "控制中" : "待机"
                    enabled: !root.locked
                    opacity: enabled ? 1.0 : 0.4
                    onClicked: {
                        root.currentAxisName = "Z"
                        root.axisChanged("Z")
                    }
                }

                // --- R 轴 ---
                AxisItemDelegate {
                    name: "R 轴 (旋转)"
                    isActive: root.currentAxisName === "R"
                    statusText: isActive ? "控制中" : "待机"
                    enabled: !root.locked
                    opacity: enabled ? 1.0 : 0.4
                    onClicked: {
                        root.currentAxisName = "R"
                        root.axisChanged("R")
                    }
                }

                // --- X 轴（逻辑龙门轴） ---
                AxisItemDelegate {
                    name: "X 轴 (龙门逻辑)"
                    isActive: root.currentAxisName === "X"
                    statusText: {
                        if (root.currentAxisName === "X") {
                            return root.gantryStatusText || "控制中"
                        }
                        return root.gantryStatusText || "待机"
                    }
                    isDual: true
                    enabled: !root.locked
                    opacity: enabled ? 1.0 : 0.4
                    // 龙门耦合状态指示色
                    indicatorColor: root.gantryStatusColor
                    onClicked: {
                        root.currentAxisName = "X"
                        root.axisChanged("X")
                    }
                }

                Item { height: 10 * Theme.scale } // 分隔线

                // --- X1 轴（物理龙门轴1） ---
                AxisItemDelegate {
                    name: "X1 轴 (物理)"
                    isActive: root.currentAxisName === "X1"
                    statusText: isActive ? "控制中" : "待机"
                    // 物理轴在龙门耦合时需要标记为"受龙门控制"
                    subLabel: (!root.gantryViewModel || root.gantryViewModel.isCoupled) ? "↳ 龙门" : ""
                    enabled: !root.locked
                    opacity: enabled ? 1.0 : 0.4
                    onClicked: {
                        // 仅当龙门已解耦或该轴独立可用时允许切换
                        root.currentAxisName = "X1"
                        root.axisChanged("X1")
                    }
                }

                // --- X2 轴（物理龙门轴2） ---
                AxisItemDelegate {
                    name: "X2 轴 (物理)"
                    isActive: root.currentAxisName === "X2"
                    statusText: isActive ? "控制中" : "待机"
                    subLabel: (!root.gantryViewModel || root.gantryViewModel.isCoupled) ? "↳ 龙门" : ""
                    enabled: !root.locked
                    opacity: enabled ? 1.0 : 0.4
                    onClicked: {
                        root.currentAxisName = "X2"
                        root.axisChanged("X2")
                    }
                }
            } // end inner ColumnLayout
        } // end ScrollView
    } // end outer ColumnLayout
}
