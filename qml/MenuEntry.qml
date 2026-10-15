import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Dayline 1.0

MenuItem {
    id: entry
    property string iconName: ""
    property string shortcutText: ""
    property bool danger: false
    implicitWidth: 270
    implicitHeight: 40
    leftPadding: 11
    rightPadding: checkable ? 49 : 11
    topPadding: 8
    bottomPadding: 8
    hoverEnabled: true
    opacity: enabled ? 1 : 0.45
    contentItem: RowLayout {
        spacing: 11
        Icon {
            visible: entry.iconName.length > 0
            name: entry.iconName
            width: 17
            height: 17
            color: entry.danger ? Theme.danger : Theme.secondary
        }
        Text {
            Layout.fillWidth: true
            text: entry.text
            font.pixelSize: 12
            color: entry.danger ? Theme.danger : Theme.text
            elide: Text.ElideRight
        }
        Text {
            visible: entry.shortcutText.length > 0
            text: entry.shortcutText
            font.pixelSize: 10
            color: Theme.muted
        }
    }
    indicator: Rectangle {
        visible: entry.checkable
        x: entry.width - width - 12
        y: Math.round((entry.height - height) / 2)
        width: 31
        height: 19
        radius: 10
        color: entry.checked ? Theme.accent : Theme.lineStrong
        Rectangle {
            x: entry.checked ? 14 : 3
            y: 3
            width: 13
            height: 13
            radius: 7
            color: entry.checked ? Theme.accentText : Theme.surface
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        }
    }
    background: Rectangle {
        radius: 9
        color: entry.down ? Theme.accentSoft : (entry.highlighted || entry.hovered ? (entry.danger ? Theme.dangerSoft : Theme.hover) : "transparent")
        border.width: entry.activeFocus ? 1 : 0
        border.color: Theme.tint(Theme.accent, 0.4)
    }
}
