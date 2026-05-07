import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import servoV6

/**
 * MainDashboard.qml - 重构版
 * 1. 侧边栏全局化，支持点击实时切换
 * 2. 默认启动展示并定位到 R 轴
 */
Item {
    id: root

    // --- 核心属性 ---
    property var axisVMs: ({})       // 由外部注入
    property var gantryVMs: ({})     // 由外部注入

    // 默认选中的轴名称改为 "R"
    property string currentAxisName: "R"

    // ── 辅助逻辑：获取排序后的键列表，确保索引稳定 ──
    readonly property var axisKeys: axisVMs ? Object.keys(axisVMs).sort() : []
    readonly property var gantryKeys: gantryVMs ? Object.keys(gantryVMs).sort() : []

    // ── 切换函数 ──
    function switchToAxis(name) {
        root.currentAxisName = name;

        // 1. 先检查是否是单轴 (Y, Z, R)
        let axisIdx = axisKeys.indexOf(name);
        if (axisIdx !== -1) {
            contentView.currentIndex = axisIdx;
            console.log("切换到单轴:", name, "索引:", axisIdx);
            return;
        }

        // 2. 如果不是单轴，检查是否是龙门组
        let gantryIdx = gantryKeys.indexOf(name);
        if (gantryIdx !== -1) {
            // 关键点：索引偏移量 = 单轴的总数 + 当前龙门组的序号
            let finalIdx = axisKeys.length + gantryIdx;
            contentView.currentIndex = finalIdx;
            console.log("切换到龙门组:", name, "计算索引:", finalIdx);
            return;
        }
    }

    // ── 启动初始化 ──
    Component.onCompleted: {
        // 使用 callLater 确保 Repeater 完成对象实例化后再执行跳转
        Qt.callLater(() => {
            switchToAxis("R");
        });
    }

    // 主背景
    Rectangle {
        anchors.fill: parent
        color: "#0d1117"

        // 使用 RowLayout 将界面分为 [左侧导航] 和 [右侧内容]
        RowLayout {
            anchors.fill: parent
            anchors.margins: 15
            spacing: 15

            // ==========================================
            // 1. 左侧全局导航栏 (设备轴列表)
            // ==========================================
            AxisSelectorBlock {
                id: globalSidebar
                Layout.fillHeight: true
                Layout.preferredWidth: 200 * Theme.scale

                // 同步当前选中的轴名称，用于高亮显示
                currentAxisName: root.currentAxisName

                // 监听侧边栏发出的切换信号
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

                // ── 顶栏：面包屑与全局状态 ──
                RowLayout {
                    Layout.fillWidth: true
                    height: 40 * Theme.scale

                    Text {
                        text: "设备控制面板 / " + root.currentAxisName + " 轴"
                        color: Theme.textMain
                        font.pixelSize: Theme.fontLarge
                        font.bold: true
                    }

                    Item { Layout.fillWidth: true } // 占位弹簧

                    // 这里可以保留原有的状态指示灯或心跳包显示
                    Rectangle {
                        width: 12; height: 12; radius: 6
                        color: "#4caf50"
                        Text {
                            anchors.left: parent.right
                            anchors.leftMargin: 8
                            text: "PLC Connected"
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSmall
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }

                // ── 核心视图切换容器 ──
                StackLayout {
                    id: contentView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: 0 // 初始索引

                    // --- 动态生成独立轴控制页面 ---
                    Repeater {
                        model: root.axisKeys
                        delegate: RowLayout {
                            id: axisPage
                            property string axisName: modelData
                            property var vm: root.axisVMs[axisName]

                            spacing: 15

                            // 每一个页面包含 遥测 + 控制
                            TelemetryBlock {
                                Layout.fillHeight: true
                                Layout.preferredWidth: 360 * Theme.scale
                                viewModel: axisPage.vm
                            }

                            ActionControlBlock {
                                Layout.fillHeight: true
                                Layout.fillWidth: true
                                viewModel: axisPage.vm
                            }
                        }
                    }

                    // --- 动态生成龙门组控制页面 (如有) ---
                    Repeater {
                        model: root.gantryKeys
                        delegate: RowLayout {
                            property string gantryName: modelData
                            property var vm: root.gantryVMs[gantryName]
                            spacing: 15

                            GantryTelemetryBlock {
                                Layout.fillHeight: true
                                Layout.preferredWidth: 320 * Theme.scale
                                gantryVM: vm
                                groupName: gantryName
                            }

                            GantryControlBlock {
                                Layout.fillHeight: true
                                Layout.fillWidth: true
                                gantryVM: vm
                                groupName: gantryName
                            }
                        }
                    }
                }
            }
        }
    }
}