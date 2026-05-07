import QtQuick
import QtQuick.Window
import servoV6

/**
 * Main.qml — 应用入口窗口
 * 委托给 MainDashboard，后者根据注入的 axisVMs / gantryVMs 自动渲染 Tab 页
 *
 * 注入依赖：
 *   - axisVMs  (QVariantMap): 独立轴 VM 映射表 (key: 轴名 → value: QtAxisViewModel*)
 *   - gantryVMs (QVariantMap): 龙门组 VM 映射表 (key: 组名 → value: QtGantryViewModel*)
 */
Window {
    id: mainWindow
    width: 1280
    height: 720
    visible: true
    title: qsTr("servoV6 — Multi-Gantry Control")
    color: "#0d1117"

    MainDashboard {
        anchors.fill: parent

        axisVMs: globalAxisVMs
        gantryVMs: globalGantryVMs
    }
}
