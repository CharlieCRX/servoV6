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
    property string _initialText: ""

    anchors.centerIn: Overlay.overlay
    modal: true
    dim: true
    width: 410 * Theme.scale
    padding: 18 * Theme.scale
    closePolicy: Popup.CloseOnEscape

    // ── 原生半透明遮罩 ──
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
            // 正号显示：allowNegative 为 true 且值非负时显示 + 号
            if (allowNegative && inputText.length > 0 && inputText.charAt(0) !== '-') {
                var val = parseFloat(inputText)
                if (!isNaN(val) && val >= 0) {
                    displayText = "+" + inputText
                } else {
                    displayText = inputText
                }
            } else {
                displayText = inputText
            }
            if (unit !== "") {
                displayText += " " + unit
            }
        }

        function _formatValue(val) {
            var fixed = val.toFixed(maxDecimals)
            if (fixed.indexOf('.') !== -1) {
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
            if (inputText === "" || inputText === "0") {
                inputText = String(digit)
            } else if (inputText === "-0") {
                inputText = "-" + String(digit)
            } else {
                var candidate = inputText + String(digit)
                var val = parseFloat(candidate)
                if (!isNaN(val) && val > maxValue) {
                    return
                }
                inputText = candidate
            }
            _buildDisplay()
        }

        function appendDoubleZero() {
            if (inputText === "0" || inputText === "") {
                return  // "00" 在首位置无意义，仍是 0
            }
            if (inputText === "-0") {
                return
            }
            var candidate = inputText + "00"
            var val = parseFloat(candidate)
            if (!isNaN(val) && val > maxValue) {
                return
            }
            inputText = candidate
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

        // 设置为正数：去掉负号
        function applyPositiveSign() {
            if (!allowNegative) return
            if (inputText.charAt(0) === '-') {
                inputText = inputText.substring(1)
            }
            if (inputText === "" || inputText === "-") {
                inputText = "0"
            }
            _buildDisplay()
        }

        // 切换正负号（- 按钮）
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
                inputText = ""
            } else {
                inputText = inputText.substring(0, inputText.length - 1)
                if (inputText === "-") {
                    inputText = "0"
                }
            }
            _buildDisplay()
        }

        function clearAll() {
            inputText = ""
            _buildDisplay()
        }

        function handleConfirm() {
            var val = parseFloat(inputText)
            if (isNaN(val)) {
                inputText = _initialText
                _buildDisplay()
                return
            }
            if (val > maxValue) {
                val = maxValue
            }
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

        // ── 预览显示区（含光标）──
        Rectangle {
            id: displayArea
            Layout.fillWidth: true
            Layout.preferredHeight: 50 * Theme.scale
            radius: 8 * Theme.scale
            color: Theme.bgDark
            border.color: Theme.colorIdle
            border.width: 1.5 * Theme.scale

            Row {
                anchors.centerIn: parent
                spacing: 0

                Text {
                    id: valueText
                    text: root.inputText
                    color: Theme.colorIdle
                    font.pixelSize: Theme.fontLarge
                    font.bold: true
                    font.family: "Monospace"
                }

                // 闪烁光标
                Rectangle {
                    id: cursor
                    width: 2 * Theme.scale
                    height: valueText.contentHeight * 0.85
                    color: Theme.colorIdle
                    anchors.verticalCenter: parent.verticalCenter
                    visible: true

                    Timer {
                        id: cursorTimer
                        interval: 530
                        running: root.visible
                        repeat: true
                        onTriggered: cursor.visible = !cursor.visible
                    }
                }

                // 单位
                Text {
                    text: root.unit
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSmall
                    opacity: 0.7
                    font.family: "Monospace"
                    visible: root.unit !== ""
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: 4 * Theme.scale
                }
            }
        }

        // ── 4 列按钮键盘区 ──
        GridLayout {
            Layout.fillWidth: true
            columns: 4
            rowSpacing: 8 * Theme.scale
            columnSpacing: 8 * Theme.scale

            // 第一行: 1, 2, 3, AC
            Repeater {
                model: [1, 2, 3]
                delegate: numKeyDelegate
            }

            NumpadKey {
                Layout.fillWidth: true
                Layout.preferredHeight: 52 * Theme.scale
                keyText: "AC"
                keyColor: "#5C3A3A"
                textColor: Theme.colorError
                fontWeight: Font.Bold
                onClicked: internal.clearAll()
            }

            // 第二行: 4, 5, 6, DEL
            Repeater {
                model: [4, 5, 6]
                delegate: numKeyDelegate
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

            // 第三行: 7, 8, 9, +
            Repeater {
                model: [7, 8, 9]
                delegate: numKeyDelegate
            }

            NumpadKey {
                Layout.fillWidth: true
                Layout.preferredHeight: 52 * Theme.scale
                keyText: "+"
                keyColor: allowNegative ? Theme.panelBg : "#2a2a2a"
                textColor: allowNegative ? Theme.textMain : "#444444"
                visible: true
                enabled: allowNegative
                onClicked: internal.applyPositiveSign()
            }

            // 第四行: 小数点, 00, 0, -
            NumpadKey {
                Layout.fillWidth: true
                Layout.preferredHeight: 52 * Theme.scale
                keyText: "."
                keyColor: allowDecimal ? Theme.panelBg : "#2a2a2a"
                textColor: allowDecimal ? Theme.textDim : "#444444"
                visible: true
                enabled: allowDecimal
                onClicked: internal.appendDecimal()
            }

            NumpadKey {
                Layout.fillWidth: true
                Layout.preferredHeight: 52 * Theme.scale
                keyText: "00"
                keyColor: Theme.panelBg
                onClicked: internal.appendDoubleZero()
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
                keyText: "-"
                keyColor: allowNegative ? Theme.panelBg : "#2a2a2a"
                textColor: allowNegative ? Theme.textDim : "#444444"
                visible: true
                enabled: allowNegative
                onClicked: internal.toggleSign()
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