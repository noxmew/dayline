import QtQuick
import QtQuick.Controls
import Dayline 1.0

Menu {
    id: menu
    width: 284
    padding: 7
    topPadding: 8
    bottomPadding: 8
    margins: 12
    overlap: 4
    spacing: 2
    delegate: MenuEntry { }
    background: Rectangle {
        implicitWidth: 284
        radius: 15
        color: Theme.surface
        border.color: Theme.lineStrong
        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.margins: -1
            anchors.topMargin: 1
            anchors.bottomMargin: -4
            radius: 17
            color: Theme.tint("#071811", Theme.dark ? 0.22 : 0.06)
        }
    }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 100 }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 70 }
    }
}
