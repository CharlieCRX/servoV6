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
    property var snapshotAdapter: controlSnapshot
    property string groupName: "Machine_A"
    property bool jogAxisSwitchLocked: false  // ★ JOG 点动活跃时阻止轴切换
    signal axisChanged(string axisName)  // 切换轴时发出的信号

    readonly property string groupLetter: groupName === "Machine_B" ? "B" : "A"
    readonly property int snapshotRevision: snapshotAdapter ? snapshotAdapter.revision : 0

    function axisSnapshot(axisName) {
        root.snapshotRevision
        return snapshotAdapter ? snapshotAdapter.axisFor(groupLetter, axisName) : ({})
    }

    function axisAvailable(axisName) {
        var ax = axisSnapshot(axisName)
        return ax.bound === true && ax.hmiVisible === true
    }

    function axisStatus(axisName, isActive) {
        if (root.jogAxisSwitchLocked && isActive) return "点动中..."
        if (!axisAvailable(axisName)) return "未绑定"
        return isActive ? "控制中" : "待机"
    }

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
                    name: root.jogAxisSwitchLocked && !isActive ? "Y 轴 (水平) 🔒" : "Y 轴 (水平)"
                    visible: root.axisAvailable("Y")
                    isActive: root.currentAxisName === "Y"
                    statusText: root.axisStatus("Y", isActive)
                    enabled: !root.locked && (root.axisSnapshot("Y").trusted === true) && (!root.jogAxisSwitchLocked || isActive)
                    opacity: enabled ? 1.0 : 0.4
                    onClicked: {
                        if (root.jogAxisSwitchLocked) return
                        root.axisChanged("Y")
                    }
                }

                // --- Z 轴 ---
                AxisItemDelegate {
                    name: root.jogAxisSwitchLocked && !isActive ? "Z 轴 (垂直) 🔒" : "Z 轴 (垂直)"
                    visible: root.axisAvailable("Z")
                    isActive: root.currentAxisName === "Z"
                    statusText: root.axisStatus("Z", isActive)
                    enabled: !root.locked && (root.axisSnapshot("Z").trusted === true) && (!root.jogAxisSwitchLocked || isActive)
                    opacity: enabled ? 1.0 : 0.4
                    onClicked: {
                        if (root.jogAxisSwitchLocked) return
                        root.axisChanged("Z")
                    }
                }

                // --- R 轴 ---
                AxisItemDelegate {
                    name: root.jogAxisSwitchLocked && !isActive ? "R 轴 (旋转) 🔒" : "R 轴 (旋转)"
                    visible: root.axisAvailable("R")
                    isActive: root.currentAxisName === "R"
                    statusText: root.axisStatus("R", isActive)
                    enabled: !root.locked && (root.axisSnapshot("R").trusted === true) && (!root.jogAxisSwitchLocked || isActive)
                    opacity: enabled ? 1.0 : 0.4
                    onClicked: {
                        if (root.jogAxisSwitchLocked) return
                        root.axisChanged("R")
                    }
                }

                // --- X 轴（逻辑龙门轴） ---
                AxisItemDelegate {
                    name: root.jogAxisSwitchLocked && !isActive ? "X 轴 (前后) 🔒" : "X 轴 (前后)"
                    visible: root.axisAvailable("X")
                    isActive: root.currentAxisName === "X"
                    statusText: root.axisStatus("X", isActive)
                    isDual: true
                    enabled: !root.locked && (root.axisSnapshot("X").trusted === true) && (!root.jogAxisSwitchLocked || isActive)
                    opacity: enabled ? 1.0 : 0.4
                    onClicked: {
                        if (root.jogAxisSwitchLocked) return
                        root.axisChanged("X")
                    }
                }

                Item {
                    height: (root.axisAvailable("X1") || root.axisAvailable("X2"))
                            ? 10 * Theme.scale : 0
                    visible: root.axisAvailable("X1") || root.axisAvailable("X2")
                }

                // --- X1 轴（物理龙门轴1） ---
                AxisItemDelegate {
                    name: root.jogAxisSwitchLocked && !isActive ? "X1 轴 (物理) 🔒" : "X1 轴 (物理)"
                    visible: root.axisAvailable("X1")
                    isActive: root.currentAxisName === "X1"
                    statusText: root.axisStatus("X1", isActive)
                    subLabel: (root.gantryViewModel && root.gantryViewModel.isCoupled) ? "受龙门控制" : ""
                    enabled: !root.locked && (root.axisSnapshot("X1").trusted === true) && (!root.jogAxisSwitchLocked || isActive)
                    opacity: enabled ? 1.0 : 0.4
                    onClicked: {
                        if (root.jogAxisSwitchLocked) return
                        root.axisChanged("X1")
                    }
                }

                // --- X2 轴（物理龙门轴2） ---
                AxisItemDelegate {
                    name: root.jogAxisSwitchLocked && !isActive ? "X2 轴 (物理) 🔒" : "X2 轴 (物理)"
                    visible: root.axisAvailable("X2")
                    isActive: root.currentAxisName === "X2"
                    statusText: root.axisStatus("X2", isActive)
                    subLabel: (root.gantryViewModel && root.gantryViewModel.isCoupled) ? "受龙门控制" : ""
                    enabled: !root.locked && (root.axisSnapshot("X2").trusted === true) && (!root.jogAxisSwitchLocked || isActive)
                    opacity: enabled ? 1.0 : 0.4
                    onClicked: {
                        if (root.jogAxisSwitchLocked) return
                        root.axisChanged("X2")
                    }
                }
            } // end inner ColumnLayout
        } // end ScrollView
    } // end outer ColumnLayout
}
