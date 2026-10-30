import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Dayline 1.0

Rectangle {
    id: root
    implicitHeight: content.implicitHeight + 32
    radius: 12
    color: Theme.input
    border.color: Theme.line

    ColumnLayout {
        id: content
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: parent.top; anchors.margins: 16
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: "MCP 服务"; color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium }
            Toggle { checked: mcpServer.enabled; onToggled: mcpServer.enabled = checked; Accessible.name: "启用 MCP 服务" }
        }
        Text {
            Layout.fillWidth: true
            text: "让本机 AI 客户端查询和编辑日历、日程及备忘录。使用 Streamable HTTP，随日笺运行。"
            color: Theme.secondary; font.pixelSize: 11; wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text { text: "端口"; color: Theme.text; font.pixelSize: 12 }
            Field {
                id: portField
                Layout.fillWidth: true
                implicitHeight: 36
                text: mcpServer.port.toString()
                validator: IntValidator { bottom: 1; top: 65535 }
                inputMethodHints: Qt.ImhDigitsOnly
                invalid: text.length > 0 && !acceptableInput
                Accessible.name: "MCP 监听端口"
                onAccepted: if (acceptableInput) mcpServer.port = Number(text)
            }
            ActionButton {
                text: "应用"; implicitHeight: 36
                enabled: portField.acceptableInput && Number(portField.text) !== mcpServer.port
                onClicked: mcpServer.port = Number(portField.text)
            }
        }
        Text {
            Layout.fillWidth: true
            text: !mcpServer.enabled ? "已关闭" : mcpServer.running ? "运行中 · 仅本机可连接" : "启动失败"
            color: mcpServer.enabled && !mcpServer.running ? Theme.danger : Theme.secondary
            font.pixelSize: 11
        }
        InlineNotice { Layout.fillWidth: true; visible: mcpServer.error.length > 0; error: true; text: mcpServer.error }
        ActionButton { visible: mcpServer.enabled && !mcpServer.running; text: "重试启动"; onClicked: mcpServer.restart() }
        ColumnLayout {
            Layout.fillWidth: true
            visible: mcpServer.enabled
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: mcpServer.url
                color: Theme.secondary; font.pixelSize: 12; wrapMode: Text.WrapAnywhere
            }
            RowLayout {
                Layout.fillWidth: true
                ActionButton { text: "复制地址"; implicitHeight: 32; onClicked: { mcpServer.copyUrl(); copyState.text = "地址已复制"; copyTimer.restart() } }
                ActionButton { text: "复制连接配置"; implicitHeight: 32; onClicked: { mcpServer.copyConfiguration(); copyState.text = "配置已复制"; copyTimer.restart() } }
            }
            Text { id: copyState; visible: text.length > 0; color: Theme.secondary; font.pixelSize: 11 }
        }
    }
    Timer { id: copyTimer; interval: 2500; onTriggered: copyState.text = "" }
}
