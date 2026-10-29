import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Dayline 1.0

Control {
    id: row
    default property alias fields: contents.data
    property string label: ""
    property bool separator: false
    padding: 14
    topPadding: 7
    bottomPadding: 7
    implicitHeight: Math.max(50, implicitContentHeight + topPadding + bottomPadding)
    contentItem: RowLayout {
        spacing: 12
        Text { visible: row.label.length > 0; text: row.label; color: Theme.text; font.pixelSize: 13 }
        RowLayout { id: contents; Layout.fillWidth: true; spacing: 8 }
    }
    background: Item {
        Rectangle {
            visible: row.separator
            anchors.left: parent.left; anchors.leftMargin: 14
            anchors.right: parent.right; anchors.bottom: parent.bottom
            height: 1; color: Theme.line
        }
    }
}
