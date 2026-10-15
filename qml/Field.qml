import QtQuick
import QtQuick.Controls
import Dayline 1.0

TextField {
    id: root
    implicitHeight: 42
    padding: 12
    font.pixelSize: 13
    color: Theme.text
    placeholderTextColor: Theme.muted
    selectionColor: Theme.accentSoft
    selectedTextColor: Theme.text
    selectByMouse: true
    background: Rectangle {
        radius: 8
        color: Theme.input
        border.color: root.activeFocus ? Theme.accent : Theme.line
    }
}
