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

            // --- Y 轴 (当前开发重点) ---
            AxisItemDelegate {
                name: "Y 轴 (水平)"
                isActive: root.currentAxisName === "Y"
                statusText: isActive ? "控制中" : "待机"
                onClicked: {
                    root.currentAxisName = "Y"
                    root.axisChanged("Y")
                }
            }

            // --- Z 轴 (预留) ---
            AxisItemDelegate {
                name: "Z 轴 (垂直)"
                isActive: root.currentAxisName === "Z"
                statusText: "未就绪"
                opacity: 0.5 // 置灰表示尚未开发
                onClicked: {} 
            }

            // --- R 轴 (预留) ---
            AxisItemDelegate {
                name: "R 轴 (旋转)"
                isActive: root.currentAxisName === "R"
                statusText: "未就绪"
                opacity: 0.5
                onClicked: {}
            }

            Item { height: 20 * Theme.scale } // 间距

            // --- X1X2 联动 (Phase 7 预留) ---
            AxisItemDelegate {
                name: "X1/X2 联动"
                isActive: root.currentAxisName === "X1X2"
                statusText: "双轴模式"
                isDual: true
                opacity: 0.6
                onClicked: {}
            }
        }

        Item { Layout.fillHeight: true } // 底部弹簧
    }
}