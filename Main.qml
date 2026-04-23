import QtQuick
import QtQuick.Window
import servoV6

Window {
    width: 1280
    height: 720
    visible: true
    title: qsTr("servoV6 - UI Integration Test")
    color: Theme.bgDark

    Column {
        anchors.centerIn: parent
        spacing: 30

        Text {
            text: "当前 Axis 绝对位置: " + axisX1VM.absPos.toFixed(2)
            color: Theme.textMain
            font.pixelSize: Theme.fontLarge
        }
        
        Text {
            text: "当前状态: " + axisX1VM.state
            color: axisX1VM.state === 6 ? Theme.colorError : Theme.colorIdle
            font.pixelSize: Theme.fontLarge
        }

        Row {
            spacing: 20
            
            IndustrialButton {
                text: "JOG -"
                onPressed: axisX1VM.jogNegativePressed()
                onReleased: axisX1VM.jogNegativeReleased()
            }

            IndustrialButton {
                text: "JOG +"
                onPressed: axisX1VM.jogPositivePressed()
                onReleased: axisX1VM.jogPositiveReleased()
            }
            
            IndustrialButton {
                text: "急停!"
                isCircle: false 
                baseColor: Theme.colorError
                onClicked: axisX1VM.stop()
            }
        }
    }
}