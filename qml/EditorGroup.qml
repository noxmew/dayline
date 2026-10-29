import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Dayline 1.0

Control {
    default property alias rows: group.data
    padding: 0
    contentItem: ColumnLayout { id: group; spacing: 0 }
    background: Rectangle { radius: 12; color: Theme.surface; border.color: Theme.line }
}
