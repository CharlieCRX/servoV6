import QtQuick
import QtQuick.Layouts
import servoV6

Rectangle {
    id: root
    
    // === 核心接口 ===
    property string currentAxisName: "Y" // 默认选中 Y 轴
    signal axisChanged(string axisName)  // 切换轴时发出的信号

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

        // 2. 轴选择列表 (重复利用列表项)
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10 * Theme.scale

            // --- Y 轴 ---
            AxisItemDelegate {
                name: "Y 轴 (水平)"
                isActive: root.currentAxisName === "Y"
                statusText: isActive ? "控制中" : "待机"
                onClicked: {
                    root.currentAxisName = "Y"
                    root.axisChanged("Y") // 发出信号
                }
            }

            // --- Z 轴 ---
            AxisItemDelegate {
                name: "Z 轴 (垂直)"
                isActive: root.currentAxisName === "Z"
                statusText: isActive ? "控制中" : "待机"
                opacity: 1.0 // 取消置灰
                onClicked: {
                    root.currentAxisName = "Z"
                    root.axisChanged("Z") // 发出信号
                }
            }

            // --- R 轴 ---
            AxisItemDelegate {
                name: "R 轴 (旋转)"
                isActive: root.currentAxisName === "R"
                statusText: isActive ? "控制中" : "待机"
                opacity: 1.0 // 取消置灰
                onClicked: {
                    root.currentAxisName = "R"
                    root.axisChanged("R") // 发出信号
                }
            }

            Item { height: 20 * Theme.scale } // 间距

            // --- X1X2 联动 (Phase 7 预留) ---
            AxisItemDelegate {
                name: "X1/X2 联动"
                // 1. 将判断条件改为 "Gantry A"
                isActive: root.currentAxisName === "Gantry A"
                isDual: true
                opacity: 1.0

                onClicked: {
                    // 2. 将内部状态和发出的信号都改为 "Gantry A"
                    root.currentAxisName = "Gantry A"
                    root.axisChanged("Gantry A")
                }
            }
        }

        Item { Layout.fillHeight: true } // 底部弹簧
    }
}