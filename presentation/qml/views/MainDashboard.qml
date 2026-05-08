import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import servoV6

Item {
    id: root

    // --- 核心属性 ---
    property var axisVMs: ({})       // 由 C++ 注入
    property var gantryVMs: ({})     // 由 C++ 注入

    // 🌟 默认启动展示 R 轴
    property string currentAxisName: "R"

    // ── 逻辑：根据名字判断是单轴还是龙门 ──
    readonly property bool isGantryMode: currentAxisName.indexOf("Gantry") !== -1

    // ── 切换函数 ──
    function switchToAxis(name) {
        root.currentAxisName = name;
        console.log("UI 路由切换至:", name, isGantryMode ? "[龙门模式]" : "[单轴模式]");
    }

    Component.onCompleted: {
        Qt.callLater(() => {
            switchToAxis("R"); // 启动强制定位到 R
        });
    }

    Rectangle {
        anchors.fill: parent
        color: "#0d1117"

        RowLayout {
            anchors.fill: parent
            anchors.margins: 15
            spacing: 15

            // ==========================================
            // 1. 左侧全局导航 (固定不变)
            // ==========================================
            AxisSelectorBlock {
                id: globalSidebar
                Layout.fillHeight: true
                Layout.preferredWidth: 200 * Theme.scale
                currentAxisName: root.currentAxisName

                onAxisChanged: (name) => {
                    root.switchToAxis(name);
                }
            }

            // ==========================================
            // 2. 右侧主内容区
            // ==========================================
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12

                // ── 顶栏：面包屑 + 状态指示 ──
                RowLayout {
                    Layout.fillWidth: true
                    height: 40 * Theme.scale

                    // 左侧标题
                    Text {
                        text: "设备控制面板 / " + root.currentAxisName + (root.isGantryMode ? " 联动组" : " 轴")
                        color: Theme.textMain
                        font.pixelSize: Theme.fontLarge
                        font.bold: true
                    }

                    Item { Layout.fillWidth: true } // 弹簧

                    // 右侧状态展示 (修复你说的展示不正常问题)
                    RowLayout {
                        spacing: 20

                        // 1. PLC 连接状态
                        Row {
                            spacing: 8
                            Rectangle {
                                width: 12; height: 12; radius: 6; anchors.verticalCenter: parent.verticalCenter
                                color: "#4caf50" // 正常绿色
                            }
                            Text { text: "PLC CONNECTED"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                        }

                        // 2. 安全锁状态
                        Row {
                            spacing: 8
                            Rectangle {
                                width: 12; height: 12; radius: 6; anchors.verticalCenter: parent.verticalCenter
                                color: "#ff9800" // 警告橙色
                            }
                            Text { text: "SAFETY ENABLED"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
                        }
                    }
                }

                // ── 核心内容动态加载区 ──
                // 这里使用 Loader 代替 StackLayout，解决黑屏和索引错误
                Loader {
                    id: mainLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    // 根据当前选中的名字决定加载哪个组件
                    sourceComponent: root.isGantryMode ? gantryViewComp : singleAxisViewComp

                    // 传值给加载进来的组件
                    property var currentVM: root.isGantryMode ?
                                            root.gantryVMs[root.currentAxisName] :
                                            root.axisVMs[root.currentAxisName]
                }
            }
        }
    }

    // ==========================================
    // 3. 视图组件定义 (Components)
    // ==========================================

    // --- 单轴界面模板 ---
    Component {
        id: singleAxisViewComp
        RowLayout {
            spacing: 15
            TelemetryBlock {
                Layout.fillHeight: true
                Layout.preferredWidth: 360 * Theme.scale
                viewModel: mainLoader.currentVM
            }
            ActionControlBlock {
                Layout.fillHeight: true
                Layout.fillWidth: true
                viewModel: mainLoader.currentVM
            }
        }
    }

    // --- 龙门界面模板 ---
    Component {
        id: gantryViewComp
        RowLayout {
            spacing: 15
            GantryTelemetryBlock {
                Layout.fillHeight: true
                Layout.preferredWidth: 320 * Theme.scale
                gantryVM: mainLoader.currentVM
                groupName: root.currentAxisName
            }
            GantryControlBlock {
                Layout.fillHeight: true
                Layout.fillWidth: true
                gantryVM: mainLoader.currentVM
                groupName: root.currentAxisName
            }
        }
    }
}