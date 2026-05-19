import QtQuick
import QtQuick.Layouts
import servoV6

Rectangle {
    id: root
    
    // === 核心接口 ===
    property string currentAxisName: "Y" // 默认选中 Y 轴
    property var emergencyViewModel: null
    signal axisChanged(string axisName)  // 切换轴时发出的信号

    // 急停锁定状态
    readonly property bool locked: emergencyViewModel && emergencyViewModel.isSystemLocked

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

        // 2. 轴选择列表（6轴全覆盖）
        ColumnLayout {
            Layout.fillWidth: true
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
                statusText: isActive ? "控制中" : "待机"
                isDual: true
                enabled: !root.locked
                opacity: enabled ? 1.0 : 0.4
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
                enabled: !root.locked
                opacity: enabled ? 1.0 : 0.4
                onClicked: {
                    root.currentAxisName = "X1"
                    root.axisChanged("X1")
                }
            }

            // --- X2 轴（物理龙门轴2） ---
            AxisItemDelegate {
                name: "X2 轴 (物理)"
                isActive: root.currentAxisName === "X2"
                statusText: isActive ? "控制中" : "待机"
                enabled: !root.locked
                opacity: enabled ? 1.0 : 0.4
                onClicked: {
                    root.currentAxisName = "X2"
                    root.axisChanged("X2")
                }
            }
        }

        Item { Layout.fillHeight: true } // 底部弹簧
    }
}
