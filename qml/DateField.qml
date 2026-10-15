import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Dayline 1.0

ActionButton {
    id: field
    property string value: Qt.formatDate(new Date(), "yyyy-MM-dd")
    property bool readOnly: false
    property string label: "日期"
    readonly property bool pickerVisible: picker.visible
    readonly property date selectedDate: new Date(value + "T12:00:00")
    iconName: "calendar"
    text: Qt.formatDate(selectedDate, "yyyy年M月d日")
    implicitWidth: dateContent.implicitWidth + leftPadding + rightPadding
    implicitHeight: 42
    leftPadding: 12
    rightPadding: 12
    enabled: !readOnly
    Accessible.name: label + "，" + text
    onClicked: picker.open()
    contentItem: RowLayout {
        id: dateContent
        spacing: 8
        Icon {
            name: "calendar"
            Layout.preferredWidth: 17; Layout.preferredHeight: 17
            Layout.alignment: Qt.AlignVCenter
            color: Theme.secondary
        }
        Text {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumWidth: 0
            text: field.text
            font.pixelSize: 13
            color: Theme.text
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
    background: Rectangle {
        radius: 9
        color: field.hovered ? Theme.hover : Theme.input
        border.color: field.visualFocus || picker.visible ? Theme.accent : Theme.lineStrong
    }

    function choose(date) {
        value = Qt.formatDate(date, "yyyy-MM-dd")
        picker.close()
        forceActiveFocus()
    }
    function closePicker() { picker.close() }

    Popup {
        id: picker
        parent: Overlay.overlay
        width: 316; padding: 14; margins: 12
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        property date month: new Date()
        property string today: ""
        readonly property int leadingDays: (month.getDay() + 6) % 7
        x: Math.max(12, Math.min(field.mapToItem(parent, 0, 0).x, parent.width - width - 12))
        y: {
            const position = field.mapToItem(parent, 0, 0)
            const below = position.y + field.height + 6
            return below + height <= parent.height - 12 ? below : Math.max(12, position.y - height - 6)
        }
        onAboutToShow: {
            month = new Date(field.selectedDate.getFullYear(), field.selectedDate.getMonth(), 1)
            today = Qt.formatDate(new Date(), "yyyy-MM-dd")
        }
        background: Rectangle { radius: 14; color: Theme.surface; border.color: Theme.lineStrong }
        contentItem: ColumnLayout {
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                ActionButton {
                    iconName: "left"; hint: "上个月"; quiet: true; implicitWidth: 32; implicitHeight: 32
                    onClicked: picker.month = new Date(picker.month.getFullYear(), picker.month.getMonth() - 1, 1)
                }
                Text { Layout.fillWidth: true; text: Qt.formatDate(picker.month, "yyyy年M月"); horizontalAlignment: Text.AlignHCenter; font.pixelSize: 13; font.weight: Font.Medium; color: Theme.text }
                ActionButton {
                    iconName: "right"; hint: "下个月"; quiet: true; implicitWidth: 32; implicitHeight: 32
                    onClicked: picker.month = new Date(picker.month.getFullYear(), picker.month.getMonth() + 1, 1)
                }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 7; columnSpacing: 4; rowSpacing: 4
                Repeater {
                    model: ["一", "二", "三", "四", "五", "六", "日"]
                    delegate: Text {
                        required property string modelData
                        Layout.fillWidth: true; Layout.preferredHeight: 20
                        text: modelData; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 10; color: Theme.muted
                    }
                }
                Repeater {
                    model: 42
                    delegate: ActionButton {
                        id: dayButton
                        required property int index
                        readonly property date day: new Date(picker.month.getFullYear(), picker.month.getMonth(), index + 1 - picker.leadingDays)
                        readonly property string dateValue: Qt.formatDate(day, "yyyy-MM-dd")
                        readonly property bool selected: dateValue === field.value
                        readonly property bool today: dateValue === picker.today
                        Layout.fillWidth: true; implicitWidth: 0; implicitHeight: 32
                        padding: 0; quiet: true
                        Accessible.name: Qt.formatDate(day, "yyyy年M月d日 dddd")
                        onClicked: field.choose(day)
                        contentItem: Text {
                            text: dayButton.day.getDate()
                            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                            font.pixelSize: 12; font.weight: dayButton.selected || dayButton.today ? Font.DemiBold : Font.Normal
                            color: dayButton.selected ? Theme.accentText : (dayButton.day.getMonth() === picker.month.getMonth() ? Theme.text : Theme.muted)
                        }
                        background: Rectangle {
                            radius: 8
                            color: dayButton.selected ? Theme.accent : (dayButton.hovered ? Theme.hover : (dayButton.today ? Theme.accentSoft : "transparent"))
                            border.width: dayButton.visualFocus ? 1 : 0
                            border.color: Theme.accent
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                ActionButton { text: "今天"; quiet: true; implicitHeight: 32; onClicked: field.choose(new Date()) }
                Item { Layout.fillWidth: true }
                ActionButton { text: "取消"; quiet: true; implicitHeight: 32; onClicked: picker.close() }
            }
        }
    }
}
