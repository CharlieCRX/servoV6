import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import servoV6

Rectangle {
    id: root

    // ===== 核心接口 =====
    property var viewModel: null

    // ===== 样式 =====
    color: Theme.panelBg
    radius: 8 * Theme.scale
    border.color: Theme.borderMain
    border.width: 1

    // 仅在有错误时显示
    visible: {
        if (!viewModel) return false
        // 过滤 Silent 类错误（不展示）
        let errors = viewModel.getAllErrors()
        for (let i = 0; i < errors.length; i++) {
            if (errors[i].category !== "Silent") return true
        }
        return false
    }

    // 计算非 Silent 错误数量（用于徽标）
    property int visibleErrorCount: {
        if (!viewModel) return 0
        let count = 0
        let errors = viewModel.getAllErrors()
        for (let i = 0; i < errors.length; i++) {
            if (errors[i].category !== "Silent") count++
        }
        return count
    }

    // ===== 布局 =====
    implicitHeight: Math.max(60 * Theme.scale, errorColumn.implicitHeight + 24 * Theme.scale)

    ColumnLayout {
        id: errorColumn
        anchors.fill: parent
        anchors.margins: 12 * Theme.scale
        spacing: 8 * Theme.scale

        // ----- 标题栏 -----
        RowLayout {
            Layout.fillWidth: true

            Text {
                text: "⚠ 提醒"
                color: Theme.colorError
                font.pixelSize: Theme.fontNormal
                font.bold: true
            }

            Text {
                text: root.visibleErrorCount + " 条"
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
            }

            Item { Layout.fillWidth: true }

            IndustrialButton {
                text: "全部确认"
                buttonSize: 80 * Theme.scale
                baseColor: "transparent"
                border.color: Theme.borderMain
                border.width: 1
                visible: root.visibleErrorCount > 0
                onClicked: {
                    if (viewModel) viewModel.acknowledgeAllErrors()
                }
            }
        }

        // ----- 错误列表 (ListView) -----
        ListView {
            id: errorList
            Layout.fillWidth: true
            Layout.preferredHeight: contentHeight
            interactive: false  // 禁用滚动（自动扩展高度）
            spacing: 6 * Theme.scale

            model: viewModel ? viewModel.getAllErrors() : []

            delegate: Rectangle {
                width: errorList.width
                height: 36 * Theme.scale
                color: "transparent"
                // Silent 类过滤（ListView 中隐藏）
                visible: modelData.category !== "Silent"

                // 读取当前项的属性
                readonly property string errCode: modelData.code || ""
                readonly property string errMsg: modelData.message || ""
                readonly property string errCat: modelData.category || ""

                RowLayout {
                    anchors.fill: parent
                    spacing: 8 * Theme.scale

                    // 类别图标
                    Text {
                        text: errCat === "Modal" ? "⚡" : "ⓘ"
                        color: errCat === "Modal" ? Theme.colorError : Theme.colorMoving
                        font.pixelSize: Theme.fontSmall
                        Layout.preferredWidth: 20 * Theme.scale
                    }

                    // 错误消息
                    Text {
                        text: errMsg
                        color: Theme.textMain
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    // 分类标签
                    Rectangle {
                        visible: errCat !== ""
                        color: errCat === "Modal" ? "#4A2020" : "#1A2A44"
                        radius: 4 * Theme.scale
                        Layout.preferredWidth: 50 * Theme.scale
                        Layout.preferredHeight: 20 * Theme.scale

                        Text {
                            anchors.centerIn: parent
                            text: errCat === "Modal" ? "弹窗" : "内联"
                            color: errCat === "Modal" ? Theme.colorError : Theme.colorMoving
                            font.pixelSize: 10 * Theme.scale
                        }
                    }

                    // 确认按钮
                    IndustrialButton {
                        text: "×"
                        buttonSize: 24 * Theme.scale
                        isCircle: true
                        baseColor: "transparent"
                        border.color: Theme.borderMain
                        border.width: 1
                        onClicked: {
                            if (viewModel) viewModel.acknowledgeError(index)
                        }
                    }
                }
            }
        }
    }
}
