import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Dayline 1.0

AbstractButton {
    id: panel
    required property Window originWindow
    property bool desktopStyle: false
    visible: calendarStore.memoEnabled
    implicitHeight: 126
    padding: 12
    hoverEnabled: true
    Accessible.name: "备忘录，点击编辑"
    onClicked: memoEditor.open()

    background: Rectangle {
        radius: 9
        color: panel.desktopStyle ? Theme.tint(Theme.text, panel.hovered ? 0.07 : 0.035)
                                 : panel.hovered ? Theme.hover : Theme.input
        border.color: panel.visualFocus ? Theme.accent : panel.desktopStyle ? Theme.tint(Theme.text, 0.10) : Theme.line
    }
    contentItem: ColumnLayout {
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 7
            Icon { name: "note"; width: 14; height: 14; color: Theme.secondary }
            Text { text: "备忘录"; color: Theme.secondary; font.pixelSize: 11; font.weight: Font.Medium }
            Item { Layout.fillWidth: true }
            Text {
                text: calendarStore.memoError.length ? "保存失败 · 点击查看"
                    : calendarStore.memoSavePending ? "正在保存…" : calendarStore.memo.length ? "已保存在本机" : "点击添加"
                color: calendarStore.memoError.length ? Theme.danger : Theme.muted
                font.pixelSize: 10
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.fillHeight: true
            text: calendarStore.memo.length ? calendarStore.memo : "记下每周惯例、长期事项…"
            textFormat: Text.PlainText
            color: calendarStore.memo.length ? Theme.text : Theme.muted
            font.pixelSize: 12
            lineHeight: 1.5
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            verticalAlignment: Text.AlignTop
        }
    }

    MemoEditor { id: memoEditor; originWindow: panel.originWindow }
}
