import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import servoV6

/**
 * GantryTelemetryBlock.qml
 * 龙门遥测面板 — 双轴位置差监控 + 使能/报警/限位指示灯
 *
 * 绑定到 QtGantryViewModel 的 Q_PROPERTY
 */
Rectangle {
    id: root
    color: "#1a1a2e"
    border.color: "#2a2a4e"
    border.width: 1
    radius: 6

    property var gantryVM: null
    property string groupName: "Gantry 1"

    implicitHeight: telemetryCol.implicitHeight + 20
    implicitWidth: 300

    ColumnLayout {
        id: telemetryCol
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        // ── 标题 ──
        Text {
            text: root.groupName + " — 遥测"
            color: "#e0e0e0"
            font.pixelSize: 13
            font.bold: true
        }

        // ── 位置差 (同步精度) ──
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "位置差 (ΔX):"
                color: "#aaa"
                font.pixelSize: 11
            }
            Text {
                text: {
                    if (!root.gantryVM) return "---"
                    var delta = Math.abs(root.gantryVM.x1Position - root.gantryVM.x2Position)
                    return delta.toFixed(3) + " mm"
                }
                color: {
                    if (!root.gantryVM) return "#555"
                    var delta = Math.abs(root.gantryVM.x1Position - root.gantryVM.x2Position)
                    return delta > 1.0 ? "#ff5252" : (delta > 0.1 ? "#ff9800" : "#81c784")
                }
                font.pixelSize: 14
                font.bold: true
            }
        }

        // ── 聚合状态 ──
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "状态:"
                color: "#aaa"
                font.pixelSize: 11
            }
            Text {
                text: root.gantryVM ? root.gantryVM.stateDescription : "---"
                color: "#e0e0e0"
                font.pixelSize: 11
            }
        }

        // ── 分隔线 ──
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#3a3a5e"
        }

        // ── 指示灯网格 ──
        GridLayout {
            columns: 2
            rowSpacing: 4
            columnSpacing: 8

            // X1 Enabled
            IndicatorLabel { label: "X1 使能"; active: root.gantryVM && root.gantryVM.x1Enabled }
            // X2 Enabled
            IndicatorLabel { label: "X2 使能"; active: root.gantryVM && root.gantryVM.x2Enabled }

            // Alarm
            IndicatorLabel {
                label: "报警"
                active: root.gantryVM && root.gantryVM.isAnyAlarm
                activeColor: "#ff1744"
            }
            // Limit
            IndicatorLabel {
                label: "限位"
                active: root.gantryVM && root.gantryVM.isAnyLimit
                activeColor: "#ff9100"
            }

            // X1 Pos Limit
            IndicatorLabel {
                label: "X1 正限位"
                active: root.gantryVM && root.gantryVM.x1PosLimit
                activeColor: "#ff5252"
            }
            // X1 Neg Limit
            IndicatorLabel {
                label: "X1 负限位"
                active: root.gantryVM && root.gantryVM.x1NegLimit
                activeColor: "#ff5252"
            }

            // X2 Pos Limit
            IndicatorLabel {
                label: "X2 正限位"
                active: root.gantryVM && root.gantryVM.x2PosLimit
                activeColor: "#ff5252"
            }
            // X2 Neg Limit
            IndicatorLabel {
                label: "X2 负限位"
                active: root.gantryVM && root.gantryVM.x2NegLimit
                activeColor: "#ff5252"
            }
        }

        // ── 命令槽状态 ──
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "命令槽:"
                color: "#aaa"
                font.pixelSize: 11
            }
            Rectangle {
                width: 10; height: 10; radius: 5
                color: root.gantryVM && root.gantryVM.canAcceptCommand ? "#4caf50" : "#ff5252"
            }
            Text {
                text: root.gantryVM && root.gantryVM.canAcceptCommand ? "空闲" : "忙碌"
                color: root.gantryVM && root.gantryVM.canAcceptCommand ? "#81c784" : "#ff5252"
                font.pixelSize: 11
            }
        }

        // ── 分隔线 ──
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#3a3a5e"
        }

        // ── 指令日志 ──
        Text {
            text: "指令日志"
            color: "#e0e0e0"
            font.pixelSize: 11
            font.bold: true
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 100
            clip: true

            TextArea {
                id: logArea
                readOnly: true
                color: "#81c784"
                font.pixelSize: 9
                font.family: "Courier New"
                background: Rectangle { color: "#0d0d1a" }
                text: root.gantryVM ? root.gantryVM.getCommandLog() : ""
            }
        }

        // 定时刷新日志
        Timer {
            interval: 500
            running: true
            repeat: true
            onTriggered: {
                if (root.gantryVM) {
                    var newLog = root.gantryVM.getCommandLog()
                    if (logArea.text !== newLog) {
                        logArea.text = newLog
                        // 自动滚动到底部
                        logArea.cursorPosition = logArea.length
                    }
                }
            }
        }
    }

    // ── 指示灯组件 ──
    component IndicatorLabel: RowLayout {
        property string label: ""
        property bool active: false
        property color activeColor: "#4caf50"
        property color inactiveColor: "#444"

        spacing: 4
        Rectangle {
            width: 8; height: 8; radius: 4
            color: active ? activeColor : inactiveColor
        }
        Text {
            text: label
            color: active ? "#e0e0e0" : "#666"
            font.pixelSize: 10
        }
    }
}
