import QtQuick
import QtQuick.Layouts
import servoV6 // 引入 Theme 单例

Rectangle {
    id: root
    
    // === 核心接口：接收外部注入的 ViewModel ===
    property var viewModel: null

    // 默认背景和边框
    color: Theme.panelBg
    radius: 12 * Theme.scale
    border.color: Theme.borderMain
    border.width: 2 * Theme.scale

    // --- 状态解析函数 (映射 AxisState 枚举) ---
    function getStateText(stateCode) {
        if (!viewModel) return "未连接";
        switch(stateCode) {
            case 1: return "DISABLED (断电)";
            case 2: return "IDLE (就绪)";
            case 3: return "JOGGING (点动中)";
            case 4: return "MOVING ABS (绝对定位)";
            case 6: return "ERROR (故障)";
            default: return "UNKNOWN (未知)";
        }
    }

    function getStateColor(stateCode) {
        if (!viewModel) return Theme.colorDisabled;
        switch(stateCode) {
            case 1: return Theme.colorDisabled;
            case 2: return Theme.colorIdle;
            case 3: 
            case 4: return Theme.colorMoving;
            case 6: return Theme.colorError;
            default: return Theme.textDim;
        }
    }

    // --- UI 布局 ---
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 30 * Theme.scale
        spacing: 20 * Theme.scale

        // 1. 顶部标题栏
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "实时运动数据看板"
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
                font.bold: true
            }
            Item { Layout.fillWidth: true } // 弹簧，把后面的元素挤到最右边
            
            // 状态指示灯模块
            Rectangle {
                width: 14 * Theme.scale
                height: 14 * Theme.scale
                radius: width / 2
                // 呼吸灯效果：跟着状态变色
                color: getStateColor(viewModel ? viewModel.state : 0)
                // 加一个发光外圈
                border.color: Qt.lighter(color, 1.5)
                border.width: 1
            }
            Text {
                text: getStateText(viewModel ? viewModel.state : 0)
                color: getStateColor(viewModel ? viewModel.state : 0)
                font.pixelSize: Theme.fontNormal
                font.bold: true
            }
        }

        Item { Layout.fillHeight: true } // 顶部垂直弹簧

        // 2. 核心大数字区 (当前绝对位置)
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
                // 如果 viewModel 存在，显示三位小数；否则显示 0.000
                text: viewModel ? viewModel.absPos.toFixed(3) : "0.000"
                color: Theme.textMain
                font.pixelSize: Theme.fontGiant * 1.5 // 极其巨大的字体
                font.family: "Monospace" // 使用等宽字体，数字跳动时界面不会左右抖动
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }
        }


        // 🌟 限位动态位置条 (Position Bar)
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 20 * Theme.scale
            spacing: 5 * Theme.scale

            // 进度条轨道 (作为属性计算的宿主)
            Rectangle {
                id: trackBar
                Layout.fillWidth: true
                height: 8 * Theme.scale
                radius: height / 2
                color: Theme.bgDark
                border.color: Theme.borderMain
                border.width: 1

                // 🌟 将安全计算直接声明为 readonly property，QML 引擎会自动追踪它们的依赖并实时刷新
                readonly property double safePos: viewModel ? viewModel.absPos : 0.0
                readonly property double safePLim: (viewModel && viewModel.posLimit < 999999) ? viewModel.posLimit : 1000.0
                readonly property double safeNLim: (viewModel && viewModel.negLimit > -999999) ? viewModel.negLimit : -1000.0
                
                // 核心进度比例计算 (限制在 0.0 ~ 1.0 之间)
                readonly property double progressRatio: {
                    let range = safePLim - safeNLim;
                    if (range <= 0) return 0.5;
                    return Math.max(0.0, Math.min(1.0, (safePos - safeNLim) / range));
                }

                // 填充条
                Rectangle {
                    width: parent.width * parent.progressRatio // 👈 直接使用宿主计算好的比例
                    height: parent.height
                    radius: parent.radius
                    color: Theme.colorMoving 
                    
                    // 加个小动画，让跳动更丝滑
                    Behavior on width {
                        NumberAnimation { duration: 100; easing.type: Easing.OutQuad }
                    }
                }
                
                // 当前位置指示游标
                Rectangle {
                    x: parent.width * parent.progressRatio - width / 2 // 👈 直接使用宿主计算好的比例
                    y: -4 * Theme.scale
                    width: 4 * Theme.scale
                    height: 16 * Theme.scale
                    color: Theme.textMain
                    radius: 2
                }
            }

            // 限位刻度文字
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

        Item { Layout.fillHeight: true } // 底部垂直弹簧
    }
}