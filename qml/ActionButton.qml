import QtQuick
import QtQuick.Controls
import Dayline 1.0

Button {
    id: root
    property string iconName: ""
    property string hint: ""
    property bool primary: false
    property bool quiet: false
    property bool danger: false
    implicitHeight: 38
    implicitWidth: body.implicitWidth + (text.length ? 26 : 18)
    padding: 9
    hoverEnabled: true
    opacity: enabled ? 1 : 0.42
    Accessible.name: text.length ? text : hint
    ToolTip {
        visible: root.hovered && root.hint.length > 0
        text: root.hint
        delay: 600
        timeout: 4000
        x: Math.round((root.width - width) / 2)
        y: root.height + 7
        padding: 8
        background: Rectangle { color: Theme.surface; radius: 7; border.color: Theme.lineStrong }
        contentItem: Text { text: root.hint; color: Theme.text; font.pixelSize: 11 }
    }
    contentItem: Item {
        implicitWidth: body.implicitWidth
        implicitHeight: 20
        Row {
            id: body
            anchors.centerIn: parent
            spacing: 7
            Icon {
                visible: root.iconName.length > 0
                name: root.iconName
                width: 17; height: 17
                anchors.verticalCenter: parent.verticalCenter
                color: root.primary ? (root.danger ? "#FFFFFF" : Theme.accentText) : (root.danger ? Theme.danger : Theme.secondary)
            }
            Text {
                visible: root.text.length > 0
                text: root.text
                font.pixelSize: 12
                font.weight: Font.Medium
                color: root.primary ? (root.danger ? "#FFFFFF" : Theme.accentText) : (root.danger ? Theme.danger : Theme.text)
            }
        }
    }
    background: Rectangle {
        radius: 9
        readonly property color tone: root.danger ? Theme.danger : Theme.accent
        color: root.primary ? (root.down ? Qt.darker(tone, 1.13) : (root.hovered ? Qt.lighter(tone, 1.07) : tone)) :
               (root.hovered || root.down ? (root.danger ? Theme.dangerSoft : Theme.hover) : (root.quiet ? "transparent" : Theme.surface))
        border.width: root.visualFocus || (!root.primary && !root.quiet) ? 1 : 0
        border.color: root.visualFocus ? Theme.accent : Theme.lineStrong
        Behavior on color { ColorAnimation { duration: 100 } }
    }
}
