import QtQuick
import QtQuick.Layouts
import servoV6

Rectangle {
    id: delegateRoot
    
    property string name: ""
    property string statusText: ""
    property bool isActive: false
    property bool isDual: false
    
    signal clicked()

    Layout.fillWidth: true
    height: 70 * Theme.scale
    radius: 8 * Theme.scale
    
    // 背景色：选中时高亮，未选中时半透明
    color: isActive ? Theme.panelBg : (mouseArea.containsMouse ? "#1A2A44" : "transparent")
    border.color: isActive ? Theme.colorMoving : Theme.borderMain
    border.width: isActive ? 2 : 1

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12 * Theme.scale
        spacing: 12 * Theme.scale

        // 状态指示灯
        Rectangle {
            width: 10 * Theme.scale
            height: 10 * Theme.scale
            radius: 5
            color: isActive ? Theme.colorIdle : Theme.colorDisabled
        }

        Column {
            Layout.fillWidth: true
            Text {
                text: delegateRoot.name
                color: delegateRoot.isActive ? Theme.textMain : Theme.textDim
                font.pixelSize: Theme.fontNormal
                font.bold: delegateRoot.isActive
            }
            Text {
                text: delegateRoot.statusText
                color: delegateRoot.isActive ? Theme.colorMoving : "#556677"
                font.pixelSize: Theme.fontSmall
            }
        }

        // 图标占位 (单轴显示 1，双轴显示 2)
        Text {
            text: isDual ? "⛓" : "📍"
            font.pixelSize: 20
            opacity: 0.5
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: delegateRoot.clicked()
    }
}