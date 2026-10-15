import QtQuick
import QtQuick.Window
import Dayline 1.0

Item {
    id: grid
    property bool desktopStyle: false
    readonly property int rows: calendarStore.weekCount
    readonly property real pixelSize: 1 / Screen.devicePixelRatio
    signal selected(string date)
    signal createRequested(string date)
    signal eventRequested(var event)

    function snap(value) { return Math.round(value / pixelSize) * pixelSize }

    Repeater {
        model: calendarStore.monthDays
        delegate: CalendarCell {
            required property int index
            required property var modelData
            readonly property int column: index % 7
            readonly property int row: Math.floor(index / 7)
            x: grid.snap(column * grid.width / 7)
            y: grid.snap(row * grid.height / grid.rows)
            width: grid.snap((column + 1) * grid.width / 7) - x
            height: grid.snap((row + 1) * grid.height / grid.rows) - y
            info: modelData
            desktopStyle: grid.desktopStyle
            onSelected: date => grid.selected(date)
            onCreateRequested: date => grid.createRequested(date)
            onEventRequested: event => grid.eventRequested(event)
        }
    }

    // Draw separators above the cells so cell clipping cannot remove them.
    Repeater {
        model: 6
        delegate: Rectangle {
            required property int index
            x: grid.snap((index + 1) * grid.width / 7)
            width: grid.pixelSize
            height: grid.height
            color: Theme.line
            z: 1
        }
    }
    Repeater {
        model: grid.rows - 1
        delegate: Rectangle {
            required property int index
            y: grid.snap((index + 1) * grid.height / grid.rows)
            width: grid.width
            height: grid.pixelSize
            color: Theme.line
            z: 1
        }
    }
}
