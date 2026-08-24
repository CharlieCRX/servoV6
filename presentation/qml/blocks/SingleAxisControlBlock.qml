// ============================================================================
// SingleAxisControlBlock.qml —— UI-2：vnext 单轴控制面板（A.Y / A.Z 首轮）
// ============================================================================
// 依据《UI 新链路控制验证实施清单》§3.2/3.3：
//   - 读取：contextProperty `controlSnapshot`（UiControlAdapter）的 axisFor(group, role)；
//   - 写入：contextProperty `controlCommand`（UiControlCommandAdapter）—— UI 唯一可写入口，
//     所有操作只经它生成 ControlCommand{source=Ui}，绝不直写 PLC；
//   - 操作状态：显示最近一次 operationId 与最终状态（经快照 operations 回显）。
// 首轮仅放开独立单轴（A.Y / A.Z）；龙门成员 X1/X2 与逻辑 X 不提供独立运动按钮。
// 普通动作可用条件：connected && safetyTrusted && !emergencyStop && !globallyLocked
//   && bound && trusted && hmiVisible && !leased；停止/急停/解除急停不受普通锁定影响。
// ============================================================================
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: root

    // === 接口 ===
    property string groupLetter: "A"
    property string role: "Y"
    property var snapshotAdapter: controlSnapshot
    property var commandAdapter: controlCommand

    // === 快照查询 ===
    readonly property var ax: snapshotAdapter ? snapshotAdapter.axisFor(groupLetter, role) : ({})
    readonly property bool connected: snapshotAdapter ? snapshotAdapter.connected : false
    readonly property bool safetyTrusted: snapshotAdapter ? snapshotAdapter.safetyTrusted : false
    readonly property bool emergencyStop: snapshotAdapter ? snapshotAdapter.emergencyStop : false
    readonly property bool globallyLocked: snapshotAdapter ? snapshotAdapter.globallyLocked : false
    // 普通动作可用性（龙门成员轴/逻辑 X 一律不可运动，即使拓扑 bound）。
    readonly property bool isIndependent: role === "Y" || role === "Z" || role === "R"

    // ★ PLC 实时运动状态回显（快照 motionState = PLC 反馈 D128）：
    //   0=未使能 1=轴控开电机关 2=电机空闲 3=正向点动 4=反向点动 5=绝对定位中 6=相对定位中。
    readonly property bool jogForwardActive: (ax.motionState ?? 0) === 3
    readonly property bool jogBackwardActive: (ax.motionState ?? 0) === 4
    readonly property bool positioningActive: (ax.motionState ?? 0) === 5 || (ax.motionState ?? 0) === 6
    // 轴正在运动（点动或定位中）：普通操作（使能/参数/定位）锁定，仅停止/急停可用。
    readonly property bool moving: (ax.motionState ?? 0) >= 3

    readonly property bool canControl: connected && safetyTrusted && !emergencyStop
                                       && !globallyLocked && ax.bound && ax.trusted
                                       && ax.hmiVisible && !ax.leased && isIndependent
                                       && !moving          // PLC 正在移动 → 锁定普通操作
    // 点动按钮可用性：定位中(motionState 5/6)锁定；PLC 点动中(3/4)仍须保持可用，
    // 以便用户松开触发 stopJog 停止（与"按住持续点动"语义一致）。
    readonly property bool canJog: connected && safetyTrusted && !emergencyStop
                                   && !globallyLocked && ax.bound && ax.trusted
                                   && ax.hmiVisible && !ax.leased && isIndependent
                                   && !positioningActive
    readonly property bool canSubmit: commandAdapter !== null && commandAdapter !== undefined

    color: Theme.panelBg
    radius: 10 * Theme.scale
    border.color: Theme.borderMain
    border.width: 2 * Theme.scale

    implicitHeight: Math.max(220 * Theme.scale, colLayout.implicitHeight + 16 * Theme.scale)

    ColumnLayout {
        id: colLayout
        anchors.fill: parent
        anchors.margins: 8 * Theme.scale
        spacing: 6 * Theme.scale

        // ---------- 头部：轴名 + 状态 ----------
        RowLayout {
            spacing: 8 * Theme.scale
            Text {
                text: "轴 " + groupLetter + "." + role + "  槽位:" + (ax.slot ?? "-")
                color: Theme.textMain; font.pixelSize: Theme.fontNormal; font.bold: true
            }
            Rectangle {
                width: 10 * Theme.scale; height: 10 * Theme.scale; radius: height / 2
                color: canControl ? Theme.colorIdle
                     : (emergencyStop || globallyLocked ? Theme.colorError : Theme.colorWarning)
            }
            Text {
                text: ax.motionStateName ?? "-"
                color: canControl ? Theme.colorIdle : Theme.textDim
                font.pixelSize: Theme.fontSmall
            }
            Text {
                text: "占用:" + (ax.leased ? (ax.leaseOwnerName ?? "-") : "空闲")
                color: ax.leased ? Theme.colorMoving : Theme.textDim
                font.pixelSize: Theme.fontSmall
            }
        }

        // ---------- 使能 ----------
        RowLayout {
            spacing: 10 * Theme.scale
            CheckBox {
                text: "轴控"
                enabled: canControl
                checked: false
                onToggled: if (canSubmit) commandAdapter.enableAxis(groupLetter, role, checked)
            }
            CheckBox {
                text: "电机"
                enabled: canControl
                checked: false
                onToggled: if (canSubmit) commandAdapter.enableMotor(groupLetter, role, checked)
            }
        }

        // ---------- 速度 ----------
        GridLayout {
            columns: 4
            Text { text: "手动速度"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
            TextField { id: manualSpeed; text: "0"; enabled: canControl; implicitWidth: 60 * Theme.scale }
            Button {
                text: "设置手动速度"
                enabled: canControl && canSubmit
                onClicked: commandAdapter.setManualSpeed(groupLetter, role, Number(manualSpeed.text))
            }
            Text { text: commandAdapter ? commandAdapter.lastError : ""; color: Theme.colorError; font.pixelSize: Theme.fontSmall }

            Text { text: "定位速度"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
            TextField { id: positioningSpeed; text: "10"; enabled: canControl; implicitWidth: 60 * Theme.scale }
            Button {
                text: "设置定位速度"
                enabled: canControl && canSubmit
                onClicked: commandAdapter.setPositioningSpeed(groupLetter, role, Number(positioningSpeed.text))
            }
            Text { text: (ax.positioningSpeed ?? 0).toFixed(1); color: Theme.colorIdle; font.pixelSize: Theme.fontSmall }
        }

        // ---------- 定位 ----------
        RowLayout {
            spacing: 6 * Theme.scale
            Text { text: "绝对目标"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
            TextField { id: absTarget; text: "0"; enabled: canControl; implicitWidth: 70 * Theme.scale }
            Button {
                // ★ PLC 状态回显：PLC 正在绝对定位(motionState==5) → 显示"运行中..."
                text: positioningActive ? "定位中..." : "绝对定位"
                enabled: canControl && canSubmit
                onClicked: commandAdapter.startAbsMove(groupLetter, role, Number(absTarget.text), Number(positioningSpeed.text))
            }
            Text { text: "相对增量"; color: Theme.textDim; font.pixelSize: Theme.fontSmall }
            TextField { id: relDelta; text: "0"; enabled: canControl; implicitWidth: 70 * Theme.scale }
            Button {
                // ★ PLC 状态回显：PLC 正在相对定位(motionState==6) → 显示"运行中..."
                text: positioningActive ? "定位中..." : "相对定位"
                enabled: canControl && canSubmit
                onClicked: commandAdapter.startRelMove(groupLetter, role, Number(relDelta.text), Number(positioningSpeed.text))
            }
        }

        // ---------- 点动（按住=启动 / 松开=停止）----------
        RowLayout {
            spacing: 8 * Theme.scale
            Button {
                text: "正转"
                enabled: canJog && canSubmit
                // ★ PLC 状态回显：PLC 反馈正在正向点动(motionState==3) → 按钮显示按下感。
                highlighted: jogForwardActive
                onPressedChanged: {
                    if (pressed) commandAdapter.startJogForward(groupLetter, role)
                    else commandAdapter.stopJog(groupLetter, role)
                }
            }
            Button {
                text: "反转"
                enabled: canJog && canSubmit
                // ★ PLC 状态回显：PLC 反馈正在反向点动(motionState==4) → 按钮显示按下感。
                highlighted: jogBackwardActive
                onPressedChanged: {
                    if (pressed) commandAdapter.startJogBackward(groupLetter, role)
                    else commandAdapter.stopJog(groupLetter, role)
                }
            }
            Button {
                text: "停止运动"
                enabled: canSubmit   // 停止不受普通锁定限制
                onClicked: commandAdapter.stopMotion(groupLetter, role)
            }
        }

        // ---------- 急停 / 解除急停 ----------
        RowLayout {
            spacing: 8 * Theme.scale
            Button {
                text: "软件急停"
                enabled: canSubmit
                onClicked: commandAdapter.triggerEmergencyStop()
            }
            Button {
                text: "解除急停"
                enabled: canSubmit
                onClicked: commandAdapter.requestEmergencyStopRelease()
            }
            Text {
                text: "全局锁定:" + (globallyLocked ? "ON" : "OFF")
                color: globallyLocked ? Theme.colorWarning : Theme.colorIdle
                font.pixelSize: Theme.fontSmall
            }
        }

        // ---------- 最近操作回显 ----------
        Text {
            id: lastOpText
            text: "最近操作: -"
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Connections {
            target: snapshotAdapter
            function onStateChanged() {
                if (!snapshotAdapter) { lastOpText.text = "最近操作: -"; return; }
                var ops = snapshotAdapter.operations;
                for (var i = ops.length - 1; i >= 0; --i) {
                    if (ops[i].source === "UI") {
                        lastOpText.text = "最近操作: " + ops[i].axis + " -> " + ops[i].state
                                          + (ops[i].diag ? "  " + ops[i].diag : "");
                        return;
                    }
                }
                lastOpText.text = "最近操作: -";
            }
        }
    }
}
