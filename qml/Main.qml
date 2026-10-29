import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Dayline 1.0

ApplicationWindow {
    id: window
    width: 1480
    height: 930
    minimumWidth: 1220
    minimumHeight: 720
    visible: !desktopController.startOnDesktop
    title: "日笺 · Dayline" + (calendarStore.demo ? " · 示例日历" : "")
    color: Theme.background
    property string page: "calendar"
    readonly property bool showingList: page === "agenda" || calendarStore.searchQuery.length > 0
    Binding { target: Theme; property: "dark"; value: calendarStore.darkMode }

    function openNewEvent() { editor.create(calendarStore.selectedDate) }
    function openICloud() { cloudDialog.open() }
    onClosing: function(close) { close.accepted = calendarStore.saveMemo() }
    Shortcut { sequence: "Ctrl+N"; onActivated: window.openNewEvent() }
    Shortcut { sequence: "Ctrl+K"; onActivated: searchField.forceActiveFocus() }
    Shortcut { sequence: "PgUp"; onActivated: calendarStore.changeMonth(-1) }
    Shortcut { sequence: "PgDown"; onActivated: calendarStore.changeMonth(1) }
    Connections {
        target: desktopController
        function onDesktopActivationFailed() {
            if (window.visible) desktopUnavailableDialog.open()
        }
    }

    component SideNav: AbstractButton {
        id: nav
        required property string glyph
        required property string label
        property bool selected: false
        Layout.fillWidth: true
        implicitHeight: 43
        hoverEnabled: true
        Accessible.name: label
        background: Rectangle {
            radius: 9
            color: nav.selected ? Theme.accentSoft : (nav.hovered ? Theme.hover : "transparent")
            border.width: nav.visualFocus ? 1 : 0
            border.color: Theme.accent
        }
        contentItem: RowLayout {
            spacing: 12
            Icon { Layout.leftMargin: 13; name: nav.glyph; width: 18; height: 18; color: nav.selected ? Theme.accent : Theme.secondary }
            Text { text: nav.label; font.pixelSize: 13; font.weight: nav.selected ? Font.DemiBold : Font.Normal; color: nav.selected ? Theme.accent : Theme.secondary }
            Item { Layout.fillWidth: true }
            Rectangle { visible: nav.selected; Layout.rightMargin: 13; width: 5; height: 5; radius: 3; color: Theme.accent }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            color: Theme.sidebar
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.line }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 0
                RowLayout {
                    Layout.topMargin: 13
                    Layout.leftMargin: 4
                    spacing: 12
                    Rectangle {
                        width: 40; height: 40; radius: 12; color: Theme.accent
                        Icon { anchors.centerIn: parent; width: 25; height: 25; color: Theme.dark ? "#17392D" : "#FFFFFF"; name: "calendar" }
                    }
                    Column {
                        spacing: 1
                        Text { text: "日笺"; font.pixelSize: 22; font.weight: Font.DemiBold; color: Theme.text; font.letterSpacing: 3 }
                        Text { text: "D A Y L I N E"; font.pixelSize: 8; color: Theme.secondary }
                    }
                }
                Text { visible: calendarStore.demo; Layout.topMargin: 30; Layout.leftMargin: 12; text: "示例日历"; font.pixelSize: 10; color: Theme.muted }
                SideNav { Layout.topMargin: calendarStore.demo ? 12 : 32; label: "我的日历"; glyph: "calendar"; selected: window.page === "calendar"; onClicked: { window.page = "calendar"; searchField.clear() } }
                SideNav { Layout.topMargin: 5; label: "日程列表"; glyph: "list"; selected: window.page === "agenda"; onClicked: window.page = "agenda" }

                RowLayout {
                    Layout.topMargin: 31; Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 9
                    Layout.minimumHeight: 25
                    Text { text: "我的日历"; font.pixelSize: 10; color: Theme.muted }
                    Item { Layout.fillWidth: true }
                    Text { text: String(calendarStore.calendars.length).padStart(2, "0"); font.pixelSize: 10; color: Theme.muted }
                }
                ScrollView {
                    id: calendarScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: Math.min(calendarList.implicitHeight, 117)
                    Layout.preferredHeight: calendarList.implicitHeight
                    Layout.maximumHeight: calendarList.implicitHeight
                    contentWidth: availableWidth
                    contentHeight: Math.ceil(calendarList.implicitHeight)
                    rightPadding: calendarListBar.visible ? calendarListBar.width + 4 : 0
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ScrollBar.vertical: SlimScrollBar {
                        id: calendarListBar
                        parent: calendarScroll; x: calendarScroll.width - width; height: calendarScroll.availableHeight
                        policy: ScrollBar.AsNeeded
                        visible: calendarScroll.contentHeight > calendarScroll.availableHeight + 1
                    }
                ColumnLayout {
                    id: calendarList
                    width: calendarScroll.availableWidth
                    spacing: 0
                Repeater {
                    model: calendarStore.calendars
                    delegate: AbstractButton {
                        id: calendarToggle
                        required property var modelData
                        Layout.fillWidth: true; Layout.topMargin: 6
                        implicitHeight: 33; hoverEnabled: true
                        Accessible.name: modelData.name + (modelData.cloud ? " · iCloud 日历" : "日历")
                        onClicked: calendarStore.toggleCalendar(modelData.id)
                        background: Rectangle {
                            radius: 7
                            color: calendarToggle.hovered ? Theme.hover : "transparent"
                            border.width: calendarToggle.visualFocus ? 1 : 0
                            border.color: Theme.accent
                        }
                        contentItem: RowLayout {
                            spacing: 10
                            Rectangle {
                                Layout.leftMargin: 13; width: 14; height: 14; radius: 4
                                color: modelData.enabled ? modelData.color : "transparent"
                                border.color: modelData.color
                                Icon { visible: modelData.enabled; anchors.centerIn: parent; width: 11; height: 11; name: "check"; color: "white" }
                            }
                            Text { Layout.fillWidth: true; text: modelData.name; elide: Text.ElideRight; font.pixelSize: 12; color: modelData.enabled ? Theme.secondary : Theme.muted }
                            Icon { visible: !!modelData.cloud; name: "cloud"; width: 13; height: 13; color: Theme.muted }
                            Text { Layout.rightMargin: 9; text: modelData.count; font.pixelSize: 10; color: Theme.muted }
                        }
                    }
                }
                }
                }
                Rectangle { Layout.fillWidth: true; Layout.topMargin: 24; height: 1; color: Theme.line }
                Text { visible: window.height >= 850; Layout.topMargin: 23; Layout.leftMargin: 10; text: calendarStore.monthLabel; font.pixelSize: 11; color: Theme.secondary; font.weight: Font.Medium }
                Grid {
                    id: miniMonth
                    visible: window.height >= 850
                    Layout.fillWidth: true; Layout.topMargin: 13
                    columns: 7
                    Repeater {
                        model: ["一","二","三","四","五","六","日"]
                        delegate: Text { required property string modelData; width: miniMonth.width/7; height: 25; text: modelData; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 9; color: Theme.muted }
                    }
                    Repeater {
                        model: calendarStore.monthDays
                        delegate: Item {
                            required property var modelData
                            width: miniMonth.width/7; height: 26
                            Rectangle {
                                anchors.centerIn: parent; width: 24; height: 24; radius: 8
                                color: modelData.today ? Theme.accent : (modelData.date === calendarStore.selectedDate ? Theme.accentSoft : "transparent")
                                Text {
                                    anchors.centerIn: parent; text: modelData.day; font.pixelSize: 10
                                    color: modelData.today ? (Theme.dark ? "#17392D" : "white") : (modelData.inMonth ? Theme.secondary : Theme.muted)
                                    opacity: modelData.inMonth ? 1 : 0.55
                                }
                            }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: calendarStore.selectDate(modelData.date) }
                        }
                    }
                }
                Item { Layout.fillHeight: true; Layout.minimumHeight: 24 }
                RowLayout {
                    Layout.topMargin: 18; Layout.fillWidth: true; spacing: 4
                    ActionButton { iconName: "settings"; text: "偏好设置"; quiet: true; onClicked: settingsDialog.open() }
                    Item { Layout.fillWidth: true }
                    ActionButton { iconName: Theme.dark ? "sun" : "moon"; hint: Theme.dark ? "浅色外观" : "深色外观"; quiet: true; onClicked: calendarStore.darkMode = !calendarStore.darkMode }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.background
            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 30; anchors.rightMargin: 30
                anchors.topMargin: 25; anchors.bottomMargin: 0
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    spacing: 10
                    Text { text: "我的日历"; color: Theme.secondary; font.pixelSize: 12 }
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        text: desktopController.preparingDesktop ? "正在准备…" : "贴到桌面"
                        iconName: "desktop"
                        enabled: !desktopController.preparingDesktop
                        onClicked: { if (!desktopController.showDesktop()) desktopUnavailableDialog.open() }
                    }
                    Field {
                        id: searchField
                        Layout.preferredWidth: 188
                        implicitHeight: 35; leftPadding: 32; rightPadding: 31
                        font.pixelSize: 11
                        placeholderText: "搜索日程…"
                        onTextChanged: calendarStore.searchQuery = text
                        Icon { x: 10; anchors.verticalCenter: parent.verticalCenter; name: "search"; width: 15; height: 15; color: Theme.muted }
                        Text { visible: !searchField.text.length; anchors.right: parent.right; anchors.rightMargin: 9; anchors.verticalCenter: parent.verticalCenter; text: "Ctrl K"; color: Theme.muted; font.pixelSize: 9 }
                        Keys.onEscapePressed: { clear(); focus = false }
                    }
                    ActionButton { text: "新建日程"; iconName: "plus"; primary: true; implicitHeight: 35; onClicked: window.openNewEvent() }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 26
                    Layout.bottomMargin: 22
                    Text { text: calendarStore.searchQuery.length ? "搜索结果" : calendarStore.monthLabel; color: Theme.text; font.pixelSize: 32; font.weight: Font.DemiBold; font.letterSpacing: -0.7 }
                    Item { Layout.fillWidth: true }
                    ActionButton { text: "今天"; onClicked: calendarStore.goToday() }
                    RowLayout {
                        spacing: 2
                        ActionButton { iconName: "left"; hint: "上个月"; quiet: true; onClicked: calendarStore.changeMonth(-1) }
                        ActionButton { iconName: "right"; hint: "下个月"; quiet: true; onClicked: calendarStore.changeMonth(1) }
                    }
                }
                MemoPanel {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 16
                    originWindow: window
                }
                Rectangle {
                    visible: !window.showingList
                    Layout.fillWidth: true; Layout.fillHeight: true
                    color: Theme.background; radius: 12; border.color: Theme.line; clip: true
                    Row {
                        id: weekdays
                        x: 1; y: 1; width: parent.width - 2; height: 40
                        Repeater {
                            model: ["周一","周二","周三","周四","周五","周六","周日"]
                            delegate: Text {
                                required property string modelData
                                required property int index
                                width: weekdays.width/7; height: weekdays.height
                                text: modelData; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 10; color: index > 4 ? Theme.muted : Theme.secondary
                            }
                        }
                    }
                    Rectangle { x: 1; y: 41; width: parent.width - 2; height: 1; color: Theme.line }
                    MonthGrid {
                        id: monthGrid
                        x: 1; y: 42; width: parent.width - 2; height: parent.height - 43
                        onSelected: function(date) { calendarStore.selectDate(date) }
                        onCreateRequested: function(date) { calendarStore.selectDate(date); editor.create(date) }
                        onEventRequested: function(event) { editor.edit(event) }
                    }
                }
                Rectangle {
                    visible: window.showingList
                    Layout.fillWidth: true; Layout.fillHeight: true
                    radius: 12; color: Theme.background; border.color: Theme.line
                    ListView {
                        id: agendaList
                        anchors.fill: parent; anchors.margins: 16
                        clip: true
                        model: calendarStore.visibleEvents
                        spacing: 0
                        ScrollBar.vertical: SlimScrollBar { }
                        delegate: Item {
                            required property var modelData
                            width: agendaList.width; height: 82
                            Rectangle { anchors.fill: parent; anchors.margins: 3; radius: 9; color: agendaMouse.containsMouse ? Theme.hover : "transparent" }
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 12; spacing: 16
                                Text { text: modelData.dateLabel; Layout.preferredWidth: 118; color: Theme.secondary; font.pixelSize: 11; elide: Text.ElideRight }
                                Rectangle { width: 3; height: 36; radius: 2; color: modelData.color }
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 7
                                    Text { text: modelData.title; color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                                    Text { text: modelData.timeRange + "  ·  " + modelData.calendarName; color: Theme.muted; font.pixelSize: 10 }
                                }
                                Icon { name: "right"; width: 15; height: 15; color: Theme.muted }
                            }
                            MouseArea { id: agendaMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { calendarStore.selectDate(modelData.date); editor.edit(modelData) } }
                        }
                        Column {
                            anchors.centerIn: parent; spacing: 12; visible: agendaList.count === 0
                            Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "search"; width: 28; height: 28; color: Theme.muted }
                            Text { text: calendarStore.searchQuery.length ? "未找到日程" : "本月没有日程"; color: Theme.secondary; font.pixelSize: 13 }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; Layout.preferredHeight: 39
                    spacing: 6
                    Text { text: "双击日期新建日程"; font.pixelSize: 9; color: Theme.muted }
                    Item { Layout.fillWidth: true }
                    Text { Layout.leftMargin: 13; text: "本月 " + calendarStore.monthCount + " 项"; font.pixelSize: 9; color: Theme.secondary }
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 290
            Layout.fillHeight: true
            color: Theme.panel
            Rectangle { width: 1; height: parent.height; color: Theme.line }
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 24
                spacing: 0
                RowLayout {
                    Layout.topMargin: 12; Layout.fillWidth: true; spacing: 15
                    Text { text: calendarStore.selectedDay; font.pixelSize: 60; font.weight: Font.Light; font.letterSpacing: -3; color: Theme.text }
                    ColumnLayout {
                        spacing: 7
                        Text { text: calendarStore.selectedWeekday; font.pixelSize: 14; color: Theme.text }
                        Text { text: calendarStore.selectedHeading; font.pixelSize: 10; color: Theme.muted }
                    }
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        visible: calendarStore.selectedIsToday
                        width: 34; height: 22; radius: 6; color: Theme.accentSoft
                        Text { anchors.centerIn: parent; text: "今天"; font.pixelSize: 9; color: Theme.accent }
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.topMargin: 17; height: 1; color: Theme.line }
                RowLayout {
                    Layout.fillWidth: true; Layout.topMargin: 17; Layout.bottomMargin: 12
                    Text { text: "当天安排"; color: Theme.text; font.pixelSize: 12; font.weight: Font.Medium }
                    Rectangle { width: 20; height: 20; radius: 6; color: Theme.accentSoft; Text { anchors.centerIn: parent; text: calendarStore.selectedEvents.length; color: Theme.accent; font.pixelSize: 10 } }
                    Item { Layout.fillWidth: true }
                    ActionButton { iconName: "plus"; hint: "添加当天日程"; quiet: true; implicitHeight: 30; onClicked: window.openNewEvent() }
                }
                ScrollView {
                    id: dayScroll
                    Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 120
                    clip: true
                    contentWidth: availableWidth
                    ScrollBar.vertical: SlimScrollBar {
                        parent: dayScroll
                        x: dayScroll.width - width
                        y: dayScroll.topPadding
                        height: dayScroll.availableHeight
                    }
                    Column {
                        width: dayScroll.availableWidth
                        spacing: 11
                        Repeater {
                            model: calendarStore.selectedEvents
                            delegate: Rectangle {
                                id: dayCard
                                required property var modelData
                                width: parent.width; height: 120; radius: 11
                                color: dayCardMouse.containsMouse ? Theme.hover : Theme.surface
                                border.color: dayCardMouse.containsMouse ? Theme.lineStrong : Theme.line
                                Column {
                                    anchors.fill: parent; anchors.margins: 15; spacing: 10
                                    Row {
                                        width: parent.width
                                        spacing: 7
                                        Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 6; height: 6; radius: 3; color: dayCard.modelData.color }
                                        Text { width: parent.width - 13; text: dayCard.modelData.timeRange; color: Theme.secondary; font.pixelSize: 10; elide: Text.ElideRight }
                                    }
                                    Text { width: parent.width; text: dayCard.modelData.title; color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium; elide: Text.ElideRight }
                                    Text { width: parent.width; text: dayCard.modelData.notes.length ? dayCard.modelData.notes : dayCard.modelData.calendarName + "日历"; color: Theme.muted; font.pixelSize: 10; elide: Text.ElideRight }
                                }
                                MouseArea { id: dayCardMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: editor.edit(dayCard.modelData) }
                            }
                        }
                        Column {
                            visible: calendarStore.selectedEvents.length === 0
                            width: parent.width; topPadding: 32; spacing: 14
                            Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "note"; width: 32; height: 32; color: Theme.muted }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "当天没有日程"; color: Theme.secondary; font.pixelSize: 12 }
                            ActionButton { anchors.horizontalCenter: parent.horizontalCenter; text: "新建日程"; iconName: "plus"; quiet: true; onClicked: window.openNewEvent() }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.topMargin: 18; height: 1; color: Theme.line }
                Text { Layout.topMargin: 19; Layout.bottomMargin: 9; text: "接下来"; color: Theme.secondary; font.pixelSize: 11; font.weight: Font.Medium }
                Repeater {
                    model: calendarStore.upcomingEvents
                    delegate: Item {
                        id: nextEvent
                        required property var modelData
                        Layout.fillWidth: true; implicitHeight: 61
                        Rectangle { anchors.fill: parent; anchors.margins: -5; radius: 8; color: nextEventMouse.containsMouse ? Theme.hover : "transparent" }
                        RowLayout {
                            anchors.fill: parent; spacing: 11
                            Rectangle { width: 3; height: 28; radius: 2; color: nextEvent.modelData.color; opacity: 0.7 }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 5
                                Text { Layout.fillWidth: true; text: nextEvent.modelData.title; color: Theme.text; font.pixelSize: 11; elide: Text.ElideRight }
                                Text { text: nextEvent.modelData.dateLabel + " · " + nextEvent.modelData.timeLabel; color: Theme.muted; font.pixelSize: 9 }
                            }
                        }
                        MouseArea { id: nextEventMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { calendarStore.selectDate(nextEvent.modelData.date); editor.edit(nextEvent.modelData) } }
                    }
                }
                Text { visible: calendarStore.upcomingEvents.length === 0; Layout.topMargin: 12; Layout.bottomMargin: 14; text: "暂无后续日程"; color: Theme.muted; font.pixelSize: 11 }
            }
        }
    }

    EventEditor { id: editor; originWindow: window }

    ICloudSettings { id: cloudDialog; originWindow: window }

    Dialog {
        id: calendarDialog
        parent: Overlay.overlay; anchors.centerIn: parent
        width: 420; padding: 26; modal: true
        property string calendarId: ""
        function create() {
            calendarStore.clearError()
            calendarId = ""
            calendarNameField.text = ""
            calendarColorField.text = "#218572"
            open()
            calendarNameField.forceActiveFocus()
        }
        function edit(calendar) {
            calendarStore.clearError()
            calendarId = calendar.id
            calendarNameField.text = calendar.name
            calendarColorField.text = calendar.color
            open()
            calendarNameField.forceActiveFocus()
        }
        background: Rectangle { radius: 16; color: Theme.surface; border.color: Theme.lineStrong }
        Overlay.modal: Rectangle { color: Theme.overlay }
        contentItem: ColumnLayout {
            spacing: 18
            RowLayout {
                Layout.fillWidth: true
                Rectangle { width: 42; height: 42; radius: 12; color: Theme.accentSoft; Icon { anchors.centerIn: parent; name: "calendar"; width: 23; height: 23; color: Theme.accent } }
                Text { text: calendarDialog.calendarId.length ? "编辑分类" : "新增本机分类"; color: Theme.text; font.pixelSize: 21; font.weight: Font.DemiBold }
                Item { Layout.fillWidth: true }
                ActionButton { iconName: "close"; hint: "关闭"; quiet: true; onClicked: calendarDialog.close() }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 8
                Text { text: "名称"; color: Theme.secondary; font.pixelSize: 12 }
                Field { id: calendarNameField; Layout.fillWidth: true; maximumLength: 30; placeholderText: "例如：学习、家人、旅行" }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 12
                Text { text: "颜色"; color: Theme.secondary; font.pixelSize: 12 }
                Row {
                    spacing: 12
                    Repeater {
                        model: ["#218572", "#C28B45", "#8273B3", "#4A87B6", "#C16981", "#4C9A70", "#BD7350", "#637B91"]
                        delegate: AbstractButton {
                            required property string modelData
                            width: 30; height: 30
                            Accessible.name: "选择颜色 " + modelData
                            onClicked: calendarColorField.text = modelData
                            background: Rectangle { radius: 10; color: modelData; border.width: parent.visualFocus ? 2 : 0; border.color: Theme.text }
                            contentItem: Icon { visible: calendarColorField.text.toLowerCase() === modelData.toLowerCase(); name: "check"; color: "white"; width: 17; height: 17 }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 12
                    Rectangle { width: 20; height: 20; radius: 6; color: /^#[0-9a-fA-F]{6}$/.test(calendarColorField.text) ? calendarColorField.text : "transparent"; border.color: Theme.lineStrong }
                    Field { id: calendarColorField; Layout.fillWidth: true; maximumLength: 7; placeholderText: "#RRGGBB" }
                }
            }
            InlineNotice { Layout.fillWidth: true; visible: calendarStore.error.length > 0; text: calendarStore.error; error: true }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { text: "取消"; quiet: true; onClicked: calendarDialog.close() }
                ActionButton {
                    text: "保存分类"; primary: true; enabled: calendarNameField.text.trim().length > 0
                    onClicked: if (calendarStore.saveCalendar(calendarDialog.calendarId, calendarNameField.text, calendarColorField.text)) calendarDialog.close()
                }
            }
        }
    }

    Dialog {
        id: deleteCalendarDialog
        parent: Overlay.overlay; anchors.centerIn: parent
        width: 430; padding: 26; modal: true
        property string calendarId: ""
        property string calendarName: ""
        readonly property int eventCount: {
            const calendar = calendarStore.localCalendars.find(c => c.id === calendarId)
            return calendar ? calendar.count : 0
        }
        property var targets: []
        function request(calendar) {
            calendarStore.clearError()
            calendarId = calendar.id
            calendarName = calendar.name
            targets = calendarStore.localCalendars.filter(c => c.id !== calendarId)
            transferCalendarField.currentIndex = -1
            open()
        }
        background: Rectangle { radius: 16; color: Theme.surface; border.color: Theme.lineStrong }
        Overlay.modal: Rectangle { color: Theme.overlay }
        contentItem: ColumnLayout {
            spacing: 18
            Rectangle { width: 42; height: 42; radius: 12; color: Theme.dangerSoft; Icon { anchors.centerIn: parent; name: "trash"; width: 22; height: 22; color: Theme.danger } }
            Text { Layout.fillWidth: true; text: "删除“" + deleteCalendarDialog.calendarName + "”？"; color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold; wrapMode: Text.Wrap }
            Text {
                Layout.fillWidth: true
                text: deleteCalendarDialog.eventCount > 0
                    ? "此分类有 " + deleteCalendarDialog.eventCount + " 项日程，请选择要移入的本机分类。"
                    : "此分类没有日程。"
                color: Theme.secondary; font.pixelSize: 12; wrapMode: Text.Wrap; lineHeight: 1.5
            }
            ColumnLayout {
                visible: deleteCalendarDialog.eventCount > 0
                Layout.fillWidth: true; spacing: 8
                Text { text: "将日程移到"; color: Theme.secondary; font.pixelSize: 12 }
                SelectField {
                    id: transferCalendarField
                    Layout.fillWidth: true
                    model: deleteCalendarDialog.targets; textRole: "name"
                    displayText: currentIndex < 0 ? "选择一个本机分类" : currentText
                }
            }
            InlineNotice { Layout.fillWidth: true; visible: calendarStore.error.length > 0; text: calendarStore.error; error: true }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { text: "取消"; quiet: true; onClicked: deleteCalendarDialog.close() }
                ActionButton {
                    text: deleteCalendarDialog.eventCount > 0 ? "移动日程并删除" : "删除分类"
                    danger: true; primary: true
                    enabled: calendarStore.localCalendars.length > 1 && (deleteCalendarDialog.eventCount === 0 || transferCalendarField.currentIndex >= 0)
                    onClicked: {
                        const target = deleteCalendarDialog.eventCount > 0 ? deleteCalendarDialog.targets[transferCalendarField.currentIndex].id : ""
                        if (calendarStore.deleteCalendar(deleteCalendarDialog.calendarId, target)) deleteCalendarDialog.close()
                    }
                }
            }
        }
    }

    Dialog {
        id: desktopUnavailableDialog
        parent: Overlay.overlay; anchors.centerIn: parent
        width: 420; padding: 24; modal: true
        background: Rectangle { color: Theme.surface; radius: 16; border.color: Theme.lineStrong }
        Overlay.modal: Rectangle { color: Theme.overlay }
        contentItem: ColumnLayout {
            spacing: 18
            Text { text: "暂时无法贴到桌面"; color: Theme.text; font.pixelSize: 19; font.weight: Font.DemiBold }
            Text {
                Layout.fillWidth: true
                text: desktopController.needsGnomeBridge
                    ? "请重新登录桌面后，再点击“贴到桌面”。"
                    : "当前桌面暂不支持此功能，请在日历窗口中继续使用日笺。"
                color: Theme.secondary; font.pixelSize: 12; wrapMode: Text.Wrap; lineHeight: 1.5
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                ActionButton { text: "知道了"; primary: true; onClicked: desktopUnavailableDialog.close() }
            }
        }
    }

    Dialog {
        id: settingsDialog
        parent: Overlay.overlay; anchors.centerIn: parent
        width: 470; padding: 28; modal: true
        height: Math.min(implicitHeight, parent.height - 48)
        background: Rectangle { color: Theme.surface; radius: 18; border.color: Theme.lineStrong }
        Overlay.modal: Rectangle { color: Theme.overlay }
        SurfaceMenu {
            id: calendarMenu
            property var calendar: ({})
            MenuEntry { text: "编辑分类"; iconName: "note"; onTriggered: calendarDialog.edit(calendarMenu.calendar) }
            MenuRule { }
            MenuEntry {
                text: calendarStore.localCalendars.length > 1 ? "删除分类…" : "至少保留一个本机分类"
                iconName: "trash"; danger: true; enabled: calendarStore.localCalendars.length > 1
                onTriggered: deleteCalendarDialog.request(calendarMenu.calendar)
            }
        }
        contentItem: ScrollView {
            id: settingsScroll
            implicitHeight: settingsContent.implicitHeight
            contentWidth: availableWidth
            contentHeight: Math.ceil(settingsContent.implicitHeight)
            rightPadding: settingsBar.visible ? settingsBar.width + 8 : 0
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical: SlimScrollBar {
                id: settingsBar
                parent: settingsScroll; x: settingsScroll.width - width; height: settingsScroll.availableHeight
                policy: ScrollBar.AsNeeded
                visible: settingsScroll.contentHeight > settingsScroll.availableHeight + 1
            }
            ColumnLayout {
                id: settingsContent
                width: settingsScroll.availableWidth
                spacing: 20
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Rectangle { width: 42; height: 42; radius: 12; color: Theme.accentSoft; Icon { anchors.centerIn: parent; name: "settings"; width: 22; height: 22; color: Theme.accent } }
                    Text { text: "偏好设置"; color: Theme.text; font.pixelSize: 21; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    ActionButton { iconName: "close"; hint: "关闭"; quiet: true; onClicked: settingsDialog.close() }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 60
                    radius: 12; color: Theme.input; border.color: Theme.line
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 16
                        Text { text: "深色外观"; color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                        Item { Layout.fillWidth: true }
                        Toggle { checked: calendarStore.darkMode; onToggled: calendarStore.darkMode = checked; Accessible.name: "深色外观" }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 76
                    radius: 12; color: Theme.input; border.color: Theme.line
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 16
                        spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            Text { text: "显示备忘录"; color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                            Text { Layout.fillWidth: true; text: "在月历上方显示，关闭后保留内容"; color: Theme.secondary; font.pixelSize: 11; wrapMode: Text.Wrap }
                        }
                        Toggle { checked: calendarStore.memoEnabled; onToggled: calendarStore.memoEnabled = checked; Accessible.name: "显示备忘录" }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "本机日历"; color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                        Item { Layout.fillWidth: true }
                        ActionButton { text: "新增分类"; iconName: "plus"; quiet: true; implicitHeight: 32; onClicked: calendarDialog.create() }
                    }
                    ScrollView {
                        id: settingsCalendarScroll
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(settingsCalendarList.implicitHeight, 168)
                        contentWidth: availableWidth
                        contentHeight: Math.ceil(settingsCalendarList.implicitHeight)
                        rightPadding: settingsCalendarBar.visible ? settingsCalendarBar.width + 4 : 0
                        clip: true
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        ScrollBar.vertical: SlimScrollBar {
                            id: settingsCalendarBar
                            parent: settingsCalendarScroll; x: settingsCalendarScroll.width - width; height: settingsCalendarScroll.availableHeight
                            policy: ScrollBar.AsNeeded
                            visible: settingsCalendarScroll.contentHeight > settingsCalendarScroll.availableHeight + 1
                        }
                        ColumnLayout {
                            id: settingsCalendarList
                            width: settingsCalendarScroll.availableWidth
                            spacing: 4
                            Repeater {
                                model: calendarStore.localCalendars
                                delegate: Rectangle {
                                    id: settingsCalendarRow
                                    required property var modelData
                                    Layout.fillWidth: true; implicitHeight: 42
                                    radius: 9; color: Theme.input; border.color: Theme.line
                                    RowLayout {
                                        anchors.fill: parent; anchors.margins: 8; spacing: 10
                                        Rectangle { width: 14; height: 14; radius: 4; color: settingsCalendarRow.modelData.color }
                                        Text { Layout.fillWidth: true; text: settingsCalendarRow.modelData.name; elide: Text.ElideRight; font.pixelSize: 12; color: Theme.text }
                                        Text { text: settingsCalendarRow.modelData.count; font.pixelSize: 10; color: Theme.muted }
                                        ActionButton {
                                            id: settingsCalendarMore
                                            implicitWidth: 28; implicitHeight: 28; quiet: true
                                            text: "⋯"; hint: "管理本机分类"
                                            Accessible.name: "管理“" + settingsCalendarRow.modelData.name + "”分类"
                                            onClicked: {
                                                calendarMenu.calendar = settingsCalendarRow.modelData
                                                calendarMenu.popup(settingsCalendarMore, width - calendarMenu.width, height + 4)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                ActionButton { text: "iCloud 账户与同步"; iconName: "cloud"; onClicked: { settingsDialog.close(); cloudDialog.open() } }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "日笺 · Dayline"; color: Theme.secondary; font.pixelSize: 11 }
                    Item { Layout.fillWidth: true }
                    Text { text: Qt.application.version; color: Theme.muted; font.pixelSize: 10 }
                }
            }
        }
    }
}
