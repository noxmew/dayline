import QtQuick
import QtQuick.Controls
import Dayline 1.0

Switch {
    id: toggle
    implicitWidth: 42
    implicitHeight: 26
    padding: 0
    indicator: Rectangle {
        width: 40; height: 23; radius: 12
        anchors.centerIn: parent
        color: toggle.checked ? Theme.accent : Theme.line
        border.color: toggle.activeFocus ? Theme.accent : "transparent"
        Rectangle {
            x: toggle.checked ? 20 : 3; y: 3
            width: 17; height: 17; radius: 9
            color: "#FFFFFF"
            Behavior on x { NumberAnimation { duration: 130; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: Item { }
}
