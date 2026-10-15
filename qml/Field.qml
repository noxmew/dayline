import QtQuick
import QtQuick.Controls
import Dayline 1.0

TextField {
    id: root
    property bool invalid: false
    implicitHeight: 42
    padding: 12
    font.pixelSize: 13
    color: Theme.text
    placeholderTextColor: Theme.muted
    selectionColor: Theme.accentSoft
    selectedTextColor: Theme.text
    selectByMouse: true
    hoverEnabled: true
    opacity: enabled ? 1 : 0.5
    background: Rectangle {
        radius: 9
        color: Theme.input
        border.color: root.invalid ? Theme.danger : (root.activeFocus ? Theme.accent : (root.hovered ? Theme.lineStrong : Theme.line))
        Behavior on border.color { ColorAnimation { duration: 100 } }
    }
}
