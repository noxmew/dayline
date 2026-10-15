import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Dayline 1.0

ApplicationWindow {
    id: desktop
    width: 1050
    height: 740
    minimumWidth: 700
    minimumHeight: 520
    visible: false
    color: "transparent"
    palette.window: Theme.surface
    palette.windowText: Theme.text
    palette.text: Theme.text
    palette.base: Theme.surface
    palette.button: Theme.surface
    palette.buttonText: Theme.text
    palette.highlight: Theme.accentSoft
    palette.highlightedText: Theme.text
    title: "日笺 · 桌面月历"
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnBottomHint | Qt.WindowDoesNotAcceptFocus
    onVisibleChanged: if (visible) calendarStore.searchQuery = ""

    function openNewEvent() { editor.create(calendarStore.selectedDate) }
    function openUpdates() { updateDialog.open() }

    component GeometryHandle: MouseArea {
        property bool resizing: false
        enabled: !desktopController.locked
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        cursorShape: resizing ? Qt.SizeFDiagCursor : Qt.SizeAllCursor
        onPressed: resizing ? desktopController.beginResize() : desktopController.beginMove()
        onPositionChanged: if (pressed) desktopController.updateInteraction()
        onReleased: desktopController.endInteraction()
        onCanceled: desktopController.endInteraction()
    }

    Rectangle {
        anchors.fill: parent
        radius: 18
        color: Theme.tint(Theme.background, desktopController.panelOpacity)
        border.color: Theme.tint(Theme.text, 0.12)

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            onClicked: contextMenu.popup()
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 0

            Item {
                Layout.fillWidth: true
                implicitHeight: 63
                GeometryHandle { anchors.fill: parent }
            RowLayout {
                anchors.fill: parent
                spacing: 10
                Item {
                    Layout.fillWidth: true
                    implicitHeight: 63
                    Column {
                        spacing: 6
                        Text { text: "日 笺  /  D A Y L I N E"; color: Theme.secondary; font.pixelSize: 9; font.weight: Font.Medium }
                        Text { text: calendarStore.monthLabel; color: Theme.text; font.pixelSize: 28; font.weight: Font.DemiBold }
                    }
                }
                ActionButton { text: "今天"; quiet: true; onClicked: calendarStore.goToday() }
                ActionButton { iconName: "left"; hint: "上个月"; quiet: true; onClicked: calendarStore.changeMonth(-1) }
                ActionButton { iconName: "right"; hint: "下个月"; quiet: true; onClicked: calendarStore.changeMonth(1) }
                Rectangle { width: 1; height: 20; color: Theme.line }
                ActionButton { iconName: "plus"; hint: "新建日程"; quiet: true; onClicked: desktop.openNewEvent() }
                ActionButton { iconName: "desktop"; hint: "打开日历窗口"; quiet: true; onClicked: desktopController.openManager() }
                ActionButton {
                    iconName: desktopController.locked ? "lock" : "unlock"
                    hint: desktopController.locked ? "位置已锁定 · 点击解锁" : "可拖动标题或底栏 · 点击锁定"
                    quiet: true
                    onClicked: desktopController.locked = !desktopController.locked
                }
                ActionButton {
                    id: appearanceButton
                    iconName: "settings"; hint: "桌面外观与设置"; quiet: true
                    onClicked: appearanceMenu.popup(appearanceButton, appearanceButton.width - appearanceMenu.width, appearanceButton.height + 8)
                }
            }
            }

            Row {
                id: weekdays
                Layout.topMargin: 11
                Layout.fillWidth: true
                height: 34
                Repeater {
                    model: ["周一", "周二", "周三", "周四", "周五", "周六", "周日"]
                    delegate: Text {
                        required property string modelData
                        required property int index
                        width: weekdays.width / 7; height: 34
                        text: modelData; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        color: index > 4 ? Theme.muted : Theme.secondary; font.pixelSize: 10
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "transparent"
                border.color: Theme.line
                clip: true
                MonthGrid {
                    id: monthGrid
                    anchors.fill: parent
                    anchors.margins: 1
                    desktopStyle: true
                    onSelected: date => calendarStore.selectDate(date)
                    onCreateRequested: date => editor.create(date)
                    onEventRequested: event => editor.edit(event)
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.topMargin: 16
                implicitHeight: 18
                GeometryHandle { anchors.fill: parent }
            RowLayout {
                anchors.fill: parent
                spacing: 7
                Icon { name: desktopController.locked ? "lock" : "desktop"; width: 12; height: 12; color: Theme.muted }
                Text { text: desktopController.locked ? "双击日期记事 · 单击日程编辑" : "拖动标题或底栏移动 · 右下角调整大小"; color: Theme.secondary; font.pixelSize: 10 }
                Item { Layout.fillWidth: true }
                Text { text: calendarStore.demo ? "预览空间 · 本机保存" : "本机保存"; color: Theme.muted; font.pixelSize: 9 }
            }
            }
        }

        GeometryHandle {
            resizing: true
            visible: !desktopController.locked
            anchors.right: parent.right; anchors.bottom: parent.bottom
            width: 22; height: 22
            Text { anchors.centerIn: parent; text: "◢"; color: Theme.muted; font.pixelSize: 13 }
        }
    }

    DesktopAppearanceMenu { id: appearanceMenu }

    SurfaceMenu {
        id: contextMenu
        MenuEntry { text: "新建日程"; iconName: "plus"; onTriggered: desktop.openNewEvent() }
        MenuEntry { text: "打开日历窗口"; iconName: "calendar"; onTriggered: desktopController.openManager() }
        MenuRule { }
        MenuEntry { text: "桌面外观"; iconName: "settings"; onTriggered: appearanceMenu.popup() }
        MenuEntry { text: "锁定位置"; iconName: desktopController.locked ? "lock" : "unlock"; checkable: true; checked: desktopController.locked; onTriggered: desktopController.locked = !desktopController.locked }
        MenuEntry { text: "返回窗口模式"; iconName: "desktop"; onTriggered: desktopController.enabled = false }
        MenuEntry { text: "检查更新"; iconName: "refresh"; onTriggered: desktop.openUpdates() }
        MenuRule { }
        MenuEntry { text: "退出日笺"; iconName: "power"; danger: true; onTriggered: Qt.quit() }
    }

    EventEditor { id: editor; originWindow: desktop }
    UpdateSettings { id: updateDialog; originWindow: desktop }
}
