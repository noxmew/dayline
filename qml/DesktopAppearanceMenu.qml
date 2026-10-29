import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Dayline 1.0

SurfaceMenu {
    id: appearance
    Item {
        implicitWidth: 270
        implicitHeight: 38
        Text { x: 12; y: 9; text: "桌面外观"; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
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
    MenuToggle {
        text: "显示备忘录"
        iconName: "note"
        checked: calendarStore.memoEnabled
        onToggled: calendarStore.memoEnabled = checked
    }
    MenuRule { }
    Item {
        implicitWidth: 270
        implicitHeight: 66
        RowLayout {
            x: 12; y: 8
            width: parent.width - 24
            Text { text: "背景不透明度"; font.pixelSize: 11; color: Theme.secondary }
            Item { Layout.fillWidth: true }
            Text { text: Math.round(desktopController.panelOpacity * 100) + "%"; font.pixelSize: 11; font.weight: Font.Medium; color: Theme.accent }
        }
        ValueSlider {
            x: 4; y: 27
            width: parent.width - 8
            from: 0.25; to: 1
            value: desktopController.panelOpacity
            onMoved: desktopController.panelOpacity = value
            Accessible.name: "桌面日历背景不透明度"
        }
    }
    MenuRule { }
    MenuEntry { text: "重置大小与位置"; iconName: "reset"; onTriggered: desktopController.resetPosition() }
    MenuEntry { text: "返回窗口模式"; iconName: "desktop"; onTriggered: desktopController.enabled = false }
    MenuRule { }
    MenuEntry { text: "退出日笺"; iconName: "power"; danger: true; onTriggered: Qt.quit() }
}
