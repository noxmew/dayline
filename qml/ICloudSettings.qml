import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Dayline 1.0

ApplicationWindow {
    id: window
    objectName: "icloudSettings"
    required property Window originWindow
    title: "iCloud · 日笺"
    width: 600
    height: Math.min(760, Math.max(420, Screen.desktopAvailableHeight - 80))
    minimumWidth: 520
    minimumHeight: Math.min(560, Math.max(420, Screen.desktopAvailableHeight - 80))
    property bool passwordVisible: false
    property bool browserOpenFailed: false
    visible: false
    color: Theme.surface
    transientParent: null
    function open() {
        accountField.text = iCloudSync.account
        regionField.currentIndex = iCloudSync.region === "china" ? 1 : 0
        height = Math.min(iCloudSync.connected ? 760 : 560,
                          Math.max(minimumHeight, Screen.desktopAvailableHeight - 80))
        WindowPlacement.center(window, originWindow)
        show(); raise(); requestActivate()
    }
    Shortcut { sequence: "Escape"; onActivated: window.close() }
    ScrollView {
        id: accountScroll
        anchors.fill: parent
        anchors.margins: 28
        clip: true
        contentWidth: availableWidth
        contentHeight: Math.ceil(accountContent.implicitHeight)
        rightPadding: accountBar.visible ? accountBar.width + 8 : 0
        bottomPadding: 8
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: SlimScrollBar {
            id: accountBar
            parent: accountScroll
            policy: ScrollBar.AsNeeded
            visible: accountScroll.contentHeight > accountScroll.availableHeight + 1
            x: accountScroll.width - width
            y: accountScroll.topPadding
            height: accountScroll.availableHeight
        }
        ColumnLayout {
            id: accountContent
            width: accountScroll.availableWidth
            spacing: 20
            RowLayout {
                Layout.fillWidth: true
                Rectangle {
                    width: 46; height: 46; radius: 14; color: Theme.accentSoft
                    Icon { anchors.centerIn: parent; name: "cloud"; width: 25; height: 25; color: Theme.accent }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Text { text: "iCloud"; color: Theme.text; font.pixelSize: 22; font.weight: Font.DemiBold }
                    Text { Layout.fillWidth: true; text: iCloudSync.status; color: Theme.secondary; font.pixelSize: 11; wrapMode: Text.Wrap }
                }
            }
            Text {
                Layout.fillWidth: true
                text: "为 Apple 账户开启双重认证后，生成 <a style='color:" + Theme.accent + "; text-decoration:none' href='https://account.apple.com/account/manage'>App 专用密码 ↗</a>。请勿使用账户登录密码。"
                textFormat: Text.RichText; wrapMode: Text.Wrap; color: Theme.secondary; linkColor: Theme.accent; font.pixelSize: 12; lineHeight: 1.5
                onLinkActivated: link => window.browserOpenFailed = !Qt.openUrlExternally(link)
                HoverHandler { cursorShape: parent.hoveredLink.length ? Qt.PointingHandCursor : Qt.ArrowCursor }
            }
            InlineNotice { Layout.fillWidth: true; visible: window.browserOpenFailed; error: true; text: "无法启动系统默认浏览器，请检查默认浏览器设置后重试。" }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: accountForm.implicitHeight + 32
                radius: 12; color: Theme.input; border.color: Theme.line
            ColumnLayout {
                id: accountForm
                anchors.fill: parent; anchors.margins: 16; spacing: 10
                Text { text: "Apple 账户"; color: Theme.secondary; font.pixelSize: 12 }
                Field { id: accountField; Layout.fillWidth: true; placeholderText: "name@example.com"; enabled: !iCloudSync.busy && !iCloudSync.connected; inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoPredictiveText }
                Text { text: "账户地区"; color: Theme.secondary; font.pixelSize: 12 }
                SelectField { id: regionField; Layout.fillWidth: true; model: ["其他国家或地区", "中国大陆（云上贵州）"]; enabled: !iCloudSync.busy && !iCloudSync.connected }
                Text { text: "App 专用密码"; color: Theme.secondary; font.pixelSize: 12 }
                Field {
                    id: passwordField
                    Layout.fillWidth: true
                    placeholderText: "xxxx-xxxx-xxxx-xxxx"
                    echoMode: window.passwordVisible ? TextInput.Normal : TextInput.Password
                    enabled: !iCloudSync.busy
                    rightPadding: 61
                    inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                    ActionButton {
                        anchors.right: parent.right; anchors.rightMargin: 5; anchors.verticalCenter: parent.verticalCenter
                        text: window.passwordVisible ? "隐藏" : "显示"
                        implicitHeight: 30; implicitWidth: 49; quiet: true
                        onClicked: window.passwordVisible = !window.passwordVisible
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        text: iCloudSync.busy ? "正在连接…" : iCloudSync.connected ? "重新授权" : "连接 iCloud"
                        primary: true; enabled: !iCloudSync.busy && passwordField.text.length > 0
                        onClicked: {
                            iCloudSync.connectAccount(accountField.text, passwordField.text, regionField.currentIndex === 1 ? "china" : "global")
                            passwordField.clear()
                        }
                    }
                }
            }
            }
            InlineNotice { Layout.fillWidth: true; visible: iCloudSync.error.length > 0; error: true; text: iCloudSync.error }
            Rectangle { visible: iCloudSync.connected; Layout.fillWidth: true; height: 1; color: Theme.line }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 12; visible: iCloudSync.connected
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "同步的日历"; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    ActionButton { text: "立即同步"; iconName: "refresh"; enabled: !iCloudSync.busy; onClicked: iCloudSync.sync() }
                }
                Text { text: "每分钟自动同步，离线修改会在联网后上传。"; color: Theme.muted; font.pixelSize: 11 }
                Repeater {
                    model: iCloudSync.calendars
                    delegate: Rectangle {
                        id: calendarRow
                        required property var modelData
                        Layout.fillWidth: true; implicitHeight: Math.max(68, calendarContent.implicitHeight + 24); radius: 12; color: Theme.input; border.color: Theme.line
                        RowLayout {
                            id: calendarContent
                            anchors.fill: parent; anchors.margins: 12; spacing: 11
                            Rectangle { width: 10; height: 10; radius: 5; color: calendarRow.modelData.color }
                            ColumnLayout {
                                spacing: 4; Layout.fillWidth: true
                                Text { text: calendarRow.modelData.name; color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight; Layout.fillWidth: true }
                                Text {
                                    Layout.fillWidth: true
                                    text: calendarRow.modelData.available
                                        ? (calendarRow.modelData.writable ? "可读写" : "只读")
                                        : "iCloud 列表中已移除 · " + calendarRow.modelData.protectedCount + " 项待处理"
                                    color: Theme.muted; font.pixelSize: 10; wrapMode: Text.Wrap
                                }
                            }
                            ActionButton {
                                visible: calendarRow.modelData.selected && calendarRow.modelData.writable && calendarRow.modelData.available
                                text: iCloudSync.defaultCalendar === calendarRow.modelData.id ? "默认" : "设为默认"
                                quiet: true; enabled: !iCloudSync.busy
                                onClicked: iCloudSync.defaultCalendar = calendarRow.modelData.id
                            }
                            Toggle { visible: calendarRow.modelData.available; checked: calendarRow.modelData.selected; enabled: !iCloudSync.busy; onToggled: iCloudSync.selectCalendar(calendarRow.modelData.id, checked); Accessible.name: "同步" + calendarRow.modelData.name }
                            ActionButton {
                                visible: !calendarRow.modelData.available
                                text: "移除本机副本…"; quiet: true; danger: true; enabled: !iCloudSync.busy
                                onClicked: removeCalendarDialog.request(calendarRow.modelData)
                            }
                        }
                    }
                }
                Text { visible: iCloudSync.calendars.length === 0; text: "未找到可用日历。"; color: Theme.muted; font.pixelSize: 12 }
                Text { text: iCloudSync.lastSync.length ? "上次同步：" + new Date(iCloudSync.lastSync).toLocaleString(Qt.locale("zh_CN"), "M月d日 HH:mm:ss") : "尚未完成同步"; color: Theme.muted; font.pixelSize: 11 }
                Text { text: "待同步：" + iCloudSync.pendingCount + " 项"; color: Theme.secondary; font.pixelSize: 12 }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 12; visible: iCloudSync.conflicts.length > 0
                Text { text: "同步问题"; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                Repeater {
                    model: iCloudSync.conflicts
                    delegate: Rectangle {
                        id: conflictCard
                        required property var modelData
                        Layout.fillWidth: true; implicitHeight: conflictContent.implicitHeight + 28
                        radius: 12; color: Theme.input; border.color: Theme.line
                        ColumnLayout {
                            id: conflictContent
                            anchors.fill: parent; anchors.margins: 14; spacing: 10
                            Text { text: conflictCard.modelData.calendarName; color: Theme.muted; font.pixelSize: 11 }
                            Text { text: "本机：" + (conflictCard.modelData.deleting ? "待删除 · " : "") + conflictCard.modelData.localTitle; color: Theme.text; font.pixelSize: 13; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            Text { text: conflictCard.modelData.localDetails; color: Theme.secondary; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true; lineHeight: 1.4 }
                            Text { text: "云端：" + conflictCard.modelData.remoteTitle; color: Theme.secondary; font.pixelSize: 13; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            Text { text: conflictCard.modelData.remoteDetails; color: Theme.secondary; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true; lineHeight: 1.4 }
                            InlineNotice { visible: conflictCard.modelData.error.length > 0; text: conflictCard.modelData.error; error: true; Layout.fillWidth: true }
                            RowLayout {
                                visible: conflictCard.modelData.isConflict
                                Layout.fillWidth: true; spacing: 6
                                ActionButton { text: "采用云端"; enabled: !iCloudSync.busy; onClicked: iCloudSync.resolveConflict(conflictCard.modelData.id, "remote") }
                                ActionButton { text: "保留本机"; enabled: !iCloudSync.busy && !conflictCard.modelData.deleted; onClicked: iCloudSync.resolveConflict(conflictCard.modelData.id, "local") }
                                ActionButton { text: "另存为新日程"; enabled: !iCloudSync.busy; onClicked: iCloudSync.resolveConflict(conflictCard.modelData.id, "copy") }
                            }
                        }
                    }
                }
            }
            ActionButton { visible: iCloudSync.connected; text: "断开 iCloud 账户…"; quiet: true; danger: true; enabled: !iCloudSync.busy; onClicked: disconnectDialog.open() }
            Item { implicitHeight: 10 }
        }
    }
    Dialog {
        id: removeCalendarDialog
        property string calendarId: ""
        property string calendarName: ""
        property int protectedCount: 0
        function request(calendar) {
            calendarId = calendar.id
            calendarName = calendar.name
            protectedCount = calendar.protectedCount
            iCloudSync.clearError()
            open()
        }
        parent: Overlay.overlay; anchors.centerIn: parent; width: 430; padding: 24; modal: true
        background: Rectangle { radius: 16; color: Theme.surface; border.color: Theme.lineStrong }
        Overlay.modal: Rectangle { color: Theme.overlay }
        contentItem: ColumnLayout {
            spacing: 18
            Text { text: "移除本机副本？"; color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            Text {
                Layout.fillWidth: true
                text: "“" + removeCalendarDialog.calendarName + "”已不在 iCloud 日历列表中。本机还有 "
                    + removeCalendarDialog.protectedCount + " 项内容未同步或需要处理。移除会删除此日历的全部本机副本，未上传的修改将无法恢复。此操作不会修改 iCloud。"
                color: Theme.secondary; font.pixelSize: 12; wrapMode: Text.Wrap; lineHeight: 1.5
            }
            InlineNotice { Layout.fillWidth: true; visible: iCloudSync.error.length > 0; error: true; text: iCloudSync.error }
            RowLayout {
                Item { Layout.fillWidth: true }
                ActionButton { text: "暂时保留"; quiet: true; onClicked: removeCalendarDialog.close() }
                ActionButton {
                    text: "移除本机副本"; danger: true; primary: true; enabled: !iCloudSync.busy
                    onClicked: {
                        if (iCloudSync.removeUnavailableCalendar(removeCalendarDialog.calendarId)) removeCalendarDialog.close()
                    }
                }
            }
        }
    }
    Dialog {
        id: disconnectDialog
        parent: Overlay.overlay; anchors.centerIn: parent; width: 430; padding: 24; modal: true
        background: Rectangle { radius: 16; color: Theme.surface; border.color: Theme.lineStrong }
        Overlay.modal: Rectangle { color: Theme.overlay }
        contentItem: ColumnLayout {
            spacing: 18
            Rectangle { width: 40; height: 40; radius: 12; color: Theme.dangerSoft; Icon { anchors.centerIn: parent; name: "cloud"; width: 21; height: 21; color: Theme.danger } }
            Text { text: "断开 iCloud 账户？"; color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            Text { text: "将移除此设备上的 iCloud 日程和专用密码，云端内容与本机日历不受影响。请先完成待同步修改。"; color: Theme.secondary; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true; lineHeight: 1.5 }
            RowLayout {
                Item { Layout.fillWidth: true }
                ActionButton { text: "取消"; quiet: true; onClicked: disconnectDialog.close() }
                ActionButton { text: "断开账户"; danger: true; primary: true; onClicked: { disconnectDialog.close(); iCloudSync.disconnectAccount() } }
            }
        }
    }
}
