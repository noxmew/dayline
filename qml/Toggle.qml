import QtQuick
import QtQuick.Controls
import Dayline 1.0

Switch {
    id: toggle
    implicitWidth: 44
    implicitHeight: 30
    padding: 0
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45
    indicator: Rectangle {
        width: 40; height: 23; radius: 12
        anchors.centerIn: parent
        color: toggle.checked ? Theme.accent : Theme.lineStrong
        border.color: toggle.visualFocus ? Theme.accent : "transparent"
        Rectangle {
            anchors.fill: parent; anchors.margins: -3
            radius: 15
            color: "transparent"
            border.width: toggle.visualFocus ? 2 : 0
            border.color: Theme.tint(Theme.accent, 0.35)
        }
        Rectangle {
            x: toggle.checked ? 20 : 3; y: 3
            width: 17; height: 17; radius: 9
            color: toggle.checked ? Theme.accentText : Theme.surface
            Behavior on x { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: Item { }
}
