import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import servoV6
import "qrc:/servoV6/presentation/qml/components" 

Rectangle {
    id: root
    property var viewModel: null

    // 内部状态：0 = 点动模式 (Jog), 1 = 定位模式 (Position)
    property int currentMode: 0 
    // 定位模式下的子状态：true = 绝对, false = 相对
    property bool isAbsolute: true 

    // 🌟 核心防呆：动态判定当前是否允许下发定位指令 (1: Disabled, 2: Idle)
    property bool isReadyForPos: viewModel ? (viewModel.state === 1 || viewModel.state === 2) : false

    color: "transparent"

    ColumnLayout {
        anchors.fill: parent
        spacing: 20 * Theme.scale

        // ==========================================
        // 1. 顶部：模式切换器 (Mode Switcher)
        // ==========================================
        Rectangle {
            Layout.fillWidth: true
            height: 40 * Theme.scale
            radius: 8 * Theme.scale
            color: Theme.bgDark
            border.color: Theme.borderMain
            border.width: 1

            RowLayout {
                anchors.fill: parent
                spacing: 0

                // 点动模式 Tab
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.currentMode === 0 ? Theme.panelBg : "transparent"
                    radius: 8 * Theme.scale
                    Text {
                        anchors.centerIn: parent
                        text: "点动 (JOG)"
                        color: root.currentMode === 0 ? Theme.textMain : Theme.textDim
                        font.bold: root.currentMode === 0
                        font.pixelSize: Theme.fontNormal
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.currentMode = 0
                    }
                }

                // 定位模式 Tab
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.currentMode === 1 ? Theme.panelBg : "transparent"
                    radius: 8 * Theme.scale
                    Text {
                        anchors.centerIn: parent
                        text: "定位 (POS)"
                        color: root.currentMode === 1 ? Theme.textMain : Theme.textDim
                        font.bold: root.currentMode === 1
                        font.pixelSize: Theme.fontNormal
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.currentMode = 1
                    }
                }
            }
        }

        // ==========================================
        // 2. 中间：动态控制面板
        // ==========================================
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // --- A. 点动控制面板 ---
            ColumnLayout {
                anchors.fill: parent
                spacing: 20 * Theme.scale
                visible: root.currentMode === 0

                Item { Layout.fillHeight: true } // 顶部弹簧
                
                // 🌟 新增：点动速度设定
                // 🌟 只读展示 + 设置按钮
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 10 * Theme.scale
                    
                    Text { 
                        text: "点动速度: " + (viewModel ? viewModel.jogVelocity.toFixed(1) : "0.0") + " mm/s"
                        color: Theme.textMain
                        font.pixelSize: Theme.fontNormal
                        font.family: "Monospace"
                    }
                    
                    IndustrialButton {
                        text: "⚙️" // 设置图标
                        buttonSize: 30 * Theme.scale
                        isCircle: true
                        baseColor: Theme.panelBg
                        onClicked: velocityPopup.open() // 👈 点击呼出全局弹窗
                    }
                }

                IndustrialButton {
                    text: "JOG +"
                    Layout.alignment: Qt.AlignHCenter
                    onPressed: if(viewModel) viewModel.jogPositivePressed()
                    onReleased: if(viewModel) viewModel.jogPositiveReleased()
                }

                IndustrialButton {
                    text: "JOG -"
                    Layout.alignment: Qt.AlignHCenter
                    onPressed: if(viewModel) viewModel.jogNegativePressed()
                    onReleased: if(viewModel) viewModel.jogNegativeReleased()
                }

                Item { Layout.fillHeight: true } // 底部弹簧
            }

            // --- B. 定位控制面板 ---
            ColumnLayout {
                anchors.fill: parent
                spacing: 20 * Theme.scale
                visible: root.currentMode === 1

                Item { Layout.fillHeight: true }
                
                // 🌟 新增：定位速度设定
                // 🌟 只读展示 + 设置按钮
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 10 * Theme.scale
                    
                    Text { 
                        text: "定位速度: " + (viewModel ? viewModel.moveVelocity.toFixed(1) : "0.0") + " mm/s"
                        color: Theme.textMain
                        font.pixelSize: Theme.fontNormal
                        font.family: "Monospace"
                    }
                    
                    IndustrialButton {
                        text: "⚙️"
                        buttonSize: 30 * Theme.scale
                        isCircle: true
                        baseColor: Theme.panelBg
                        onClicked: velocityPopup.open() // 👈 点击呼出同一个弹窗
                    }
                }

                // 绝对/相对 单选区
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 8 * Theme.scale 

                    RadioButton {
                        text: "绝对 (Abs)"
                        checked: root.isAbsolute
                        enabled: root.isReadyForPos  // ✅ 只有 1 个 enabled 属性
                        opacity: enabled ? 1.0 : 0.5 
                        onClicked: root.isAbsolute = true
                        contentItem: Text {
                            text: parent.text
                            color: Theme.textMain
                            font.pixelSize: Theme.fontNormal
                            leftPadding: parent.indicator.width + parent.spacing
                        }
                    }
                    
                    RadioButton {
                        text: "相对 (Rel)"
                        checked: !root.isAbsolute
                        enabled: root.isReadyForPos  // ✅ 只有 1 个 enabled 属性
                        opacity: enabled ? 1.0 : 0.5 
                        onClicked: root.isAbsolute = false
                        contentItem: Text {
                            text: parent.text
                            color: Theme.textMain
                            font.pixelSize: Theme.fontNormal
                            leftPadding: parent.indicator.width + parent.spacing
                        }
                    }
                }

                // 目标值输入框
                TextField {
                    id: targetInput
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 140 * Theme.scale
                    text: "100.0"
                    
                    enabled: root.isReadyForPos  // ✅ 只有 1 个 enabled 属性
                    opacity: enabled ? 1.0 : 0.5 
                    
                    font.pixelSize: Theme.fontLarge
                    font.family: "Monospace"
                    color: Theme.textMain
                    horizontalAlignment: TextInput.AlignHCenter
                    
                    background: Rectangle {
                        color: Theme.bgDark
                        border.color: targetInput.activeFocus ? Theme.colorMoving : Theme.borderMain
                        border.width: 2
                        radius: 6 * Theme.scale
                    }
                    validator: DoubleValidator { bottom: -9999.9; top: 9999.9; decimals: 2 }
                }

                // 执行按钮 (GO)
                IndustrialButton {
                    text: root.isReadyForPos ? "执行 GO" : "运行中..."
                    isCircle: false
                    buttonSize: 140 * Theme.scale
                    
                    enabled: root.isReadyForPos  // ✅ 只有 1 个 enabled 属性
                    baseColor: root.isReadyForPos ? Theme.colorIdle : Theme.colorDisabled 
                    
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: {
                        let target = parseFloat(targetInput.text)
                        if (!isNaN(target) && viewModel) {
                            if (root.isAbsolute) {
                                viewModel.moveAbsolute(target)
                            } else {
                                viewModel.moveRelative(target) 
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true } // 底部弹簧
            }
        } // 结束 Item (中间控制面板)

        // ==========================================
        // 3. 底部：全局急停 (STOP)
        // ==========================================
        IndustrialButton {
            text: "急 停"
            isCircle: false
            buttonSize: 140 * Theme.scale
            baseColor: Theme.colorError
            activeColor: "#FF8A80" // 按下时更亮的红色
            Layout.alignment: Qt.AlignHCenter
            onClicked: if(viewModel) viewModel.stop()
        }
    }

    VelocitySettingsPopup {
        id: velocityPopup
        viewModel: root.viewModel
    }
}