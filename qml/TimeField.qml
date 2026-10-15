import QtQuick
import QtQuick.Layouts
import Dayline 1.0

RowLayout {
    id: field
    property string value: "09:00"
    property bool readOnly: false
    property string label: "时间"
    readonly property bool pickerVisible: hourField.popup.visible || minuteField.popup.visible
    spacing: 6
    function closePicker() {
        hourField.popup.close()
        minuteField.popup.close()
    }

    SelectField {
        id: hourField
        Layout.fillWidth: true
        model: Array.from({length: 24}, (_, hour) => String(hour).padStart(2, "0"))
        currentIndex: Number(field.value.slice(0, 2))
        displayText: currentText + " 时"
        enabled: !field.readOnly
        Accessible.name: field.label + "（小时）"
        onActivated: field.value = currentText + ":" + minuteField.currentText
    }
    SelectField {
        id: minuteField
        Layout.fillWidth: true
        model: Array.from({length: 60}, (_, minute) => String(minute).padStart(2, "0"))
        currentIndex: Number(field.value.slice(3, 5))
        displayText: currentText + " 分"
        enabled: !field.readOnly
        Accessible.name: field.label + "（分钟）"
        onActivated: field.value = hourField.currentText + ":" + currentText
    }
}
