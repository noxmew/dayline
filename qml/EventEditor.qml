import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Dayline 1.0

ApplicationWindow {
    id: editor
    objectName: "eventEditor"
    required property Window originWindow
    property string eventId: ""
    property var calendarOptions: []
    property bool originalCloudEvent: false
    property string chosenCalendarId: ""
    property string chosenCalendarName: ""
    property bool readOnlyEvent: false
    property string readOnlyReason: ""
    property bool cloudEvent: false
    property string syncState: ""
    transientParent: null
    title: readOnlyEvent ? "日程详情 · 日笺" : eventId.length ? "编辑日程 · 日笺" : "新建日程 · 日笺"
    width: 520
    height: Math.min(Math.ceil(form.implicitHeight) + 52 + editorScroll.topPadding + editorScroll.bottomPadding, Math.max(420, Screen.desktopAvailableHeight - 80))
    minimumWidth: 480
    minimumHeight: Math.min(520, Math.max(420, Screen.desktopAvailableHeight - 80))
    visible: false
    color: Theme.surface
    background: Rectangle { color: Theme.surface }

    function open() {
        WindowPlacement.center(editor, originWindow)
        show(); raise(); requestActivate()
    }
    onClosing: deleteDialog.close()
    Shortcut { sequence: "Escape"; onActivated: editor.close() }
    Connections {
        target: calendarStore
        function onChanged() { if (editor.visible) editor.refreshCalendarOptions() }
    }

    function refreshCalendarOptions() {
        var options = eventId.length > 0 && !originalCloudEvent
            ? calendarStore.localCalendars.map(c => ({id:c.id, name:c.name + " · 本机"}))
            : calendarStore.writableCalendars
        var selected = chosenCalendarId
        if (eventId.length > 0 && !originalCloudEvent && !options.some(c => c.id === selected))
            selected = calendarStore.eventCalendar(eventId)
        if (readOnlyEvent && originalCloudEvent && selected.length && !options.some(c => c.id === selected))
            options = options.concat([{id:selected, name:chosenCalendarName}])
        calendarOptions = options
        calendarField.currentIndex = options.findIndex(c => c.id === selected)
        chosenCalendarId = calendarField.currentIndex >= 0 ? selected : ""
        cloudEvent = chosenCalendarId.indexOf("icloud:") === 0
    }

    function create(date) {
        calendarStore.clearError()
        eventId = ""
        originalCloudEvent = false
        readOnlyEvent = false
        readOnlyReason = ""
        syncState = "待保存"
        titleField.text = ""
        dateField.text = date
        endDateField.text = date
        locationField.text = ""
        allDay.checked = true
        startField.text = "09:00"
        endField.text = "10:00"
        calendarOptions = calendarStore.writableCalendars
        var preferred = calendarOptions.findIndex(c => c.id === iCloudSync.defaultCalendar)
        calendarField.currentIndex = preferred >= 0 ? preferred : 0
        chosenCalendarId = calendarOptions.length > 0 ? calendarOptions[calendarField.currentIndex].id : ""
        cloudEvent = calendarOptions.length > 0 && calendarOptions[calendarField.currentIndex].id.indexOf("icloud:") === 0
        notesField.text = ""
        open()
        titleField.forceActiveFocus()
    }
    function edit(event) {
        calendarStore.clearError()
        eventId = event.id
        originalCloudEvent = !!event.cloud
        readOnlyEvent = !!event.readOnly
        readOnlyReason = event.readOnlyReason || ""
        cloudEvent = !!event.cloud
        syncState = event.syncState || "本机日程"
        titleField.text = event.title
        dateField.text = event.date
        endDateField.text = event.endDate || event.date
        locationField.text = event.location || ""
        allDay.checked = event.allDay
        startField.text = event.allDay ? "09:00" : event.start
        endField.text = event.allDay ? "10:00" : event.end
        chosenCalendarId = event.calendar
        chosenCalendarName = event.calendarName
        refreshCalendarOptions()
        notesField.text = event.notes
        open()
    }
    function save() {
        if (readOnlyEvent || calendarField.currentIndex < 0) return
        if (calendarStore.saveEvent({id:eventId, title:titleField.text, date:dateField.text,
            allDay:allDay.checked, start:startField.text, end:endField.text,
            endDate:endDateField.text, location:locationField.text,
            calendar:calendarOptions[calendarField.currentIndex].id, notes:notesField.text})) close()
    }
    ScrollView {
        id: editorScroll
        anchors.fill: parent
        anchors.margins: 26
        contentWidth: availableWidth
        contentHeight: Math.ceil(form.implicitHeight)
        rightPadding: editorBar.visible ? editorBar.width + 8 : 0
        bottomPadding: 8
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: SlimScrollBar {
            id: editorBar
            parent: editorScroll
            policy: ScrollBar.AsNeeded
            visible: editorScroll.contentHeight > editorScroll.availableHeight + 1
            x: editorScroll.width - width
            y: editorScroll.topPadding
            height: editorScroll.availableHeight
        }
    ColumnLayout {
        id: form
        width: editorScroll.availableWidth
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Rectangle { width: 42; height: 42; radius: 12; color: Theme.accentSoft; Icon { anchors.centerIn: parent; name: editor.readOnlyEvent ? "lock" : "note"; width: 22; height: 22; color: Theme.accent } }
            Text { Layout.fillWidth: true; text: editor.readOnlyEvent ? "日程详情" : editor.eventId.length ? "编辑日程" : "新建日程"; color: Theme.text; font.pixelSize: 21; font.weight: Font.DemiBold }
        }
        Field { id: titleField; readOnly: editor.readOnlyEvent; Layout.fillWidth: true; placeholderText: "日程标题"; font.pixelSize: 15; implicitHeight: 48; maximumLength: editor.originalCloudEvent && editor.eventId.length > 0 ? 2147483647 : 200 }
        RowLayout {
            Layout.fillWidth: true; spacing: 14
            ColumnLayout {
                Layout.fillWidth: true; spacing: 7
                Text { text: editor.cloudEvent ? "开始日期" : "日期"; color: Theme.secondary; font.pixelSize: 11 }
                Field { id: dateField; readOnly: editor.readOnlyEvent; Layout.fillWidth: true; inputMask: "0000-00-00"; placeholderText: "YYYY-MM-DD" }
            }
            ColumnLayout {
                Layout.preferredWidth: 150; spacing: 7
                Text { text: "日历"; color: Theme.secondary; font.pixelSize: 11 }
                SelectField {
                    id: calendarField
                    Layout.fillWidth: true; implicitHeight: 42
                    model: editor.calendarOptions; textRole: "name"
                    displayText: currentIndex < 0 ? "请选择日历" : currentText
                    enabled: !editor.readOnlyEvent && (editor.eventId.length === 0 || !editor.originalCloudEvent)
                    onActivated: {
                        editor.chosenCalendarId = editor.calendarOptions[currentIndex].id
                        editor.chosenCalendarName = editor.calendarOptions[currentIndex].name
                        editor.cloudEvent = editor.chosenCalendarId.indexOf("icloud:") === 0
                    }
                }
            }
        }
        RowLayout {
            visible: editor.cloudEvent
            Layout.fillWidth: true; spacing: 12
            ColumnLayout {
                Layout.fillWidth: true; spacing: 7
                Text { text: allDay.checked ? "结束日期（包含这一天）" : "结束日期"; color: Theme.secondary; font.pixelSize: 11 }
                Field { id: endDateField; readOnly: editor.readOnlyEvent; Layout.fillWidth: true; inputMask: "0000-00-00" }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 7
                Text { text: "地点"; color: Theme.secondary; font.pixelSize: 11 }
                Field { id: locationField; readOnly: editor.readOnlyEvent; Layout.fillWidth: true; placeholderText: "添加地点" }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Text { text: "全天日程"; color: Theme.text; font.pixelSize: 13 }
            Item { Layout.fillWidth: true }
            Toggle { id: allDay; checked: true; enabled: !editor.readOnlyEvent; Accessible.name: "全天日程" }
        }
        RowLayout {
            visible: !allDay.checked
            Layout.fillWidth: true; spacing: 12
            ColumnLayout {
                Layout.fillWidth: true; spacing: 7
                Text { text: "开始时间"; color: Theme.secondary; font.pixelSize: 11 }
                Field { id: startField; readOnly: editor.readOnlyEvent; Layout.fillWidth: true; inputMask: "00:00"; text: "09:00" }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 7
                Text { text: "结束时间"; color: Theme.secondary; font.pixelSize: 11 }
                Field { id: endField; readOnly: editor.readOnlyEvent; Layout.fillWidth: true; inputMask: "00:00"; text: "10:00" }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; spacing: 7
            Text { text: "备注"; color: Theme.secondary; font.pixelSize: 11 }
            ScrollView {
                id: notesScroll
                Layout.fillWidth: true; Layout.preferredHeight: 110
                contentWidth: availableWidth
                rightPadding: notesBar.visible ? notesBar.width + 6 : 0
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical: SlimScrollBar {
                    id: notesBar
                    parent: notesScroll
                    policy: ScrollBar.AsNeeded
                    visible: notesField.contentHeight + notesField.topPadding + notesField.bottomPadding > notesScroll.availableHeight + 1
                    x: notesScroll.width - width
                    y: notesScroll.topPadding
                    height: notesScroll.availableHeight
                }
                TextArea {
                    id: notesField
                    width: notesScroll.availableWidth
                    readOnly: editor.readOnlyEvent
                    placeholderText: "添加备注"
                    wrapMode: TextEdit.Wrap
                    padding: 12
                    font.pixelSize: 12
                    color: Theme.text
                    placeholderTextColor: Theme.muted
                    selectionColor: Theme.accentSoft
                    selectedTextColor: Theme.text
                    selectByMouse: true
                    background: Rectangle { radius: 9; color: Theme.input; border.color: notesField.activeFocus ? Theme.accent : Theme.line }
                }
            }
        }
        InlineNotice { visible: calendarStore.error.length > 0; text: calendarStore.error; error: true; Layout.fillWidth: true }
        RowLayout {
            visible: editor.cloudEvent
            Layout.fillWidth: true
            Icon { name: "cloud"; width: 14; height: 14; color: Theme.muted }
            Text { Layout.fillWidth: true; text: "iCloud · " + editor.syncState; color: Theme.muted; font.pixelSize: 11; wrapMode: Text.Wrap }
        }
        InlineNotice { Layout.fillWidth: true; visible: editor.readOnlyEvent; text: editor.readOnlyReason; iconName: "lock" }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }
        RowLayout {
            Layout.fillWidth: true
            ActionButton { visible: editor.eventId.length > 0 && !editor.readOnlyEvent; iconName: "trash"; quiet: true; danger: true; hint: "删除日程"; onClicked: deleteDialog.open() }
            Item { Layout.fillWidth: true }
            ActionButton { text: editor.readOnlyEvent ? "关闭" : "取消"; quiet: true; onClicked: editor.close() }
            ActionButton { visible: !editor.readOnlyEvent; text: "保存日程"; iconName: "check"; primary: true; enabled: calendarField.currentIndex >= 0; onClicked: editor.save() }
        }
    }
    }
    Dialog {
        id: deleteDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 380; padding: 24
        modal: true
        background: Rectangle { radius: 16; color: Theme.surface; border.color: Theme.lineStrong }
        Overlay.modal: Rectangle { color: Theme.overlay }
        contentItem: ColumnLayout {
            spacing: 18
            Rectangle { width: 40; height: 40; radius: 12; color: Theme.dangerSoft; Icon { anchors.centerIn: parent; name: "trash"; width: 21; height: 21; color: Theme.danger } }
            Text { text: "删除日程？"; font.pixelSize: 19; font.weight: Font.DemiBold; color: Theme.text }
            Text { text: editor.cloudEvent ? "这项日程也会从 iCloud 和其他设备中删除。" : "日程将从本机删除，无法撤销。"; font.pixelSize: 12; color: Theme.secondary; Layout.fillWidth: true; wrapMode: Text.Wrap; lineHeight: 1.5 }
            RowLayout {
                Item { Layout.fillWidth: true }
                ActionButton { text: "取消"; onClicked: deleteDialog.close() }
                ActionButton { text: "删除日程"; danger: true; primary: true; onClicked: { if (calendarStore.deleteEvent(editor.eventId)) { deleteDialog.close(); editor.close() } } }
            }
        }
    }
}
