import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Dayline 1.0

SurfaceMenu {
    id: appearance
    Item {
        implicitWidth: 270
        implicitHeight: 56
        Column {
            x: 12; y: 9
            spacing: 5
            Text { text: "桌面外观"; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
            Text { text: "让日历融入你的桌面"; color: Theme.muted; font.pixelSize: 10 }
        }
    }
    MenuToggle {
        text: "锁定位置"
        iconName: desktopController.locked ? "lock" : "unlock"
        checked: desktopController.locked
        onToggled: desktopController.locked = checked
    }
    MenuToggle {
        text: "深色外观"
        iconName: "moon"
        checked: calendarStore.darkMode
        onToggled: calendarStore.darkMode = checked
    }
    MenuRule { }
    Item {
        implicitWidth: 270
        implicitHeight: 86
        RowLayout {
            x: 12; y: 8
            width: parent.width - 24
            Text { text: "背景透明度"; font.pixelSize: 11; color: Theme.secondary }
            Item { Layout.fillWidth: true }
            Text { text: Math.round(desktopController.panelOpacity * 100) + "%"; font.pixelSize: 11; font.weight: Font.Medium; color: Theme.accent }
        }
        ValueSlider {
            x: 4; y: 27
            width: parent.width - 8
            from: 0.25; to: 1
            value: desktopController.panelOpacity
            onMoved: desktopController.panelOpacity = value
            Accessible.name: "桌面日历背景透明度"
        }
        RowLayout {
            x: 12; y: 61
            width: parent.width - 24
            Text { text: "通透"; font.pixelSize: 9; color: Theme.muted }
            Item { Layout.fillWidth: true }
            Text { text: "清晰"; font.pixelSize: 9; color: Theme.muted }
        }
    }
    MenuRule { }
    MenuEntry { text: "重置大小与位置"; iconName: "reset"; onTriggered: desktopController.resetPosition() }
    MenuEntry { text: "返回窗口模式"; iconName: "desktop"; onTriggered: desktopController.enabled = false }
    MenuRule { }
    MenuEntry { text: "退出日笺"; iconName: "power"; danger: true; onTriggered: Qt.quit() }
}
