import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import servoV6

Item {
    id: root

    // --- 核心属性 ---
    property var axisVMs: ({})
    property var gantryVMs: ({})

    property string currentAxisName: "R"

    // 🌟 解决黑屏的终极绝杀：QVariantMap 必须用 for...in 解析键名！
    property var axisKeys: []
    property var gantryKeys: []

    function extractKeys(map) {
        let arr = [];
        for (let key in map) {
            arr.push(key);
        }
        return arr.sort();
    }

    // 启动时解析出所有的设备名，交给 Repeater
    Component.onCompleted: {
        root.axisKeys = extractKeys(root.axisVMs);
        root.gantryKeys = extractKeys(root.gantryVMs);

        Qt.callLater(() => {
            switchToAxis("R");
        });
    }

    readonly property bool isGantryMode: currentAxisName.indexOf("Gantry") !== -1

    function switchToAxis(name) {
        // 🌟 防呆设计：如果 QML 发出的名字和 C++ 注册的 Map Key 对不上，立刻报错！
        if (!root.axisVMs[name] && !root.gantryVMs[name]) {
            console.error("⚠️ 严重警告: 找不到名字为 [" + name + "] 的设备！请检查 AxisSelectorBlock 传出的名字是否与 C++ 完全一致。");
        }

        root.currentAxisName = name;
        console.log("UI 路由切换至:", name, isGantryMode ? "[龙门模式]" : "[单轴模式]");
    }

    Rectangle {
        anchors.fill: parent
        color: "#0d1117"

        RowLayout {
            anchors.fill: parent
            anchors.margins: 15
            spacing: 15

            // ==========================================
            // 1. 左侧全局导航
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

                // ── 顶栏 ──
                RowLayout {
                    Layout.fillWidth: true
                    height: 40 * Theme.scale

                    Text {
                        text: "设备控制面板 / " + root.currentAxisName + (root.isGantryMode ? " 联动组" : " 轴")
                        color: Theme.textMain
                        font.pixelSize: Theme.fontLarge
                        font.bold: true
                    }
                    Item { Layout.fillWidth: true } // 占位弹簧
                }

                // ── 核心叠加区 (彻底抛弃 Loader 和 StackLayout) ──
                Item {
                    id: contentView
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    // --- 生成独立轴页面 ---
                    Repeater {
                        model: root.axisKeys
                        delegate: RowLayout {
                            anchors.fill: parent

                            // 🌟 魔法：只有当前名字和自己匹配时，才显示
                            visible: root.currentAxisName === modelData
                            spacing: 15

                            // 初始化时直接获取真实的 VM，永不为空！
                            property var vm: root.axisVMs[modelData]

                            TelemetryBlock {
                                Layout.fillHeight: true
                                Layout.preferredWidth: 360 * Theme.scale
                                viewModel: parent.vm
                            }
                            ActionControlBlock {
                                Layout.fillHeight: true
                                Layout.fillWidth: true
                                viewModel: parent.vm
                            }
                        }
                    }

                    // --- 生成龙门组页面 ---
                    Repeater {
                        model: root.gantryKeys
                        delegate: RowLayout {
                            anchors.fill: parent

                            // 🌟 魔法：双轴同理，名字匹配才显示
                            visible: root.currentAxisName === modelData
                            spacing: 15

                            // 初始化时直接获取真实的 VM，永不为空！
                            property var vm: root.gantryVMs[modelData]

                            GantryTelemetryBlock {
                                Layout.fillHeight: true
                                Layout.preferredWidth: 320 * Theme.scale
                                gantryVM: parent.vm
                                groupName: modelData
                            }
                            GantryControlBlock {
                                Layout.fillHeight: true
                                Layout.fillWidth: true
                                gantryVM: parent.vm
                                groupName: modelData
                            }
                        }
                    }
                } // End Item
            }
        }
    }
}