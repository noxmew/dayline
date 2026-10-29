import QtQuick
import Dayline 1.0

Field {
    id: field
    property alias value: field.text
    property string label: "时间"
    text: "09:00"
    implicitWidth: 78
    implicitHeight: 42
    horizontalAlignment: TextInput.AlignHCenter
    inputMask: "99:99"
    inputMethodHints: Qt.ImhTime
    validator: RegularExpressionValidator { regularExpression: /([01][0-9]|2[0-3]):[0-5][0-9]/ }
    color: readOnly ? Theme.text : Theme.accent
    Accessible.name: label + "，" + text
    Accessible.description: "输入小时和分钟，按上下方向键调整"
    function adjust(amount) {
        if (readOnly || !acceptableInput) return
        const position = cursorPosition
        const hours = position < 3
        let part = Number(text.slice(hours ? 0 : 3, hours ? 2 : 5))
        const limit = hours ? 24 : 60
        part = (part + amount + limit) % limit
        text = hours ? String(part).padStart(2, "0") + text.slice(2)
                     : text.slice(0, 3) + String(part).padStart(2, "0")
        select(hours ? 0 : 3, hours ? 2 : 5)
    }
    Keys.onUpPressed: adjust(1)
    Keys.onDownPressed: adjust(-1)
    background: Rectangle {
        radius: 9
        color: field.hovered ? Theme.hover : Theme.panel
        border.width: field.activeFocus ? 1 : 0
        border.color: Theme.accent
    }
}
