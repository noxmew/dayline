import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Dayline 1.0

ApplicationWindow {
    id: window
    objectName: "updateSettings"
    required property Window originWindow
    title: "检查更新 · 日笺"
    width: 560
    height: Math.min(600, Math.max(440, Screen.desktopAvailableHeight - 80))
    minimumWidth: 500
    minimumHeight: 440
    visible: false
    color: Theme.surface
    transientParent: null

    function open() {
        height = Math.min(updateChecker.releaseNotes.length > 0 ? 600 : 440,
                          Math.max(minimumHeight, Screen.desktopAvailableHeight - 80))
        WindowPlacement.center(window, originWindow)
        show(); raise(); requestActivate()
        if (updateChecker.configured && !updateChecker.busy && updateChecker.state !== "downloaded")
            updateChecker.check()
    }
    function sizeText(bytes) {
        return bytes < 1024 * 1024 ? (bytes / 1024).toFixed(1) + " KB" : (bytes / 1024 / 1024).toFixed(1) + " MB"
    }
    Shortcut { sequence: "Escape"; onActivated: window.close() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 20
        RowLayout {
            spacing: 14
            Rectangle {
                width: 48; height: 48; radius: 15; color: Theme.accentSoft
                Icon { anchors.centerIn: parent; width: 25; height: 25; name: "refresh"; color: Theme.accent }
            }
            ColumnLayout {
                spacing: 5
                Text { text: "检查更新"; color: Theme.text; font.pixelSize: 22; font.weight: Font.DemiBold }
                Text { text: "当前版本 " + Qt.application.version; color: Theme.secondary; font.pixelSize: 12 }
            }
            Item { Layout.fillWidth: true }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: versionContent.implicitHeight + 32
            radius: 12; color: Theme.input; border.color: Theme.line
            ColumnLayout {
                id: versionContent
                anchors.fill: parent; anchors.margins: 16; spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: updateChecker.latestVersion.length ? "最新版本 " + updateChecker.latestVersion : "版本信息"; color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    Icon { width: 17; height: 17; name: updateChecker.state === "current" || updateChecker.state === "downloaded" ? "check" : "refresh"; color: Theme.accent }
                }
                Text { text: "Linux · amd64 / arm64"; color: Theme.muted; font.pixelSize: 11 }
                Text { visible: updateChecker.packageName.length > 0; Layout.fillWidth: true; text: updateChecker.packageName + " · " + window.sizeText(updateChecker.packageSize); color: Theme.secondary; font.pixelSize: 11; wrapMode: Text.WrapAnywhere }
            }
        }
        InlineNotice {
            Layout.fillWidth: true
            error: updateChecker.state === "error"
            iconName: error ? "warning" : "note"
            text: updateChecker.message
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: updateChecker.busy
            spacing: 8
            ProgressBar {
                id: downloadProgress
                Layout.fillWidth: true
                implicitHeight: 6
                from: 0; to: Math.max(1, updateChecker.totalBytes); value: updateChecker.receivedBytes
                indeterminate: updateChecker.state === "checking"
                onIndeterminateChanged: if (!indeterminate) progressFill.x = 0
                background: Rectangle { radius: 3; color: Theme.line }
                contentItem: Item {
                    clip: true
                    Rectangle {
                        id: progressFill
                        height: parent.height; radius: 3; color: Theme.accent
                        width: downloadProgress.indeterminate ? parent.width * 0.28 : parent.width * downloadProgress.position
                        x: 0
                        NumberAnimation on x {
                            running: downloadProgress.visible && downloadProgress.indeterminate
                            from: -progressFill.width; to: downloadProgress.width
                            duration: 1100; loops: Animation.Infinite
                        }
                    }
                }
            }
            Text {
                visible: updateChecker.state === "downloading"
                text: window.sizeText(updateChecker.receivedBytes) + " / " + window.sizeText(updateChecker.totalBytes)
                color: Theme.muted; font.pixelSize: 11
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            visible: updateChecker.releaseNotes.length > 0
            spacing: 10
            Text { text: "版本说明"; color: Theme.secondary; font.pixelSize: 12; font.weight: Font.Medium }
            ScrollView {
                id: notesScroll
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.minimumHeight: 0
                clip: true; contentWidth: availableWidth
                contentHeight: Math.ceil(releaseNotesText.implicitHeight)
                rightPadding: releaseNotesBar.visible ? releaseNotesBar.width + 8 : 0
                bottomPadding: 4
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical: SlimScrollBar {
                    id: releaseNotesBar
                    parent: notesScroll; x: notesScroll.width - width
                    policy: ScrollBar.AsNeeded
                    visible: notesScroll.contentHeight > notesScroll.availableHeight + 1
                    y: notesScroll.topPadding; height: notesScroll.availableHeight
                }
                Text {
                    id: releaseNotesText
                    width: notesScroll.availableWidth
                    text: updateChecker.releaseNotes
                    textFormat: Text.PlainText; wrapMode: Text.Wrap
                    color: Theme.secondary; font.pixelSize: 11; lineHeight: 1.5
                }
            }
        }
        Item { Layout.fillHeight: true; visible: updateChecker.releaseNotes.length === 0 }
        Text {
            Layout.fillWidth: true
            visible: updateChecker.state === "downloaded"
            text: "打开下载文件夹，双击安装包安装。"
            color: Theme.muted; font.pixelSize: 11; wrapMode: Text.Wrap; lineHeight: 1.5
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 9
            ActionButton { visible: updateChecker.busy; text: "取消"; onClicked: updateChecker.cancel() }
            ActionButton { visible: !updateChecker.busy; text: "检查更新"; iconName: "refresh"; enabled: updateChecker.configured; onClicked: updateChecker.check() }
            Item { Layout.fillWidth: true }
            ActionButton { visible: updateChecker.state === "available"; text: "下载安装包"; primary: true; onClicked: updateChecker.download() }
            ActionButton { visible: updateChecker.state === "downloaded"; text: "打开下载文件夹"; primary: true; onClicked: updateChecker.openDownloadFolder() }
        }
    }
}
