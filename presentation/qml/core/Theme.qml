pragma Singleton
import QtQuick

QtObject {
    // === 1. 动态缩放因子 (适配 1080P Pad 与 PC 窗口) ===
    property real scale: 1.0 

    // === 2. 字体尺寸池 ===
    readonly property int fontGiant: 64 * scale
    readonly property int fontLarge: 32 * scale
    readonly property int fontNormal: 18 * scale
    readonly property int fontSmall: 14 * scale

    // === 3. 核心色盘 (工业深色风) ===
    readonly property color bgDark: "#0B1528"    // 极深蓝背景 (防眩光)
    readonly property color panelBg: "#16243A"   // 模块底色
    readonly property color borderMain: "#2A3F5F"// 边框颜色
    readonly property color textMain: "#FFFFFF"  // 主文本纯白
    readonly property color textDim: "#8899A6"   // 次要文本灰

    // === 4. 状态语义色 (红绿灯) ===
    readonly property color colorIdle: "#00E676"      // 就绪绿
    readonly property color colorMoving: "#00B0FF"    // 运行蓝
    readonly property color colorDisabled: "#607D8B"  // 掉电灰暗
    readonly property color colorError: "#FF1744"     // 警报红
    readonly property color colorWarning: "#FFEA00"   // 警告黄
}