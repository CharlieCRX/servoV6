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

        Item { Layout.fillHeight: true } // 底部垂直弹簧
    }
}