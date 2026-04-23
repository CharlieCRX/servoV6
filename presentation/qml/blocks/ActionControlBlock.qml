import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import servoV6
import "qrc:/servoV6/presentation/qml/components" // 确保能找到你的原子组件

Rectangle {
    id: root
    property var viewModel: null

    // 内部状态：0 = 点动模式 (Jog), 1 = 定位模式 (Position)
    property int currentMode: 0 
    // 定位模式下的子状态：true = 绝对, false = 相对
    property bool isAbsolute: true 

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
        // 2. 中间：动态控制面板 (使用 StackLayout 或简单的 visible 控制)
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

                // 绝对/相对 单选区
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 15 * Theme.scale

                    RadioButton {
                        text: "绝对 (Abs)"
                        checked: root.isAbsolute
                        onClicked: root.isAbsolute = true
                        // 覆盖默认样式，适配深色主题
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
                        enabled: false // 🚦 架构师锁定：底层还没实现，先置灰！
                        onClicked: root.isAbsolute = false
                        contentItem: Text {
                            text: parent.text
                            color: parent.enabled ? Theme.textMain : Theme.textDim
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
                    font.pixelSize: Theme.fontLarge
                    font.family: "Monospace"
                    color: Theme.textMain
                    horizontalAlignment: TextInput.AlignHCenter
                    
                    // 工业风输入框样式
                    background: Rectangle {
                        color: Theme.bgDark
                        border.color: targetInput.activeFocus ? Theme.colorMoving : Theme.borderMain
                        border.width: 2
                        radius: 6 * Theme.scale
                    }
                    
                    // 仅允许输入数字和小数点
                    validator: DoubleValidator { bottom: -9999.9; top: 9999.9; decimals: 2 }
                }

                // 执行按钮 (GO)
                IndustrialButton {
                    text: "执行 GO"
                    isCircle: false
                    buttonSize: 140 * Theme.scale
                    baseColor: Theme.colorIdle // 绿色底色代表自动执行
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: {
                        let target = parseFloat(targetInput.text)
                        if (!isNaN(target) && viewModel) {
                            // 由于目前禁用了相对移动，这里只调用 moveAbsolute
                            if (root.isAbsolute) {
                                viewModel.moveAbsolute(target)
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

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
}