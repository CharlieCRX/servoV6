import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

/**
 * GantryControlBlock.qml
 * 龙门控制面板 — 耦合/解耦 + Jog 操作 (联动/分动)
 *
 * 绑定到 QtGantryViewModel 的 Q_PROPERTY
 */
Rectangle {
    id: root
    color: "#1a1a2e"
    border.color: "#2a2a4e"
    border.width: 1
    radius: 6

    // ── 外部注入属性 ──
    property var gantryVM: null
    property string groupName: "Gantry 1"

    implicitHeight: gantryCol.implicitHeight + 20
    implicitWidth: 420

    ColumnLayout {
        id: gantryCol
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // ── 标题栏 ──
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: root.groupName
                color: "#e0e0e0"
                font.pixelSize: 14
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            // 模式指示器
            Rectangle {
                width: 12; height: 12; radius: 6
                color: root.gantryVM && root.gantryVM.coupled ? "#4caf50" : "#ff9800"
            }
            Text {
                text: root.gantryVM && root.gantryVM.coupled ? "联动" : "分动"
                color: "#c0c0c0"
                font.pixelSize: 12
            }
        }

        // ── 耦合/解耦按钮 ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            IndustrialButton {
                text: "耦合 (Couple)"
                enabled: root.gantryVM && root.gantryVM.canCouple
                baseColor: "#2e7d32"
                Layout.preferredWidth: 130
                onClicked: {
                    if (root.gantryVM) root.gantryVM.requestCoupling()
                }
            }
            IndustrialButton {
                text: "解耦 (Decouple)"
                enabled: root.gantryVM && root.gantryVM.coupled
                baseColor: "#e65100"
                Layout.preferredWidth: 130
                onClicked: {
                    if (root.gantryVM) root.gantryVM.requestDecoupling("UI manual")
                }
            }
            Item { Layout.fillWidth: true }
            IndustrialButton {
                text: "急停"
                baseColor: "#c62828"
                Layout.preferredWidth: 80
                onClicked: {
                    if (root.gantryVM) root.gantryVM.stop()
                }
            }
        }

        // ── 分隔线 ──
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#3a3a5e"
        }

        // ── 分动 Jog (X1 / X2) ──
        Text {
            text: "分动 Jog (X1 / X2)"
            color: "#888"
            font.pixelSize: 11
        }

        // X1 Jog 行
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text { text: "X1"; color: "#64b5f6"; font.pixelSize: 12; Layout.preferredWidth: 30 }
            IndustrialButton {
                text: "◀"
                enabled: root.gantryVM && !root.gantryVM.isAnyLimit
                baseColor: "#1565c0"
                Layout.preferredWidth: 50
                onPressed: { if (root.gantryVM) root.gantryVM.jogX1ReversePressed() }
                onReleased: { if (root.gantryVM) root.gantryVM.stop() }
            }
            Text {
                text: root.gantryVM ? root.gantryVM.x1Position.toFixed(2) + " mm" : "---"
                color: "#e0e0e0"
                font.pixelSize: 12
                Layout.preferredWidth: 90
                horizontalAlignment: Text.AlignHCenter
            }
            IndustrialButton {
                text: "▶"
                enabled: root.gantryVM && !root.gantryVM.isAnyLimit
                baseColor: "#1565c0"
                Layout.preferredWidth: 50
                onPressed: { if (root.gantryVM) root.gantryVM.jogX1ForwardPressed() }
                onReleased: { if (root.gantryVM) root.gantryVM.stop() }
            }
            // X1 限位灯
            Rectangle {
                width: 10; height: 10; radius: 5
                color: root.gantryVM && (root.gantryVM.x1PosLimit || root.gantryVM.x1NegLimit) ? "#ff5252" : "#333"
                Layout.leftMargin: 8
            }
        }

        // X2 Jog 行
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text { text: "X2"; color: "#64b5f6"; font.pixelSize: 12; Layout.preferredWidth: 30 }
            IndustrialButton {
                text: "◀"
                enabled: root.gantryVM && !root.gantryVM.isAnyLimit
                baseColor: "#1565c0"
                Layout.preferredWidth: 50
                onPressed: { if (root.gantryVM) root.gantryVM.jogX2ReversePressed() }
                onReleased: { if (root.gantryVM) root.gantryVM.stop() }
            }
            Text {
                text: root.gantryVM ? root.gantryVM.x2Position.toFixed(2) + " mm" : "---"
                color: "#e0e0e0"
                font.pixelSize: 12
                Layout.preferredWidth: 90
                horizontalAlignment: Text.AlignHCenter
            }
            IndustrialButton {
                text: "▶"
                enabled: root.gantryVM && !root.gantryVM.isAnyLimit
                baseColor: "#1565c0"
                Layout.preferredWidth: 50
                onPressed: { if (root.gantryVM) root.gantryVM.jogX2ForwardPressed() }
                onReleased: { if (root.gantryVM) root.gantryVM.stop() }
            }
            // X2 限位灯
            Rectangle {
                width: 10; height: 10; radius: 5
                color: root.gantryVM && (root.gantryVM.x2PosLimit || root.gantryVM.x2NegLimit) ? "#ff5252" : "#333"
                Layout.leftMargin: 8
            }
        }

        // ── 分隔线 ──
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#3a3a5e"
        }

        // ── 联动 Jog (逻辑轴 X) ──
        Text {
            text: "联动 Jog (逻辑轴 X) — 仅耦合模式"
            color: root.gantryVM && root.gantryVM.coupled ? "#888" : "#555"
            font.pixelSize: 11
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            IndustrialButton {
                text: "◀"
                enabled: root.gantryVM && root.gantryVM.coupled && !root.gantryVM.isAnyLimit
                baseColor: root.gantryVM && root.gantryVM.coupled ? "#2e7d32" : "#444"
                Layout.preferredWidth: 50
                onPressed: { if (root.gantryVM) root.gantryVM.jogCoupledReversePressed() }
                onReleased: { if (root.gantryVM) root.gantryVM.jogCoupledReleased() }
            }
            Text {
                text: root.gantryVM ? root.gantryVM.position.toFixed(2) + " mm" : "---"
                color: root.gantryVM && root.gantryVM.coupled ? "#a5d6a7" : "#555"
                font.pixelSize: 13
                font.bold: root.gantryVM && root.gantryVM.coupled
                Layout.preferredWidth: 100
                horizontalAlignment: Text.AlignHCenter
            }
            IndustrialButton {
                text: "▶"
                enabled: root.gantryVM && root.gantryVM.coupled && !root.gantryVM.isAnyLimit
                baseColor: root.gantryVM && root.gantryVM.coupled ? "#2e7d32" : "#444"
                Layout.preferredWidth: 50
                onPressed: { if (root.gantryVM) root.gantryVM.jogCoupledForwardPressed() }
                onReleased: { if (root.gantryVM) root.gantryVM.jogCoupledReleased() }
            }
        }

        // ── 分隔线 ──
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#3a3a5e"
        }

        // ── Move Absolute 控制区 ──
        Text {
            text: "定点移动 (Move Absolute)"
            color: "#888"
            font.pixelSize: 11
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            // 联动 MoveAbs
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Text { text: "联动"; color: "#81c784"; font.pixelSize: 11; Layout.preferredWidth: 35 }
                TextField {
                    id: absTargetField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    color: "#e0e0e0"
                    font.pixelSize: 11
                    placeholderText: "目标位置 mm"
                    placeholderTextColor: "#555"
                    background: Rectangle { color: "#1a1a2e"; border.color: "#3a3a5e"; border.width: 1; radius: 3 }
                    text: "0.0"
                }
                IndustrialButton {
                    text: "Go"
                    enabled: root.gantryVM && root.gantryVM.coupled && !root.gantryVM.isAnyLimit
                    baseColor: root.gantryVM && root.gantryVM.coupled ? "#2e7d32" : "#444"
                    Layout.preferredWidth: 50
                    onClicked: {
                        if (root.gantryVM) root.gantryVM.moveCoupledTo(absTargetField.text)
                    }
                }
            }

            // 分动 MoveAbs X1/X2
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Text { text: "X1"; color: "#64b5f6"; font.pixelSize: 11; Layout.preferredWidth: 35 }
                TextField {
                    id: x1AbsField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    color: "#e0e0e0"
                    font.pixelSize: 11
                    placeholderText: "目标位置 mm"
                    placeholderTextColor: "#555"
                    background: Rectangle { color: "#1a1a2e"; border.color: "#3a3a5e"; border.width: 1; radius: 3 }
                    text: "0.0"
                }
                IndustrialButton {
                    text: "Go"
                    enabled: root.gantryVM && !root.gantryVM.coupled && !root.gantryVM.isAnyLimit
                    baseColor: root.gantryVM && !root.gantryVM.coupled ? "#1565c0" : "#444"
                    Layout.preferredWidth: 50
                    onClicked: {
                        if (root.gantryVM) root.gantryVM.moveX1To(x1AbsField.text)
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Text { text: "X2"; color: "#64b5f6"; font.pixelSize: 11; Layout.preferredWidth: 35 }
                TextField {
                    id: x2AbsField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    color: "#e0e0e0"
                    font.pixelSize: 11
                    placeholderText: "目标位置 mm"
                    placeholderTextColor: "#555"
                    background: Rectangle { color: "#1a1a2e"; border.color: "#3a3a5e"; border.width: 1; radius: 3 }
                    text: "0.0"
                }
                IndustrialButton {
                    text: "Go"
                    enabled: root.gantryVM && !root.gantryVM.coupled && !root.gantryVM.isAnyLimit
                    baseColor: root.gantryVM && !root.gantryVM.coupled ? "#1565c0" : "#444"
                    Layout.preferredWidth: 50
                    onClicked: {
                        if (root.gantryVM) root.gantryVM.moveX2To(x2AbsField.text)
                    }
                }
            }
        }

        // ── 分隔线 ──
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#3a3a5e"
        }

        // ── Move Relative 控制区 ──
        Text {
            text: "增量移动 (Move Relative)"
            color: "#888"
            font.pixelSize: 11
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Text { text: "联动"; color: "#81c784"; font.pixelSize: 11; Layout.preferredWidth: 35 }
                TextField {
                    id: relDeltaField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    color: "#e0e0e0"
                    font.pixelSize: 11
                    placeholderText: "增量 mm"
                    placeholderTextColor: "#555"
                    background: Rectangle { color: "#1a1a2e"; border.color: "#3a3a5e"; border.width: 1; radius: 3 }
                    text: "10.0"
                }
                IndustrialButton {
                    text: "▶"
                    enabled: root.gantryVM && root.gantryVM.coupled && !root.gantryVM.isAnyLimit
                    baseColor: root.gantryVM && root.gantryVM.coupled ? "#2e7d32" : "#444"
                    Layout.preferredWidth: 40
                    onClicked: {
                        if (root.gantryVM) root.gantryVM.moveCoupledRel(relDeltaField.text)
                    }
                }
                IndustrialButton {
                    text: "◀"
                    enabled: root.gantryVM && root.gantryVM.coupled && !root.gantryVM.isAnyLimit
                    baseColor: root.gantryVM && root.gantryVM.coupled ? "#2e7d32" : "#444"
                    Layout.preferredWidth: 40
                    onClicked: {
                        if (root.gantryVM) root.gantryVM.moveCoupledRel("-" + relDeltaField.text)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Text { text: "X1"; color: "#64b5f6"; font.pixelSize: 11; Layout.preferredWidth: 35 }
                TextField {
                    id: x1RelField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    color: "#e0e0e0"
                    font.pixelSize: 11
                    placeholderText: "增量 mm"
                    placeholderTextColor: "#555"
                    background: Rectangle { color: "#1a1a2e"; border.color: "#3a3a5e"; border.width: 1; radius: 3 }
                    text: "10.0"
                }
                IndustrialButton {
                    text: "▶"
                    enabled: root.gantryVM && !root.gantryVM.coupled && !root.gantryVM.isAnyLimit
                    baseColor: root.gantryVM && !root.gantryVM.coupled ? "#1565c0" : "#444"
                    Layout.preferredWidth: 40
                    onClicked: {
                        if (root.gantryVM) root.gantryVM.moveX1Rel(x1RelField.text)
                    }
                }
                IndustrialButton {
                    text: "◀"
                    enabled: root.gantryVM && !root.gantryVM.coupled && !root.gantryVM.isAnyLimit
                    baseColor: root.gantryVM && !root.gantryVM.coupled ? "#1565c0" : "#444"
                    Layout.preferredWidth: 40
                    onClicked: {
                        if (root.gantryVM) root.gantryVM.moveX1Rel("-" + x1RelField.text)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Text { text: "X2"; color: "#64b5f6"; font.pixelSize: 11; Layout.preferredWidth: 35 }
                TextField {
                    id: x2RelField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    color: "#e0e0e0"
                    font.pixelSize: 11
                    placeholderText: "增量 mm"
                    placeholderTextColor: "#555"
                    background: Rectangle { color: "#1a1a2e"; border.color: "#3a3a5e"; border.width: 1; radius: 3 }
                    text: "10.0"
                }
                IndustrialButton {
                    text: "▶"
                    enabled: root.gantryVM && !root.gantryVM.coupled && !root.gantryVM.isAnyLimit
                    baseColor: root.gantryVM && !root.gantryVM.coupled ? "#1565c0" : "#444"
                    Layout.preferredWidth: 40
                    onClicked: {
                        if (root.gantryVM) root.gantryVM.moveX2Rel(x2RelField.text)
                    }
                }
                IndustrialButton {
                    text: "◀"
                    enabled: root.gantryVM && !root.gantryVM.coupled && !root.gantryVM.isAnyLimit
                    baseColor: root.gantryVM && !root.gantryVM.coupled ? "#1565c0" : "#444"
                    Layout.preferredWidth: 40
                    onClicked: {
                        if (root.gantryVM) root.gantryVM.moveX2Rel("-" + x2RelField.text)
                    }
                }
            }
        }

        // ── 分隔线 ──
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#3a3a5e"
        }

        // ── Jog 速度设置 ──
        Text {
            text: "Jog 速度"
            color: "#888"
            font.pixelSize: 11
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Text { text: "当前:"; color: "#aaa"; font.pixelSize: 11 }
            Text {
                text: root.gantryVM ? root.gantryVM.jogVelocity.toFixed(1) + " mm/s" : "---"
                color: "#ffab40"
                font.pixelSize: 12
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            IndustrialButton {
                text: "−"
                baseColor: "#e65100"
                Layout.preferredWidth: 35
                onClicked: {
                    if (root.gantryVM) root.gantryVM.adjustJogVelocity(-5.0)
                }
            }
            IndustrialButton {
                text: "+"
                baseColor: "#2e7d32"
                Layout.preferredWidth: 35
                onClicked: {
                    if (root.gantryVM) root.gantryVM.adjustJogVelocity(5.0)
                }
            }
        }
        Slider {
            id: jogVelSlider
            Layout.fillWidth: true
            from: 1.0; to: 100.0
            value: root.gantryVM ? root.gantryVM.jogVelocity : 15.0
            onValueChanged: {
                if (root.gantryVM) root.gantryVM.setJogVelocity(value)
            }
            background: Rectangle {
                x: jogVelSlider.leftPadding
                y: jogVelSlider.topPadding + jogVelSlider.availableHeight / 2 - height / 2
                implicitWidth: 200; implicitHeight: 4
                width: jogVelSlider.availableWidth; height: implicitHeight
                radius: 2
                color: "#3a3a5e"
                Rectangle {
                    width: jogVelSlider.visualPosition * parent.width
                    height: parent.height
                    color: "#ff9800"
                    radius: 2
                }
            }
            handle: Rectangle {
                x: jogVelSlider.leftPadding + jogVelSlider.visualPosition * (jogVelSlider.availableWidth - width)
                y: jogVelSlider.topPadding + jogVelSlider.availableHeight / 2 - height / 2
                implicitWidth: 14; implicitHeight: 14
                radius: 7
                color: "#ff9800"
                border.color: "#ffab40"
                border.width: 2
            }
        }

        // ── 状态信息 ──
        Text {
            text: root.gantryVM ? root.gantryVM.stateDescription : "---"
            color: "#aaa"
            font.pixelSize: 11
            Layout.fillWidth: true
        }

        // ── 命令结果反馈 ──
        Text {
            text: root.gantryVM ? root.gantryVM.lastCommandResult : ""
            color: {
                if (!root.gantryVM || !root.gantryVM.lastCommandResult) return "#555"
                return root.gantryVM.lastCommandResult.indexOf("Rejected") >= 0 ? "#ff5252" : "#81c784"
            }
            font.pixelSize: 10
            Layout.fillWidth: true
            visible: root.gantryVM && root.gantryVM.lastCommandResult !== ""
        }
    }

    // ── 速度调节函数 (供外部调用) ──
    function updateJogVelocity(delta) {
        if (root.gantryVM) root.gantryVM.adjustJogVelocity(delta)
    }
}
