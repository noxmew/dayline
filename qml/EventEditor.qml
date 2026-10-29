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
    property bool recurringEvent: false
    property string recurrenceId: ""
    property var originalOccurrence: ({})
    property var originalSeries: ({})
    property bool loading: false
    property string previousStartDate: ""
    property string previousStartTime: "09:00"
    property string formError: ""
    property var pendingFields: ({})
    property bool pendingRuleChange: false
    property bool pendingTimingChange: false
    property bool pendingCalendarChange: false
    readonly property bool seriesHasExceptions: recurringEvent && !!originalSeries.hasExceptions
    readonly property bool workdayRange: workdayToggle.checked && recurrenceField.frequency === "none"
    readonly property bool requiresSeries: pendingRuleChange || pendingCalendarChange
    readonly property bool canSaveSeries: !seriesHasExceptions || (!pendingTimingChange && !pendingRuleChange)
    readonly property bool pickerVisible: dateField.pickerVisible || endDateField.pickerVisible
        || calendarField.popup.visible || recurrenceField.pickerVisible || scopeDialog.visible
    transientParent: null
    title: (readOnlyEvent ? "日程详情" : eventId.length ? "编辑日程" : "新建日程") + " · 日笺"
    width: 520
    height: Math.min(680, Math.max(420, Screen.desktopAvailableHeight - 72))
    minimumWidth: 480
    minimumHeight: Math.min(500, Math.max(420, Screen.desktopAvailableHeight - 72))
    visible: false
    color: Theme.panel
    background: Rectangle { color: Theme.panel }

    function open() {
        editorScroll.contentItem.contentY = 0
        WindowPlacement.center(editor, originWindow)
        show(); raise(); requestActivate()
    }
    onClosing: {
        dateField.closePicker()
        endDateField.closePicker()
        calendarField.popup.close()
        recurrenceField.closePickers()
        scopeDialog.close()
    }
    Shortcut { sequence: "Escape"; enabled: !editor.pickerVisible; onActivated: editor.close() }
    Shortcut { sequence: "Ctrl+S"; enabled: !editor.readOnlyEvent && !editor.pickerVisible; onActivated: editor.requestSave() }
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
    function dayNumber(value) {
        const parts = value.split("-").map(Number)
        return Date.UTC(parts[0], parts[1] - 1, parts[2]) / 86400000
    }
    function shiftDate(value, days) {
        const parts = value.split("-").map(Number)
        const date = new Date(parts[0], parts[1] - 1, parts[2] + days, 12)
        return Qt.formatDate(date, "yyyy-MM-dd")
    }
    function minutes(value) { return Number(value.slice(0, 2)) * 60 + Number(value.slice(3, 5)) }
    function clockText(value) {
        const adjusted = (value % 1440 + 1440) % 1440
        return String(Math.floor(adjusted / 60)).padStart(2, "0") + ":" + String(adjusted % 60).padStart(2, "0")
    }
    function startDateChanged() {
        if (loading) return
        if (previousStartDate.length) endDateField.value = shiftDate(endDateField.value, dayNumber(dateField.value) - dayNumber(previousStartDate))
        previousStartDate = dateField.value
    }
    function alignStartTime() {
        if (loading || allDay.checked || !startField.acceptableInput || !endField.acceptableInput) return
        const delta = minutes(startField.value) - minutes(previousStartTime)
        if (delta !== 0) {
            const end = minutes(endField.value) + delta
            if (!workdayToggle.checked) endDateField.value = shiftDate(endDateField.value, Math.floor(end / 1440))
            endField.value = clockText(end)
        }
        previousStartTime = startField.value
    }
    function create(date) {
        calendarStore.clearError()
        formError = ""
        eventId = ""
        originalCloudEvent = false
        readOnlyEvent = false
        readOnlyReason = ""
        recurringEvent = false
        recurrenceId = ""
        originalOccurrence = ({})
        originalSeries = ({})
        loading = true
        syncState = "待保存"
        titleField.text = ""
        dateField.value = date
        endDateField.value = date
        previousStartDate = date
        locationField.text = ""
        allDay.checked = false
        workdayToggle.checked = false
        startField.value = "09:00"
        endField.value = "10:00"
        previousStartTime = "09:00"
        calendarOptions = calendarStore.writableCalendars
        var preferred = calendarOptions.findIndex(c => c.id === iCloudSync.defaultCalendar)
        calendarField.currentIndex = preferred >= 0 ? preferred : 0
        chosenCalendarId = calendarOptions.length > 0 ? calendarOptions[calendarField.currentIndex].id : ""
        cloudEvent = chosenCalendarId.indexOf("icloud:") === 0
        notesField.text = ""
        recurrenceField.load({frequency:"none"})
        loading = false
        open()
        titleField.forceActiveFocus()
    }
    function edit(event) {
        calendarStore.clearError()
        formError = ""
        eventId = event.id
        originalCloudEvent = !!event.cloud
        readOnlyEvent = !!event.readOnly
        readOnlyReason = event.readOnlyReason || ""
        cloudEvent = !!event.cloud
        syncState = event.syncState || "本机日程"
        recurringEvent = !!event.recurring
        recurrenceId = event.recurrenceId || ""
        originalOccurrence = event
        originalSeries = event.series || ({})
        loading = true
        titleField.text = event.title
        dateField.value = event.date
        endDateField.value = event.endDate || event.date
        previousStartDate = event.date
        locationField.text = event.location || ""
        allDay.checked = event.allDay
        workdayToggle.checked = !!(event.recurrence && event.recurrence.workdaysOnly)
        startField.value = event.allDay ? "09:00" : event.start
        endField.value = event.allDay ? "10:00" : event.end
        previousStartTime = startField.value
        notesField.text = event.notes
        recurrenceField.load(event.recurrence || {frequency:"none"})
        loading = false
        chosenCalendarId = event.calendar
        chosenCalendarName = event.calendarName
        refreshCalendarOptions()
        open()
    }
    function ruleKey(rule) {
        if (!rule || rule.frequency === "none") return JSON.stringify(["none", !!(rule && rule.workdaysOnly)])
        return JSON.stringify([rule.frequency, Number(rule.interval || 1), (rule.weekdays || []).slice().sort((a,b) => a-b),
            rule.endMode || "never", rule.endMode === "until" ? rule.until : "", rule.endMode === "count" ? Number(rule.count) : 1,
            rule.weekStart || "MO", !!rule.workdaysOnly])
    }
    function timingChanged(fields) {
        return ["date", "endDate", "allDay"].some(key => fields[key] !== originalOccurrence[key])
            || (!fields.allDay && ["start", "end"].some(key => fields[key] !== originalOccurrence[key]))
    }
    function requestSave() {
        if (readOnlyEvent || calendarField.currentIndex < 0) return
        Qt.inputMethod.commit()
        alignStartTime()
        formError = ""
        calendarStore.clearError()
        if (!titleField.text.trim().length) { formError = "请填写日程标题。"; titleField.forceActiveFocus(); return }
        if (endDateField.value < dateField.value || (!allDay.checked && (!startField.acceptableInput || !endField.acceptableInput
            || (endDateField.value === dateField.value && endField.value <= startField.value)))) {
            formError = "结束时间需要晚于开始时间。"; return
        }
        let recurrence = recurrenceField.value()
        if (workdayToggle.checked) {
            if (!allDay.checked && endField.value <= startField.value) {
                formError = "仅工作日安排需要在当天结束，请将每天的结束时间设在开始时间之后。"; return
            }
            if (workdayRange) {
                recurrence = {frequency:"daily", interval:1, weekdays:[], endMode:"until", until:endDateField.value, count:1, weekStart:"MO"}
            } else {
                if (endDateField.value !== dateField.value) {
                    formError = "仅工作日的每次重复需要在当天结束。若要按日期范围逐日安排，请把重复设为“从不”。"; return
                }
                if (recurrence.endMode === "never") {
                    formError = "仅工作日需要设置结束日期或次数；未来年份的节假日安排尚未确定。"; return
                }
            }
        }
        recurrence.workdaysOnly = workdayToggle.checked
        pendingFields = {id:eventId, title:titleField.text, date:dateField.value, allDay:allDay.checked,
            start:startField.value, end:endField.value, endDate:workdayRange ? dateField.value : endDateField.value, location:locationField.text,
            calendar:calendarOptions[calendarField.currentIndex].id, notes:notesField.text,
            recurrence:recurrence, recurrenceId:recurrenceId}
        pendingRuleChange = ruleKey(pendingFields.recurrence) !== ruleKey(originalOccurrence.recurrence)
        pendingTimingChange = timingChanged(pendingFields)
        pendingCalendarChange = pendingFields.calendar !== originalOccurrence.calendar
        if (eventId.length && !pendingRuleChange && !pendingTimingChange && !pendingCalendarChange
            && ["title", "location", "notes"].every(key => pendingFields[key] === (originalOccurrence[key] || ""))) {
            close(); return
        }
        if (!recurringEvent) { save("series"); return }
        scopeDialog.deleting = false
        scopeDialog.open()
    }
    function save(scope) {
        let fields = Object.assign({}, pendingFields, {scope:scope})
        if (recurringEvent && scope === "series") {
            // Map relative date changes to the first occurrence, keeping the
            // series anchor and untouched exception fields out of this edit.
            if (pendingTimingChange) {
                if (!fields.allDay && !originalSeries.allDay && !originalOccurrence.allDay) {
                    for (const pair of [["date", "start"], ["endDate", "end"]]) {
                        const dayKey = pair[0], timeKey = pair[1]
                        const deltaDays = dayNumber(fields[dayKey]) - dayNumber(originalOccurrence[dayKey])
                        const changedTime = minutes(originalSeries[timeKey]) + minutes(fields[timeKey]) - minutes(originalOccurrence[timeKey])
                        fields[dayKey] = shiftDate(originalSeries[dayKey], deltaDays + Math.floor(changedTime / 1440))
                        fields[timeKey] = clockText(changedTime)
                    }
                } else {
                    const offset = dayNumber(originalSeries.date) - dayNumber(originalOccurrence.date)
                    fields.date = shiftDate(fields.date, offset)
                    fields.endDate = shiftDate(fields.endDate, offset)
                }
                if (!pendingRuleChange && fields.recurrence.frequency === "weekly"
                    && fields.recurrence.weekdays.length === 1
                    && fields.recurrence.weekdays[0] === recurrenceField.weekday(originalSeries.date)) {
                    fields.recurrence = Object.assign({}, fields.recurrence, {weekdays:[recurrenceField.weekday(fields.date)]})
                }
            } else {
                for (const key of ["date", "endDate", "allDay", "start", "end"]) fields[key] = originalSeries[key]
            }
            for (const key of ["title", "location", "notes"]) {
                if (fields[key] === (originalOccurrence[key] || "")) fields[key] = originalSeries[key] || ""
            }
        }
        if (calendarStore.saveEvent(fields)) { scopeDialog.close(); close() }
    }
    function requestDelete() {
        formError = ""
        calendarStore.clearError()
        scopeDialog.deleting = true
        scopeDialog.open()
    }
    function remove(scope) {
        if (calendarStore.deleteEvent(eventId, recurringEvent && scope === "occurrence" ? recurrenceId : "")) {
            scopeDialog.close(); close()
        }
    }

    Item {
        id: toolbar
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: 64
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
            ActionButton { text: editor.readOnlyEvent ? "关闭" : "取消"; quiet: true; Layout.preferredWidth: 66; onClicked: editor.close() }
            Text { Layout.fillWidth: true; text: editor.readOnlyEvent ? "日程详情" : editor.eventId.length ? "编辑日程" : "新建日程"; horizontalAlignment: Text.AlignHCenter; color: Theme.text; font.pixelSize: 17; font.weight: Font.DemiBold }
            ActionButton { visible: !editor.readOnlyEvent; text: editor.eventId.length ? "完成" : "添加"; primary: true; Layout.preferredWidth: 66; enabled: calendarField.currentIndex >= 0 && titleField.text.trim().length > 0; onClicked: editor.requestSave() }
            Item { visible: editor.readOnlyEvent; Layout.preferredWidth: 66 }
        }
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Theme.line }
    }
    ScrollView {
        id: editorScroll
        anchors.top: toolbar.bottom; anchors.bottom: errorArea.top
        anchors.left: parent.left; anchors.right: parent.right
        anchors.margins: 20
        contentWidth: availableWidth
        contentHeight: Math.ceil(form.implicitHeight)
        rightPadding: editorBar.visible ? editorBar.width + 8 : 0
        bottomPadding: 4
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: SlimScrollBar {
            id: editorBar
            parent: editorScroll
            policy: ScrollBar.AsNeeded
            visible: editorScroll.contentHeight > editorScroll.availableHeight + 1
            x: editorScroll.width - width; y: editorScroll.topPadding; height: editorScroll.availableHeight
        }
        ColumnLayout {
            id: form
            width: editorScroll.availableWidth
            spacing: 16
            EditorGroup {
                Layout.fillWidth: true
                EditorRow {
                    Layout.fillWidth: true; separator: true
                    Field {
                        id: titleField
                        Layout.fillWidth: true; implicitHeight: 36; padding: 0
                        readOnly: editor.readOnlyEvent; placeholderText: "标题"; font.pixelSize: 16
                        maximumLength: editor.originalCloudEvent && editor.eventId.length > 0 ? 2147483647 : 200
                        background: Item { }
                        Accessible.name: "日程标题"
                    }
                }
                EditorRow {
                    Layout.fillWidth: true
                    Field {
                        id: locationField
                        Layout.fillWidth: true; implicitHeight: 36; padding: 0
                        readOnly: editor.readOnlyEvent; placeholderText: "地点"
                        background: Item { }
                        Accessible.name: "日程地点"
                    }
                }
            }
            EditorGroup {
                Layout.fillWidth: true
                EditorRow {
                    Layout.fillWidth: true; label: "全天"; separator: true
                    Item { Layout.fillWidth: true }
                    Toggle { id: allDay; checked: false; enabled: !editor.readOnlyEvent; Accessible.name: "全天日程" }
                }
                EditorRow {
                    Layout.fillWidth: true; label: "开始"; separator: true
                    Item { Layout.fillWidth: true }
                    DateField { id: dateField; compact: true; readOnly: editor.readOnlyEvent; label: "开始日期"; onValueChanged: editor.startDateChanged() }
                    TimeField { id: startField; visible: !allDay.checked; readOnly: editor.readOnlyEvent; label: "开始时间"; onEditingFinished: editor.alignStartTime() }
                }
                EditorRow {
                    Layout.fillWidth: true; label: "结束"; separator: true
                    Item { Layout.fillWidth: true }
                    DateField { id: endDateField; compact: true; readOnly: editor.readOnlyEvent; label: "结束日期" }
                    TimeField { id: endField; visible: !allDay.checked; readOnly: editor.readOnlyEvent; label: "结束时间"; value: "10:00" }
                }
                EditorRow {
                    Layout.fillWidth: true; label: "仅工作日"
                    Item { Layout.fillWidth: true }
                    Toggle {
                        id: workdayToggle
                        objectName: "workdaysOnly"
                        checked: false
                        enabled: !editor.readOnlyEvent && !editor.seriesHasExceptions
                        Accessible.name: "仅工作日，排除周末和法定假期，保留调休补班日"
                    }
                }
            }
            Text {
                visible: workdayToggle.checked
                Layout.fillWidth: true; Layout.leftMargin: 4; Layout.rightMargin: 4
                text: (editor.workdayRange ? "起止日期内按相同时间逐日安排，保存为每日重复日程。" : "请设置结束日期或次数。")
                    + "跳过周末和法定假期，保留调休补班日。"
                color: Theme.secondary; font.pixelSize: 11; wrapMode: Text.Wrap
            }
            RecurrenceEditor {
                id: recurrenceField
                Layout.fillWidth: true
                startDate: editor.recurringEvent && editor.originalSeries.date
                    ? editor.shiftDate(dateField.value, editor.dayNumber(editor.originalSeries.date) - editor.dayNumber(editor.originalOccurrence.date))
                    : dateField.value
                followStartDate: !editor.recurringEvent
                workdaysOnly: workdayToggle.checked
                readOnly: editor.readOnlyEvent || editor.seriesHasExceptions
            }
            Text {
                visible: editor.seriesHasExceptions && !editor.readOnlyEvent
                Layout.fillWidth: true; Layout.leftMargin: 4; Layout.rightMargin: 4
                text: "此系列已单独修改过部分日程。可调整此次时间；重复规则暂不能修改。"
                color: Theme.secondary; font.pixelSize: 11; wrapMode: Text.Wrap
            }
            EditorGroup {
                Layout.fillWidth: true
                EditorRow {
                    Layout.fillWidth: true; label: "日历"
                    SelectField {
                        id: calendarField
                        Layout.fillWidth: true; implicitHeight: 36
                        model: editor.calendarOptions; textRole: "name"
                        displayText: currentIndex < 0 ? "请选择日历" : currentText
                        enabled: !editor.readOnlyEvent && (editor.eventId.length === 0 || !editor.originalCloudEvent)
                        contentItem: Text {
                            text: calendarField.displayText
                            font: calendarField.font
                            color: Theme.secondary
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        onActivated: {
                            editor.chosenCalendarId = editor.calendarOptions[currentIndex].id
                            editor.chosenCalendarName = editor.calendarOptions[currentIndex].name
                            editor.cloudEvent = editor.chosenCalendarId.indexOf("icloud:") === 0
                        }
                        background: Item { }
                        Accessible.name: "日程所属日历"
                    }
                }
            }
            EditorGroup {
                Layout.fillWidth: true
                padding: 14
                ScrollView {
                    id: notesScroll
                    Layout.fillWidth: true; Layout.preferredHeight: 80
                    contentWidth: availableWidth
                    rightPadding: notesBar.visible ? notesBar.width + 6 : 0
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ScrollBar.vertical: SlimScrollBar {
                        id: notesBar
                        parent: notesScroll
                        policy: ScrollBar.AsNeeded
                        visible: notesField.contentHeight + notesField.topPadding + notesField.bottomPadding > notesScroll.availableHeight + 1
                        x: notesScroll.width - width; y: notesScroll.topPadding; height: notesScroll.availableHeight
                    }
                    TextArea {
                        id: notesField
                        width: notesScroll.availableWidth
                        readOnly: editor.readOnlyEvent; placeholderText: "备注"
                        wrapMode: TextEdit.Wrap; padding: 0; font.pixelSize: 13
                        color: Theme.text; placeholderTextColor: Theme.muted
                        selectionColor: Theme.accentSoft; selectedTextColor: Theme.text; selectByMouse: true
                        textFormat: TextEdit.PlainText
                        background: Item { }
                        Accessible.name: "日程备注"
                    }
                }
            }
            RowLayout {
                visible: editor.cloudEvent
                Layout.fillWidth: true; Layout.leftMargin: 4
                Icon { name: "cloud"; width: 14; height: 14; color: Theme.muted }
                Text { Layout.fillWidth: true; text: "iCloud · " + editor.syncState; color: Theme.muted; font.pixelSize: 11; wrapMode: Text.Wrap }
            }
            InlineNotice { Layout.fillWidth: true; visible: editor.readOnlyEvent; text: editor.readOnlyReason; iconName: "lock" }
            EditorGroup {
                visible: editor.eventId.length > 0 && !editor.readOnlyEvent
                Layout.fillWidth: true
                EditorRow {
                    Layout.fillWidth: true
                    ActionButton { Layout.fillWidth: true; text: "删除日程"; quiet: true; danger: true; onClicked: editor.requestDelete() }
                }
            }
        }
    }
    Item {
        id: errorArea
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: errorNotice.visible ? errorNotice.implicitHeight + 24 : 0
        InlineNotice {
            id: errorNotice
            anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 20
            anchors.verticalCenter: parent.verticalCenter
            visible: text.length > 0
            text: editor.formError || calendarStore.error
            error: true
        }
    }
    Dialog {
        id: scopeDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(420, parent.width - 32)
        padding: 20
        modal: true; focus: true
        closePolicy: Popup.CloseOnEscape
        property bool deleting: false
        background: Rectangle { radius: 16; color: Theme.panel; border.color: Theme.lineStrong }
        Overlay.modal: Rectangle { color: Theme.overlay }
        contentItem: ColumnLayout {
            spacing: 16
            Text {
                Layout.fillWidth: true
                text: scopeDialog.deleting ? "删除日程" : "保存更改"
                color: Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
            }
            Text {
                Layout.fillWidth: true
                text: editor.recurringEvent ? "这是一个重复日程。你想将更改应用到哪里？" : "删除后无法撤销。"
                color: Theme.secondary; font.pixelSize: 12; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
            }
            InlineNotice {
                visible: !scopeDialog.deleting && editor.requiresSeries
                Layout.fillWidth: true
                text: editor.pendingRuleChange ? "重复规则的更改将应用到整个系列。" : "更换日历将移动整个系列。"
            }
            InlineNotice {
                visible: !scopeDialog.deleting && !editor.canSaveSeries
                Layout.fillWidth: true
                text: editor.requiresSeries ? "此系列已有单次修改。请保留此次原有时间，再保存整个系列。" : "此系列已有单次修改，时间更改仅能保存到此次日程。"
            }
            EditorGroup {
                Layout.fillWidth: true
                EditorRow {
                    visible: editor.recurringEvent
                    Layout.fillWidth: true; separator: true
                    ActionButton {
                        Layout.fillWidth: true; quiet: true; danger: scopeDialog.deleting
                        text: scopeDialog.deleting ? "仅删除此次日程" : "仅保存此次日程"
                        enabled: scopeDialog.deleting || !editor.requiresSeries
                        onClicked: scopeDialog.deleting ? editor.remove("occurrence") : editor.save("occurrence")
                    }
                }
                EditorRow {
                    Layout.fillWidth: true
                    ActionButton {
                        Layout.fillWidth: true; quiet: true; danger: scopeDialog.deleting
                        text: scopeDialog.deleting ? editor.recurringEvent ? "删除整个系列" : "删除日程" : "保存整个系列"
                        enabled: scopeDialog.deleting || editor.canSaveSeries
                        onClicked: scopeDialog.deleting ? editor.remove("series") : editor.save("series")
                    }
                }
            }
            InlineNotice { visible: calendarStore.error.length > 0; text: calendarStore.error; error: true; Layout.fillWidth: true }
            ActionButton { Layout.fillWidth: true; text: "取消"; quiet: true; onClicked: scopeDialog.close() }
        }
    }
}
