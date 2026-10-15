import QtQuick
import Dayline 1.0

Rectangle {
    id: cell
    required property var info
    signal selected(string date)
    signal createRequested(string date)
    signal eventRequested(var event)
    color: info.selected ? Theme.accentSoft : (hovered.hovered ? Theme.hover : (info.weekend ? Theme.panel : Theme.background))
    clip: true
    Accessible.role: Accessible.Button
    Accessible.name: info.date + "，" + info.events.length + "项日程"
    HoverHandler { id: hovered }
    MouseArea {
        anchors.fill: parent
        onClicked: cell.selected(cell.info.date)
        onDoubleClicked: cell.createRequested(cell.info.date)
    }
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.line }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
    Rectangle {
        x: 11; y: 11; width: 28; height: 28; radius: 9
        color: cell.info.today ? Theme.accent : "transparent"
        Text {
            anchors.centerIn: parent
            text: cell.info.day
            color: cell.info.today ? (Theme.dark ? "#15352A" : "white") : (!cell.info.inMonth ? Theme.muted : Theme.text)
            font.pixelSize: 13
            font.weight: cell.info.today || cell.info.selected ? Font.DemiBold : Font.Normal
        }
    }
    Text {
        anchors.right: parent.right; anchors.rightMargin: 12; y: 18
        visible: cell.info.today
        text: "今天"; color: Theme.accent; font.pixelSize: 10
    }
    Column {
        x: 7; y: 47; width: parent.width - 14; spacing: 5
        Repeater {
            model: cell.info.events.slice(0, Math.max(1, Math.floor((cell.height - 69) / 28)))
            delegate: Rectangle {
                required property var modelData
                width: parent.width; height: 25; radius: 5
                color: Theme.tint(modelData.color, Theme.dark ? 0.16 : 0.09)
                Rectangle { x: 0; y: 5; width: 2; height: 15; radius: 1; color: modelData.color }
                Text {
                    anchors.fill: parent; anchors.leftMargin: 7; anchors.rightMargin: 5
                    verticalAlignment: Text.AlignVCenter
                    text: (modelData.allDay ? "" : modelData.start + "  ") + modelData.title
                    elide: Text.ElideRight
                    font.pixelSize: 10
                    color: Theme.dark ? Qt.lighter(modelData.color,1.6) : Qt.darker(modelData.color,1.08)
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { cell.selected(cell.info.date); cell.eventRequested(modelData) } }
            }
        }
        Text {
            readonly property int remaining: cell.info.events.length - Math.max(1, Math.floor((cell.height - 69) / 28))
            visible: remaining > 0
            text: "还有 " + remaining + " 项"
            font.pixelSize: 10; color: Theme.secondary; leftPadding: 5
        }
    }
}
