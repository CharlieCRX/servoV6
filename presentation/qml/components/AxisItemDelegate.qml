import QtQuick
import QtQuick.Layouts
import servoV6

Rectangle {
    id: delegateRoot
    
    property string name: ""
    property string statusText: ""
    property string subLabel: ""
    property bool isActive: false
    property bool isDual: false
    property string indicatorColor: ""  // 空字符串时使用默认逻辑
    
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

        // 状态指示灯（支持外部覆盖颜色，如龙门耦合状态色）
        Rectangle {
            width: 10 * Theme.scale
            height: 10 * Theme.scale
            radius: 5
            color: {
                if (delegateRoot.indicatorColor !== "") return delegateRoot.indicatorColor
                return isActive ? Theme.colorIdle : Theme.colorDisabled
            }
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
            // 副标签（如"↳ 龙门"）
            Text {
                text: delegateRoot.subLabel
                visible: delegateRoot.subLabel !== ""
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall * 0.85
                font.italic: true
            }
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: delegateRoot.clicked()
    }
}
