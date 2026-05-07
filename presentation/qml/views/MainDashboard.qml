import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * MainDashboard.qml
 * 主面板总装区 — 支持多独立轴 + 多组龙门控制的 Tab 视图
 *
 * 数据源：
 *   - axisVMs  (QVariantMap): key=轴名("Y","Z","R") → value=QtAxisViewModel*
 *   - gantryVMs (QVariantMap): key=组名("Gantry-A",...) → value=QtGantryViewModel*
 */
Item {
    id: root

    // ── 外部注入属性 ──
    property var axisVMs: ({})       // 独立轴 VM 映射表
    property var gantryVMs: ({})     // 龙门组 VM 映射表

    // ── 辅助：获取独立轴 key 列表 ──
    readonly property var axisKeys: axisVMs ? Object.keys(axisVMs) : []
    readonly property var gantryKeys: gantryVMs ? Object.keys(gantryVMs) : []

    Rectangle {
        anchors.fill: parent
        color: "#0d1117"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 10

            // ── 顶栏 ──
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "ServoV6 控制系统"
                    color: "#e0e0e0"
                    font.pixelSize: 18
                    font.bold: true
                }
                Item { Layout.fillWidth: true }
                // 日志状态灯
                Rectangle {
                    width: 8; height: 8; radius: 4
                    color: "#4caf50"
                }
                Text {
                    text: "日志运行中"
                    color: "#888"
                    font.pixelSize: 10
                }
            }

            // ── TabBar — 动态生成：独立轴 Tab + 龙门 Tab ──
            TabBar {
                id: tabBar
                Layout.fillWidth: true
                background: Rectangle { color: "#161b22" }

                // 独立轴 Tab：每个轴一个 Tab
                Repeater {
                    id: axisTabs
                    model: root.axisKeys

                    TabButton {
                        text: modelData
                        contentItem: Text {
                            text: parent.text
                            color: parent.checked ? "#58a6ff" : "#8b949e"
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                        }
                        background: Rectangle {
                            color: parent.checked ? "#1a2332" : "#161b22"
                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width; height: 2
                                color: parent.parent.checked ? "#58a6ff" : "transparent"
                            }
                        }
                    }
                }

                // 龙门组 Tab：每个龙门组一个 Tab
                Repeater {
                    id: gantryTabs
                    model: root.gantryKeys

                    TabButton {
                        text: "[龙门] " + modelData
                        contentItem: Text {
                            text: parent.text
                            color: parent.checked ? "#4caf50" : "#8b949e"
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                        }
                        background: Rectangle {
                            color: parent.checked ? "#1a2e1a" : "#161b22"
                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width; height: 2
                                color: parent.parent.checked ? "#4caf50" : "transparent"
                            }
                        }
                    }
                }
            }

            // ── 内容区 (StackLayout) ──
            StackLayout {
                id: contentStack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: tabBar.currentIndex

                // ── 独立轴页面 (按 axisKeys 顺序动态生成) ──
                Repeater {
                    id: axisPages
                    model: root.axisKeys

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true

                        property string axisName: modelData
                        property var vm: root.axisVMs[axisName]

                        RowLayout {
                            anchors.fill: parent
                            spacing: 10

                            // 轴选择器侧栏
                            AxisSelectorBlock {
                                id: axisSelector
                                Layout.fillHeight: true
                                Layout.preferredWidth: 180
                                // 高亮当前轴
                                currentAxisName: axisName
                            }

                            // 操作控制面板
                            ActionControlBlock {
                                id: actionBlock
                                Layout.fillHeight: true
                                Layout.preferredWidth: 300
                                viewModel: vm
                            }

                            // 遥测数据面板
                            TelemetryBlock {
                                id: telemetryBlock
                                Layout.fillHeight: true
                                Layout.fillWidth: true
                                viewModel: vm
                            }
                        }
                    }
                }

                // ── 龙门组页面 (按 gantryKeys 顺序动态生成) ──
                Repeater {
                    id: gantryPages
                    model: root.gantryKeys

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true

                        property string groupName: modelData
                        property var vm: root.gantryVMs[groupName]

                        RowLayout {
                            anchors.fill: parent
                            spacing: 10

                            AxisSelectorBlock {
                                Layout.fillHeight: true
                                Layout.preferredWidth: 180
                            }

                            GantryControlBlock {
                                id: gantryControl
                                Layout.fillHeight: true
                                Layout.preferredWidth: 420
                                gantryVM: vm
                                groupName: groupName
                            }

                            GantryTelemetryBlock {
                                id: gantryTelemetry
                                Layout.fillHeight: true
                                Layout.preferredWidth: 300
                                gantryVM: vm
                                groupName: groupName
                            }
                        }
                    }
                }
            }
        }
    }
}
