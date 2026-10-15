import QtQuick
import QtQuick.Controls
import Dayline 1.0

ComboBox {
    id: field
    implicitHeight: 42
    leftPadding: 12
    rightPadding: 35
    font.pixelSize: 13
    hoverEnabled: true
    opacity: enabled ? 1 : 0.5
    contentItem: Text {
        text: field.displayText
        font: field.font
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Icon {
        name: "down"
        width: 14
        height: 14
        x: field.width - width - 12
        y: Math.round((field.height - height) / 2)
        color: field.popup.visible ? Theme.accent : Theme.secondary
    }
    background: Rectangle {
        radius: 9
        color: field.hovered ? Theme.hover : Theme.input
        border.color: field.activeFocus || field.popup.visible ? Theme.accent : Theme.lineStrong
    }
    delegate: ItemDelegate {
        id: option
        required property int index
        width: field.width - 12
        height: 38
        highlighted: field.highlightedIndex === index
        contentItem: Text {
            text: field.textAt(option.index)
            color: field.currentIndex === option.index ? Theme.accent : Theme.text
            font.pixelSize: 12
            font.weight: field.currentIndex === option.index ? Font.Medium : Font.Normal
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            rightPadding: 22
        }
        Icon {
            visible: field.currentIndex === option.index
            name: "check"
            width: 15
            height: 15
            x: parent.width - width - 12
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.accent
        }
        background: Rectangle {
            radius: 7
            color: option.highlighted ? Theme.hover : "transparent"
        }
    }
    popup: Popup {
        y: field.height + 5
        width: field.width
        implicitHeight: Math.min(contentItem.implicitHeight + 12, 240)
        padding: 6
        background: Rectangle { color: Theme.surface; radius: 12; border.color: Theme.lineStrong }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: field.popup.visible ? field.delegateModel : null
            currentIndex: field.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator { }
        }
    }
}
