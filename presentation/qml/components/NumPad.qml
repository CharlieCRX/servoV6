import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import servoV6

Popup {
    id: root

    // ── 公共属性 ──
    property string inputText: "0"          // 当前输入文本（双向绑定）
    property bool allowDecimal: true        // 是否允许小数点
    property bool allowNegative: false      // 是否允许负号
    property real maxValue: 99999.99        // 最大值限制
    property int maxDecimals: 2             // 最大小数位数
    property string unit: ""                // 单位文本（如 "mm"、"mm/s"）
    property string title: "数字键盘"       // 键盘标题

    // ── 信号 ──
    signal confirmed(string value)

    // ── 内部状态 ──
    // 在 popup 打开时保存初始值，以便取消时恢复
    property string _initialText: ""

    anchors.centerIn: Overlay.overlay
    modal: true
    dim: true
    width: 340 * Theme.scale
    padding: 18 * Theme.scale
    closePolicy: Popup.CloseOnEscape

    // ── 原生半透明遮罩（通过 Overlay.modal 定制，Qt 内置机制）──
    Overlay.modal: Rectangle {
        color: '#b4000000'

        Behavior on opacity {
            NumberAnimation { duration: 150 }
        }
    }

    onOpened: {
        _initialText = inputText
        if (inputText === "" || inputText === "-") {
            internal._buildDisplay()
            return
        }
        // 规范化初始显示
        var val = parseFloat(inputText)
        if (isNaN(val)) {
            inputText = "0"
        } else {
            inputText = internal._formatValue(val)
        }
        internal._buildDisplay()
    }

    // ── 背景 ──
    background: Rectangle {
        color: Theme.panelBg
        radius: 16 * Theme.scale
        border.color: Theme.borderMain
        border.width: 2 * Theme.scale
    }

    // ── QtObject 内部逻辑 ──
    QtObject {
        id: internal

        property string displayText: ""

        function _buildDisplay() {
            displayText = inputText
            if (unit !== "") {
                displayText += " " + unit
            }
        }

        function _formatValue(val) {
            var fixed = val.toFixed(maxDecimals)
            // 去掉尾部无意义的零，但保留至少一位小数（如果有小数部分）
            if (fixed.indexOf('.') !== -1) {
                // 去掉尾部零
                while (fixed.charAt(fixed.length - 1) === '0' && fixed.charAt(fixed.length - 2) !== '.') {
                    fixed = fixed.substring(0, fixed.length - 1)
                }
            }
            return fixed
        }

        function hasDecimal() {
            return inputText.indexOf('.') !== -1
        }

        function decimalCount() {
            var idx = inputText.indexOf('.')
            if (idx === -1) return 0
            return inputText.length - idx - 1
        }

        function appendDigit(digit) {
            // "0" 特殊处理：如果当前是"0"，替换它
            if (inputText === "0") {
                inputText = String(digit)
            } else if (inputText === "-0") {
                inputText = "-" + String(digit)
            } else {
                // 检查是否超过最大值（粗略检查）
                var candidate = inputText + String(digit)
                var val = parseFloat(candidate)
                if (!isNaN(val) && val > maxValue) {
                    return // 超过最大值，不允许输入
                }
                inputText = candidate
            }
            _buildDisplay()
        }

        function appendDecimal() {
            if (!allowDecimal) return
            if (hasDecimal()) return
            if (inputText === "" || inputText === "-") {
                inputText = inputText + "0."
            } else {
                inputText = inputText + "."
            }
            _buildDisplay()
        }

        function toggleSign() {
            if (!allowNegative) return
            if (inputText.charAt(0) === '-') {
                inputText = inputText.substring(1)
            } else {
                inputText = "-" + inputText
            }
            if (inputText === "" || inputText === "-") {
                inputText = "0"
            }
            _buildDisplay()
        }

        function backspace() {
            if (inputText.length <= 1) {
                inputText = "0"
            } else {
                inputText = inputText.substring(0, inputText.length - 1)
                // 如果删到只剩负号
                if (inputText === "-") {
                    inputText = "0"
                }
            }
            _buildDisplay()
        }

        function clearAll() {
            inputText = "0"
            _buildDisplay()
        }

        function handleConfirm() {
            var val = parseFloat(inputText)
            if (isNaN(val)) {
                inputText = _initialText  // 恢复
                _buildDisplay()
                return
            }
            if (val > maxValue) {
                val = maxValue
            }
            // 限制小数位数
            val = parseFloat(val.toFixed(maxDecimals))
            inputText = internal._formatValue(val)
            _buildDisplay()
            root.confirmed(inputText)
            root.close()
        }

        function handleCancel() {
            inputText = _initialText
            _buildDisplay()
            root.close()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12 * Theme.scale

        // ── 标题行 ──
        Text {
            text: root.title
            color: Theme.textMain
            font.pixelSize: Theme.fontLarge
            font.bold: true
            Layout.alignment: Qt.AlignHCenter
        }

        // ── 预览显示区 ──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 50 * Theme.scale
            radius: 8 * Theme.scale
            color: Theme.bgDark
            border.color: Theme.colorIdle
            border.width: 1.5 * Theme.scale

            Row {
                anchors.centerIn: parent
                spacing: 6 * Theme.scale

                // 值：高亮醒目
                Text {
                    id: valueText
                    text: root.inputText
                    color: Theme.colorIdle
                    font.pixelSize: Theme.fontLarge
                    font.bold: true
                    font.family: "Monospace"
                }

                // 单位：缩小、半透明、嵌入背景
                Text {
                    text: root.unit
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                    opacity: 0.7
                    font.family: "Monospace"
                    visible: root.unit !== ""
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        // ── 按钮键盘区 ──
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            rowSpacing: 8 * Theme.scale
            columnSpacing: 8 * Theme.scale

            // 第一行: 1, 2, 3
            Repeater {
                model: [1, 2, 3]
                delegate: numKeyDelegate
            }

            // 第二行: 4, 5, 6
            Repeater {
                model: [4, 5, 6]
                delegate: numKeyDelegate
            }

            // 第三行: 7, 8, 9
            Repeater {
                model: [7, 8, 9]
                delegate: numKeyDelegate
            }

            // 第四行: 小数点(或正负号) , 0, 退格
            NumpadKey {
                Layout.fillWidth: true
                Layout.preferredHeight: 52 * Theme.scale
                keyText: allowNegative ? "+/-" : (allowDecimal ? "." : "")
                keyColor: Theme.panelBg
                textColor: Theme.textDim
                visible: allowNegative || allowDecimal
                enabled: allowNegative || allowDecimal
                onClicked: {
                    if (allowNegative) {
                        internal.toggleSign()
                    } else if (allowDecimal) {
                        internal.appendDecimal()
                    }
                }
            }

            // 如果既不允许负号也不允许小数点，放一个占位
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 52 * Theme.scale
                visible: !allowNegative && !allowDecimal
            }

            NumpadKey {
                Layout.fillWidth: true
                Layout.preferredHeight: 52 * Theme.scale
                keyText: "0"
                keyColor: Theme.panelBg
                onClicked: internal.appendDigit(0)
            }

            NumpadKey {
                Layout.fillWidth: true
                Layout.preferredHeight: 52 * Theme.scale
                keyText: "DEL"
                keyColor: "#3A5A7C"
                textColor: Theme.colorWarning
                fontWeight: Font.Bold
                onClicked: internal.backspace()
            }
        }

        // ── 操作按钮行 ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 12 * Theme.scale

            NumpadKey {
                Layout.fillWidth: true
                Layout.preferredHeight: 48 * Theme.scale
                keyText: "取 消"
                keyColor: "transparent"
                borderWidth: 1.5
                onClicked: internal.handleCancel()
            }

            NumpadKey {
                Layout.fillWidth: true
                Layout.preferredHeight: 48 * Theme.scale
                keyText: "确 认"
                keyColor: Theme.colorIdle
                textColor: Theme.bgDark
                fontWeight: Font.Bold
                onClicked: internal.handleConfirm()
            }
        }
    }

    // ── 数字按键委托 ──
    Component {
        id: numKeyDelegate
        NumpadKey {
            Layout.fillWidth: true
            Layout.preferredHeight: 52 * Theme.scale
            keyText: String(modelData)
            keyColor: Theme.panelBg
            onClicked: internal.appendDigit(modelData)
        }
    }

    // ── 内联 NumpadKey 组件 ──
    component NumpadKey: Rectangle {
        id: keyRect
        property string keyText: ""
        property color keyColor: Theme.panelBg
        property color textColor: Theme.textMain
        property real borderWidth: 1.0
        property int fontWeight: Font.Normal

        signal clicked()

        radius: 8 * Theme.scale
        color: keyMouse.pressed ? Qt.lighter(keyColor, 1.6) : keyColor
        border.color: Theme.borderMain
        border.width: borderWidth * Theme.scale

        Text {
            anchors.centerIn: parent
            text: keyRect.keyText
            color: keyRect.textColor
            font.pixelSize: Theme.fontLarge
            font.bold: keyRect.fontWeight === Font.Bold
        }

        MouseArea {
            id: keyMouse
            anchors.fill: parent
            onClicked: keyRect.clicked()
        }
    }
}
