import QtQuick
import QtQuick.Layouts
import servoV6

Rectangle {
    id: root

    // === 核心接口：接收外部注入的 ViewModel ===
    property var viewModel: null

    color: Theme.panelBg
    radius: 12 * Theme.scale
    border.color: Theme.borderMain
    border.width: 2 * Theme.scale

    // --- 状态颜色函数（保留用于指示灯）---
    function getStateColor(stateCode) {
        if (!viewModel) return Theme.colorDisabled;
        switch(stateCode) {
            case 1: return Theme.colorDisabled;   // Disabled
            case 2: return Theme.colorIdle;        // Idle / Standstill
            case 3:
            case 4: return Theme.colorMoving;      // Jogging / Moving
            case 6: return Theme.colorError;       // Error
            default: return Theme.textDim;
        }
    }

    // --- UI 布局 ---
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24 * Theme.scale
        spacing: 12 * Theme.scale

        // 1. 顶部标题栏
        RowLayout {
            Layout.fillWidth: true

            Text {
                text: "实时运动数据看板"
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
                font.bold: true
            }
            Item { Layout.fillWidth: true }

            // 使能指示灯（P2: 新增 isEnabled 徽标）
            RowLayout {
                spacing: 4 * Theme.scale
                Rectangle {
                    width: 10 * Theme.scale
                    height: 10 * Theme.scale
                    radius: width / 2
                    color: viewModel && viewModel.isEnabled ? Theme.colorIdle : Theme.colorDisabled
                }
                Text {
                    text: viewModel && viewModel.isEnabled ? "已使能" : "未使能"
                    color: viewModel && viewModel.isEnabled ? Theme.colorIdle : Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }
            }

            Item { width: 12 * Theme.scale }

            // 状态指示灯 + 文本（P2: 使用 viewModel.stateText 替代硬编码）
            Rectangle {
                width: 14 * Theme.scale
                height: 14 * Theme.scale
                radius: width / 2
                color: getStateColor(viewModel ? viewModel.state : 0)
                border.color: Qt.lighter(color, 1.5)
                border.width: 1
            }
            Text {
                text: viewModel ? viewModel.stateText : "—"
                color: getStateColor(viewModel ? viewModel.state : 0)
                font.pixelSize: Theme.fontNormal
                font.bold: true
            }
        }

        Item { Layout.fillHeight: true }

        // 2. 核心大数字区 — 绝对位置
        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 5

            Text {
                text: "当前绝对位置 (mm)"
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                Layout.alignment: Qt.AlignHCenter
            }

            Text {
                text: viewModel ? viewModel.absPos.toFixed(3) : "0.000"
                color: Theme.textMain
                font.pixelSize: Theme.fontGiant * 1.5
                font.family: "Monospace"
                font.bold: true
                fontSizeMode: Text.Fit
                minimumPixelSize: Theme.fontNormal
                Layout.fillWidth: true
                Layout.maximumWidth: parent.width
                horizontalAlignment: Text.AlignHCenter
            }
        }

        // 3. 相对位置（P2: 新增）
        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 2

            Text {
                text: "相对位置 (mm)"
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                Layout.alignment: Qt.AlignHCenter
            }

            Text {
                text: {
                    if (!viewModel) return "0.000"
                    let r = viewModel.relPos
                    return (r >= 0 ? "+" : "") + r.toFixed(3)
                }
                color: Theme.textMain
                font.pixelSize: Theme.fontLarge
                font.family: "Monospace"
                Layout.alignment: Qt.AlignHCenter
            }
        }

        // 4. 限位进度条
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 12 * Theme.scale
            spacing: 5 * Theme.scale

            Rectangle {
                id: trackBar
                Layout.fillWidth: true
                height: 8 * Theme.scale
                radius: height / 2
                color: Theme.bgDark
                border.color: Theme.borderMain
                border.width: 1

                readonly property double safePos: viewModel ? viewModel.absPos : 0.0
                readonly property double safePLim: (viewModel && viewModel.posLimit < 999999) ? viewModel.posLimit : 1000.0
                readonly property double safeNLim: (viewModel && viewModel.negLimit > -999999) ? viewModel.negLimit : -1000.0

                readonly property double progressRatio: {
                    let range = safePLim - safeNLim;
                    if (range <= 0) return 0.5;
                    return Math.max(0.0, Math.min(1.0, (safePos - safeNLim) / range));
                }

                Rectangle {
                    width: parent.width * parent.progressRatio
                    height: parent.height
                    radius: parent.radius
                    color: Theme.colorMoving
                    Behavior on width {
                        NumberAnimation { duration: 100; easing.type: Easing.OutQuad }
                    }
                }

                Rectangle {
                    x: parent.width * parent.progressRatio - width / 2
                    y: -4 * Theme.scale
                    width: 4 * Theme.scale
                    height: 16 * Theme.scale
                    color: Theme.textMain
                    radius: 2
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: viewModel && viewModel.negLimit > -999999 ? "负限位: " + viewModel.negLimit : "负限位: 未设"
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: viewModel && viewModel.posLimit < 999999 ? "正限位: " + viewModel.posLimit : "正限位: 未设"
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                }
            }
        }

        // 5. 零位操作区（P4: 新增）
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 12 * Theme.scale
            height: 36 * Theme.scale
            color: "transparent"

            RowLayout {
                anchors.centerIn: parent
                spacing: 10 * Theme.scale

                IndustrialButton {
                    text: "⚡ 清零"
                    buttonSize: 90 * Theme.scale
                    baseColor: Theme.panelBg
                    border.color: Theme.borderMain
                    border.width: 1
                    onClicked: {
                        if (viewModel) viewModel.zeroAbsolutePosition()
                    }
                }

                IndustrialButton {
                    text: "⊙ 设零"
                    buttonSize: 90 * Theme.scale
                    baseColor: Theme.panelBg
                    border.color: Theme.borderMain
                    border.width: 1
                    onClicked: {
                        if (viewModel) viewModel.setRelativeZero()
                    }
                }

                IndustrialButton {
                    text: "⊗ 清除"
                    buttonSize: 90 * Theme.scale
                    baseColor: Theme.panelBg
                    border.color: Theme.borderMain
                    border.width: 1
                    onClicked: {
                        if (viewModel) viewModel.clearRelativeZero()
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
