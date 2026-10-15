import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Dayline 1.0

AbstractButton {
    id: toggle
    property string iconName: ""
    implicitWidth: 270
    implicitHeight: 40
    padding: 11
    topPadding: 8
    bottomPadding: 8
    checkable: true
    hoverEnabled: true
    Accessible.role: Accessible.CheckBox
    Accessible.name: text
    contentItem: RowLayout {
        spacing: 11
        Icon { name: toggle.iconName; width: 17; height: 17; color: Theme.secondary }
        Text { Layout.fillWidth: true; text: toggle.text; font.pixelSize: 12; color: Theme.text }
        Rectangle {
            width: 31; height: 19; radius: 10
            color: toggle.checked ? Theme.accent : Theme.lineStrong
            Rectangle {
                x: toggle.checked ? 14 : 3; y: 3
                width: 13; height: 13; radius: 7
                color: toggle.checked ? Theme.accentText : Theme.surface
                Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
            }
        }
    }
    background: Rectangle {
        radius: 9
        color: toggle.down ? Theme.accentSoft : (toggle.hovered ? Theme.hover : "transparent")
        border.width: toggle.visualFocus ? 1 : 0
        border.color: Theme.tint(Theme.accent, 0.4)
    }
}
