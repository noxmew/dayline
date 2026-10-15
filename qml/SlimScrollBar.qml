import QtQuick
import QtQuick.Controls
import Dayline 1.0

ScrollBar {
    id: bar
    padding: 2
    minimumSize: 0.08
    contentItem: Rectangle {
        implicitWidth: 4
        implicitHeight: 4
        radius: 2
        color: bar.pressed ? Theme.accent : Theme.tint(Theme.muted, bar.hovered ? 0.65 : 0.35)
    }
    background: Item { }
}
