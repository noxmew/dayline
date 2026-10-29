import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Dayline 1.0

ApplicationWindow {
    id: editor
    required property Window originWindow
    transientParent: null
    title: "备忘录 · 日笺"
    width: 520
    height: Math.min(400, Math.max(300, Screen.desktopAvailableHeight - 80))
    minimumWidth: 420
    minimumHeight: 300
    visible: false
    color: Theme.surface
    background: Rectangle { color: Theme.surface }

    function open() {
        if (!visible) WindowPlacement.center(editor, originWindow)
        show(); raise(); requestActivate()
        memoField.forceActiveFocus()
    }
    function finish() {
        Qt.inputMethod.commit()
        if (calendarStore.saveMemo()) close()
    }
    onClosing: function(close) {
        Qt.inputMethod.commit()
        close.accepted = calendarStore.saveMemo()
    }
    Shortcut { sequence: "Escape"; onActivated: editor.finish() }
    Shortcut { sequence: "Ctrl+S"; onActivated: { Qt.inputMethod.commit(); calendarStore.saveMemo() } }
    Connections {
        target: calendarStore
        function onMemoEnabledChanged() {
            if (!calendarStore.memoEnabled && editor.visible) editor.finish()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            spacing: 9
            Icon { name: "note"; width: 20; height: 20; color: Theme.accent }
            Text { text: "备忘录"; color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            Text { text: "自动保存"; color: Theme.muted; font.pixelSize: 11 }
        }
        ScrollView {
            id: memoScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            rightPadding: memoBar.visible ? memoBar.width + 6 : 0
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical: SlimScrollBar {
                id: memoBar
                parent: memoScroll
                policy: ScrollBar.AsNeeded
                visible: memoField.contentHeight + memoField.topPadding + memoField.bottomPadding > memoScroll.availableHeight + 1
                x: memoScroll.width - width
                y: memoScroll.topPadding
                height: memoScroll.availableHeight
            }
            TextArea {
                id: memoField
                width: memoScroll.availableWidth
                height: Math.max(memoScroll.availableHeight, implicitHeight)
                text: calendarStore.memo
                onTextChanged: if (text !== calendarStore.memo) calendarStore.memo = text
                textFormat: TextEdit.PlainText
                placeholderText: "记下每周惯例、长期事项…"
                wrapMode: TextEdit.Wrap
                padding: 12
                font.pixelSize: 13
                color: Theme.text
                placeholderTextColor: Theme.muted
                selectionColor: Theme.accentSoft
                selectedTextColor: Theme.text
                selectByMouse: true
                Accessible.name: "备忘录内容"
                background: Rectangle { radius: 9; color: Theme.input; border.color: memoField.activeFocus ? Theme.accent : Theme.line }
            }
        }
        InlineNotice {
            Layout.fillWidth: true
            visible: calendarStore.memoError.length > 0
            error: true
            text: calendarStore.memoError
        }
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: calendarStore.memoError.length ? "内容仍在编辑框中，请重试保存"
                    : calendarStore.memoSavePending ? "正在保存…" : "已保存在本机 · Enter 换行"
                color: calendarStore.memoError.length ? Theme.danger : Theme.secondary
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
            ActionButton { visible: calendarStore.memoError.length > 0; text: "重试保存"; onClicked: calendarStore.saveMemo() }
            ActionButton { text: "完成"; primary: true; onClicked: editor.finish() }
        }
    }
}
