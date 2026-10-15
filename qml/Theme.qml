pragma Singleton
import QtQuick

QtObject {
    property bool dark: false
    readonly property color background: dark ? "#151D1B" : "#FFFFFF"
    readonly property color sidebar: dark ? "#19221F" : "#F5F7F4"
    readonly property color panel: dark ? "#18211F" : "#FAFBF9"
    readonly property color surface: dark ? "#202B27" : "#FFFFFF"
    readonly property color input: dark ? "#1A2420" : "#F8FAF7"
    readonly property color text: dark ? "#E7EDE9" : "#253B32"
    readonly property color secondary: dark ? "#AFBDB5" : "#64786D"
    readonly property color muted: dark ? "#91A69A" : "#82948A"
    readonly property color line: dark ? "#2B3932" : "#E9EEE8"
    readonly property color lineStrong: dark ? "#3B4C42" : "#DEE6DF"
    readonly property color accent: dark ? "#79C7AB" : "#218572"
    readonly property color accentSoft: dark ? "#263D33" : "#E8F2EC"
    readonly property color hover: dark ? "#24332B" : "#F2F6F1"
    readonly property color danger: dark ? "#EE9C92" : "#BC5B51"
    readonly property color dangerSoft: dark ? "#412E2B" : "#FAEFEC"
    readonly property color accentText: dark ? "#17382C" : "#FFFFFF"
    readonly property color overlay: dark ? "#88070D0A" : "#580E211A"
    function tint(value, alpha) {
        const c = Qt.darker(value, 1.0)
        return Qt.rgba(c.r, c.g, c.b, alpha)
    }
}
