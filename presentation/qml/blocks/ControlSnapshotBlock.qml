// ============================================================================
// ControlSnapshotBlock.qml —— Phase 2：统一状态快照只读对照面板
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》Phase 2：
//   - UI 开始展示统一快照（只读），仍由旧 Core 发控制，仅用于对照验证投影正确性；
//   - 快照自带 group/role/hmiVisible，UI 无需自行推导轴映射；
//   - trusted 与全局锁定影响 UI 的可用/锁定表现；
//   - **只读**：本面板不提交任何命令，不提供可写入口。
//
// 依赖组合根在 C++ 侧注入的 contextProperty `controlSnapshot`（UiControlAdapter）。
// ============================================================================
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import servoV6

Rectangle {
    id: root

    // === 接口 ===
    // C++ 组合根注入的 UiControlAdapter（见 main.cpp）。
    property var snapshotAdapter: controlSnapshot
    // 当前选中轴（来自 Main.qml，A/B + Y/X/X1/X2/Z/R）。
    property string groupLetter: "A"
    property string role: "Y"

    // === 派生的快照查询（QVariantMap，未匹配返回空 map）===
    readonly property var selectedAxis: snapshotAdapter
                                        ? snapshotAdapter.axisFor(groupLetter, role) : ({})

    color: Theme.panelBg
    radius: 10 * Theme.scale
    border.color: Theme.borderMain
    border.width: 2 * Theme.scale

    implicitHeight: Math.max(96 * Theme.scale, rowLayout.implicitHeight + 16 * Theme.scale)

    RowLayout {
        id: rowLayout
        anchors.fill: parent
        anchors.margins: 8 * Theme.scale
        spacing: 14 * Theme.scale

        // ---------- 连接 / 急停 / 全局锁定 指示灯 ----------
        ColumnLayout {
            spacing: 4 * Theme.scale
            RowLayout {
                spacing: 5 * Theme.scale
                Rectangle {
                    width: 10 * Theme.scale; height: 10 * Theme.scale; radius: height / 2
                    color: snapshotAdapter && snapshotAdapter.connected ? Theme.colorIdle : Theme.colorError
                }
                Text {
                    text: snapshotAdapter && snapshotAdapter.connected ? "连接" : "断连"
                    color: Theme.textMain; font.pixelSize: Theme.fontSmall
                }
            }
            RowLayout {
                spacing: 5 * Theme.scale
                Rectangle {
                    width: 10 * Theme.scale; height: 10 * Theme.scale; radius: height / 2
                    color: snapshotAdapter && snapshotAdapter.emergencyStop ? Theme.colorError : Theme.colorIdle
                }
                Text {
                    text: "急停:" + (snapshotAdapter && snapshotAdapter.emergencyStop ? "ON" : "OFF")
                    color: Theme.textMain; font.pixelSize: Theme.fontSmall
                }
            }
            RowLayout {
                spacing: 5 * Theme.scale
                Rectangle {
                    width: 10 * Theme.scale; height: 10 * Theme.scale; radius: height / 2
                    color: snapshotAdapter && snapshotAdapter.globallyLocked ? Theme.colorWarning : Theme.colorIdle
                }
                Text {
                    text: "全局锁定:" + (snapshotAdapter && snapshotAdapter.globallyLocked ? "ON" : "OFF")
                    color: Theme.textMain; font.pixelSize: Theme.fontSmall
                }
            }
        }

        // ---------- 当前轴快照（group/role 由快照自带，无需推导）----------
        ColumnLayout {
            spacing: 4 * Theme.scale
            Text {
                text: "轴 " + groupLetter + "." + role + "  槽位:" + (selectedAxis.slot ?? "-")
                color: Theme.textMain; font.pixelSize: Theme.fontNormal; font.bold: true
            }
            RowLayout {
                spacing: 10 * Theme.scale
                Text {
                    text: "位置:" + (selectedAxis.absPosition !== undefined ? selectedAxis.absPosition.toFixed(3) : "-")
                    color: Theme.colorIdle; font.pixelSize: Theme.fontSmall; font.family: "Monospace"
                }
                Text {
                    text: "运动态:" + (selectedAxis.motionStateName ?? "-")
                    color: selectedAxis.motionState >= 3 ? Theme.colorMoving : Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }
                Text {
                    text: "可信:" + (selectedAxis.trusted !== undefined ? (selectedAxis.trusted ? "是" : "否") : "-")
                    color: selectedAxis.trusted === true ? Theme.colorIdle : Theme.colorWarning
                    font.pixelSize: Theme.fontSmall
                }
                Text {
                    text: "锁定:" + (selectedAxis.locked !== undefined ? (selectedAxis.locked ? "是" : "否") : "-")
                    color: selectedAxis.locked === true ? Theme.colorWarning : Theme.colorIdle
                    font.pixelSize: Theme.fontSmall
                }

        // ---------- 龙门 A 组许可（只读快照投影）----------
        ColumnLayout {
            spacing: 4 * Theme.scale
            Text {
                text: "龙门 A"
                color: Theme.textMain; font.pixelSize: Theme.fontNormal; font.bold: true
            }
            Text {
                text: "状态:" + (snapshotAdapter ? (snapshotAdapter.gantry(0).stateName ?? "-") : "-")
                color: snapshotAdapter && snapshotAdapter.gantry(0).state === 3 ? Theme.colorIdle : Theme.textDim
                font.pixelSize: Theme.fontSmall
            }
            Text {
                text: "逻辑许可:" + (snapshotAdapter && snapshotAdapter.gantry(0).logicalControlAllowed ? "ON" : "OFF")
                    + "  成员许可:" + (snapshotAdapter && snapshotAdapter.gantry(0).memberControlAllowed ? "ON" : "OFF")
                color: Theme.textDim; font.pixelSize: Theme.fontSmall
            }
        }

        // ---------- 操作条目（进行中 + 最近历史）----------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 2 * Theme.scale
            Text {
                text: "操作队列"
                color: Theme.textMain; font.pixelSize: Theme.fontSmall; font.bold: true
            }
            Repeater {
                model: snapshotAdapter ? snapshotAdapter.operations : []
                delegate: Text {
                    text: (modelData.operationId ?? "") + " [" + (modelData.source ?? "-")
                          + "] " + (modelData.axis ?? "-") + " -> " + (modelData.state ?? "-")
                    color: modelData.state === "失败" || modelData.state === "已拒绝"
                           ? Theme.colorError : Theme.textDim
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                }
            }
        }
    }
}

                Text {
                    text: "占用:" + (selectedAxis.leased ? (selectedAxis.leaseOwnerName ?? "-") : "空闲")
                    color: selectedAxis.leased ? Theme.colorMoving : Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }
            }
        }
