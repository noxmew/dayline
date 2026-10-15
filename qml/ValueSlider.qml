import QtQuick
import QtQuick.Controls
import Dayline 1.0

Slider {
    id: slider
    implicitWidth: 220
    implicitHeight: 30
    leftPadding: 9
    rightPadding: 9
    hoverEnabled: true
    background: Rectangle {
        x: slider.leftPadding
        y: Math.round((slider.height - height) / 2)
        width: slider.availableWidth
        height: 4
        radius: 2
        color: Theme.lineStrong
        Rectangle {
            width: slider.visualPosition * parent.width
            height: parent.height
            radius: 2
            color: Theme.accent
        }
    }
    handle: Rectangle {
        x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
        y: Math.round((slider.height - height) / 2)
        width: 18
        height: 18
        radius: 9
        color: Theme.surface
        border.color: slider.pressed || slider.activeFocus ? Theme.accent : Theme.lineStrong
        border.width: slider.pressed || slider.activeFocus ? 2 : 1
        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: 13
            color: Theme.tint(Theme.accent, slider.pressed ? 0.14 : (slider.hovered ? 0.08 : 0))
            z: -1
        }
    }
}
