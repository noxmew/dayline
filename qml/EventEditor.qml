import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Dayline 1.0

Dialog {
    id: editor
    property string eventId: ""
    property var calendarOptions: [ {name:"工作",id:"work"}, {name:"生活",id:"life"}, {name:"灵感",id:"ideas"} ]
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 490
    modal: true
    focus: true
    padding: 26
    closePolicy: Popup.CloseOnEscape
    background: Rectangle { radius: 18; color: Theme.surface; border.color: Theme.line }
    Overlay.modal: Rectangle { color: "#650E211A" }

    function create(date) {
        calendarStore.clearError()
        eventId = ""
        titleField.text = ""
        dateField.text = date
        allDay.checked = true
        startField.text = "09:00"
        endField.text = "10:00"
        calendarField.currentIndex = 0
        notesField.text = ""
        open()
        titleField.forceActiveFocus()
    }
    function edit(event) {
        calendarStore.clearError()
        eventId = event.id
        titleField.text = event.title
        dateField.text = event.date
        allDay.checked = event.allDay
        startField.text = event.allDay ? "09:00" : event.start
        endField.text = event.allDay ? "10:00" : event.end
        calendarField.currentIndex = ["work","life","ideas"].indexOf(event.calendar)
        notesField.text = event.notes
        open()
    }
    function save() {
        if (calendarStore.saveEvent({id:eventId, title:titleField.text, date:dateField.text,
            allDay:allDay.checked, start:startField.text, end:endField.text,
            calendar:calendarOptions[calendarField.currentIndex].id, notes:notesField.text})) close()
    }
    contentItem: ColumnLayout {
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 5
                Text { text: editor.eventId.length ? "编辑日程" : "记下下一件小事"; color: Theme.text; font.pixelSize: 22; font.weight: Font.DemiBold }
                Text { text: "给重要的事，留一个位置。"; color: Theme.secondary; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
            ActionButton { iconName: "close"; hint: "关闭"; quiet: true; onClicked: editor.close() }
        }
        Field { id: titleField; Layout.fillWidth: true; placeholderText: "这一天，有什么安排？"; font.pixelSize: 15; implicitHeight: 48; maximumLength: 200 }
        RowLayout {
            Layout.fillWidth: true; spacing: 14
            ColumnLayout {
                Layout.fillWidth: true; spacing: 7
                Text { text: "日期"; color: Theme.secondary; font.pixelSize: 11 }
                Field { id: dateField; Layout.fillWidth: true; inputMask: "0000-00-00"; placeholderText: "YYYY-MM-DD" }
            }
            ColumnLayout {
                Layout.preferredWidth: 150; spacing: 7
                Text { text: "日历"; color: Theme.secondary; font.pixelSize: 11 }
                ComboBox {
                    id: calendarField
                    Layout.fillWidth: true; implicitHeight: 42
                    model: editor.calendarOptions; textRole: "name"
                    font.pixelSize: 13
                    palette.text: Theme.text
                    palette.buttonText: Theme.text
                    palette.window: Theme.surface
                    palette.base: Theme.surface
                    background: Rectangle { radius: 8; color: Theme.input; border.color: Theme.line }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Text { text: "全天日程"; color: Theme.text; font.pixelSize: 13 }
            Item { Layout.fillWidth: true }
            Switch { id: allDay; palette.highlight: Theme.accent; checked: true }
        }
        RowLayout {
            visible: !allDay.checked
            Layout.fillWidth: true; spacing: 12
            Field { id: startField; Layout.fillWidth: true; inputMask: "00:00"; text: "09:00" }
            Text { text: "—"; color: Theme.muted }
            Field { id: endField; Layout.fillWidth: true; inputMask: "00:00"; text: "10:00" }
        }
        ColumnLayout {
            Layout.fillWidth: true; spacing: 7
            Text { text: "备注"; color: Theme.secondary; font.pixelSize: 11 }
            ScrollView {
                Layout.fillWidth: true; Layout.preferredHeight: 110
                TextArea {
                    id: notesField
                    placeholderText: "一些细节，或者一个突然冒出的想法…"
                    wrapMode: TextEdit.Wrap
                    padding: 12
                    font.pixelSize: 12
                    color: Theme.text
                    placeholderTextColor: Theme.muted
                    selectByMouse: true
                    background: Rectangle { radius: 8; color: Theme.input; border.color: notesField.activeFocus ? Theme.accent : Theme.line }
                }
            }
        }
        Text { visible: calendarStore.error.length > 0; text: calendarStore.error; color: Theme.danger; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            Icon { name: "note"; width: 14; height: 14; color: Theme.muted }
            Text { text: "保存到本机"; color: Theme.muted; font.pixelSize: 11 }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }
        RowLayout {
            Layout.fillWidth: true
            ActionButton { visible: editor.eventId.length > 0; iconName: "trash"; quiet: true; danger: true; hint: "删除日程"; onClicked: deleteDialog.open() }
            Item { Layout.fillWidth: true }
            ActionButton { text: "取消"; quiet: true; onClicked: editor.close() }
            ActionButton { text: "保存日程"; iconName: "check"; primary: true; onClicked: editor.save() }
        }
    }
    Dialog {
        id: deleteDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 360; padding: 24
        modal: true
        background: Rectangle { radius: 16; color: Theme.surface; border.color: Theme.line }
        contentItem: ColumnLayout {
            spacing: 18
            Text { text: "删除这项日程？"; font.pixelSize: 19; font.weight: Font.DemiBold; color: Theme.text }
            Text { text: "日程会从本机移除，此操作无法撤销。"; font.pixelSize: 12; color: Theme.secondary; Layout.fillWidth: true; wrapMode: Text.Wrap }
            RowLayout {
                Item { Layout.fillWidth: true }
                ActionButton { text: "取消"; onClicked: deleteDialog.close() }
                ActionButton { text: "删除"; danger: true; onClicked: { if (calendarStore.deleteEvent(editor.eventId)) { deleteDialog.close(); editor.close() } } }
            }
        }
    }
}
