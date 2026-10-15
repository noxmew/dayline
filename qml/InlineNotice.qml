import QtQuick
import QtQuick.Layouts
import Dayline 1.0

Rectangle {
    id: notice
    property string text: ""
    property bool error: false
    property string iconName: error ? "warning" : "note"
    implicitWidth: 300
    implicitHeight: message.implicitHeight + 24
    radius: 9
    color: error ? Theme.dangerSoft : Theme.accentSoft
    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 9
        Icon {
            Layout.alignment: Qt.AlignTop
            width: 15; height: 15
            name: notice.iconName
            color: notice.error ? Theme.danger : Theme.accent
        }
        Text {
            id: message
            Layout.fillWidth: true
            text: notice.text
            color: notice.error ? Theme.danger : Theme.secondary
            font.pixelSize: 11
            wrapMode: Text.Wrap
            lineHeight: 1.35
        }
    }
}
