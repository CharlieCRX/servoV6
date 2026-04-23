import QtQuick
import QtQuick.Controls
import servoV6 // 引入 Theme 单例

Rectangle {
    id: root
    
    // --- 开放的属性接口 ---
    property string text: "BUTTON"
    property color baseColor: "transparent"
    property color activeColor: Theme.textMain // 按下时的颜色，默认纯白
    property bool isCircle: true // 默认为圆形大按钮
    property int buttonSize: 120 * Theme.scale // 默认尺寸
    
    // --- 开放的信号接口 ---
    signal clicked()
    signal pressed()
    signal released()

    // --- 内部样式 ---
    width: buttonSize
    height: isCircle ? buttonSize : buttonSize * 0.4
    radius: isCircle ? width / 2 : 8 * Theme.scale
    border.color: Theme.borderMain
    border.width: 3 * Theme.scale

    // 背景色状态机：按下 -> Hover -> 默认
    color: mouseArea.pressed ? activeColor : 
           (mouseArea.containsMouse ? Qt.lighter(Theme.panelBg, 1.5) : baseColor)

    // 内部文本
    Text {
        anchors.centerIn: parent
        text: root.text
        // 如果背景被点亮成了白色，文字就反转成深色
        color: mouseArea.pressed ? Theme.bgDark : Theme.textMain
        font.pixelSize: Theme.fontNormal
        font.bold: true
    }

    // --- 交互处理 ---
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true // 开启 PC 端的鼠标悬停检测
        
        onClicked: root.clicked()
        onPressed: root.pressed()
        
        // 确保无论在按钮内松开，还是按住拖出按钮外，都能发出 released 信号 (极其重要，防死机)
        onReleased: root.released()
        onCanceled: root.released() 
    }
}