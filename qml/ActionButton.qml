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
    Accessible.name: text.length ? text : hint
    ToolTip.visible: hovered && hint.length > 0
    ToolTip.text: hint
    ToolTip.delay: 600
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
                color: root.primary ? (Theme.dark ? "#18372B" : "#FFFFFF") : (root.danger ? Theme.danger : Theme.secondary)
            }
            Text {
                visible: root.text.length > 0
                text: root.text
                font.pixelSize: 12
                font.weight: Font.Medium
                color: root.primary ? (Theme.dark ? "#18372B" : "#FFFFFF") : (root.danger ? Theme.danger : Theme.text)
            }
        }
    }
    background: Rectangle {
        radius: 8
        color: root.primary ? (root.down ? Qt.darker(Theme.accent,1.13) : (root.hovered ? Qt.lighter(Theme.accent,1.07) : Theme.accent)) :
               (root.hovered || root.down ? Theme.hover : (root.quiet ? "transparent" : Theme.surface))
        border.width: root.primary || root.quiet ? 0 : 1
        border.color: root.activeFocus ? Theme.accent : Theme.line
    }
}
