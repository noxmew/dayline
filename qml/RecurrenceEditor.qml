import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Dayline 1.0

ColumnLayout {
    id: control
    property bool readOnly: false
    property string startDate: ""
    property string frequency: "none"
    property int interval: 1
    property var weekdays: []
    property string weekStart: "MO"
    property string endMode: "never"
    property string until: ""
    property int count: 1
    property bool loading: false
    property bool followStartDate: true
    property bool workdaysOnly: false
    property int previousWeekday: 0
    readonly property bool repeating: frequency !== "none" && frequency !== "custom"
    readonly property bool pickerVisible: repeatPicker.visible || endPicker.visible
    readonly property var frequencyOptions: [
        {id:"daily", name:"每天", unit:"天"}, {id:"weekly", name:"每周", unit:"周"},
        {id:"monthly", name:"每月", unit:"个月"}, {id:"yearly", name:"每年", unit:"年"}
    ]
    readonly property var presets: [
        {id:"none", interval:1, name:"从不"}, {id:"daily", interval:1, name:"每天"},
        {id:"weekly", interval:1, name:"每周"}, {id:"weekly", interval:2, name:"每两周"},
        {id:"monthly", interval:1, name:"每月"}, {id:"yearly", interval:1, name:"每年"}
    ]
    readonly property var dayNames: ["周一", "周二", "周三", "周四", "周五", "周六", "周日"]
    readonly property string summary: {
        if (frequency === "none") return "从不"
        if (frequency === "custom") return "自定义重复（只读）"
        const day = new Date(startDate + "T12:00:00")
        if (frequency === "daily") return interval === 1 ? "每天" : "每 " + interval + " 天"
        if (frequency === "weekly") {
            const prefix = interval === 1 ? "每周" : interval === 2 ? "每两周" : "每 " + interval + " 周"
            return weekdays.length === 1 && weekdays[0] === weekday(startDate) ? prefix
                : prefix + " · " + weekdays.map(day => dayNames[day - 1]).join("、")
        }
        if (frequency === "monthly") return (interval === 1 ? "每月" : "每 " + interval + " 个月") + " " + day.getDate() + " 日"
        return (interval === 1 ? "每年" : "每 " + interval + " 年") + " " + (day.getMonth() + 1) + " 月 " + day.getDate() + " 日"
    }
    spacing: 8
    onStartDateChanged: {
        const day = weekday(startDate)
        if (!loading && followStartDate && frequency === "weekly" && weekdays.length === 1 && weekdays[0] === previousWeekday) weekdays = [day]
        previousWeekday = day
    }

    function weekday(date) { return (new Date(date + "T12:00:00").getDay() + 6) % 7 + 1 }
    function load(rule) {
        loading = true
        frequency = rule.frequency || "none"
        interval = rule.interval || 1
        weekdays = (rule.weekdays || []).slice()
        weekStart = rule.weekStart || "MO"
        endMode = rule.endMode || "never"
        until = rule.until || startDate
        count = rule.count || 1
        previousWeekday = weekday(startDate)
        loading = false
    }
    function value() {
        if (frequency === "none") return {frequency:"none", interval:1, weekdays:[], endMode:"never", until:"", count:1, weekStart:"MO"}
        return {frequency:frequency, interval:interval, weekdays:frequency === "weekly" ? weekdays.slice() : [],
            endMode:endMode, until:endMode === "until" ? until : "", count:endMode === "count" ? count : 1, weekStart:weekStart}
    }
    function matchesPreset(preset) {
        return frequency === preset.id && interval === preset.interval && (frequency !== "weekly"
            || (weekdays.length === 1 && weekdays[0] === weekday(startDate)))
    }
    function selectPreset(preset) {
        frequency = preset.id
        interval = preset.interval
        weekdays = frequency === "weekly" ? [weekday(startDate)] : []
        weekStart = "MO"
        if (frequency === "none") endMode = "never"
        repeatPicker.close()
    }
    function closePickers() {
        customFrequency.popup.close()
        untilField.closePicker()
        repeatPicker.close()
        endPicker.close()
    }

    EditorGroup {
        Layout.fillWidth: true
        EditorRow {
            Layout.fillWidth: true
            separator: control.repeating
            ActionButton {
                Layout.fillWidth: true
                quiet: true; padding: 0; enabled: !control.readOnly
                Accessible.name: "重复，" + control.summary
                onClicked: { repeatPicker.customPage = false; repeatPicker.open() }
                contentItem: RowLayout {
                    spacing: 12
                    Text { text: "重复"; color: Theme.text; font.pixelSize: 13 }
                    Text { Layout.fillWidth: true; text: control.summary; horizontalAlignment: Text.AlignRight; color: Theme.secondary; font.pixelSize: 13; elide: Text.ElideRight }
                    Icon { visible: !control.readOnly; name: "right"; width: 15; height: 15; color: Theme.muted }
                }
            }
        }
        EditorRow {
            visible: control.repeating
            Layout.fillWidth: true
            ActionButton {
                Layout.fillWidth: true
                quiet: true; padding: 0; enabled: !control.readOnly
                Accessible.name: "结束重复，" + endingValue.text
                onClicked: endPicker.open()
                contentItem: RowLayout {
                    spacing: 12
                    Text { text: "结束重复"; color: Theme.text; font.pixelSize: 13 }
                    Text {
                        id: endingValue
                        Layout.fillWidth: true; horizontalAlignment: Text.AlignRight
                        text: control.endMode === "never" ? "永不" : control.endMode === "count" ? control.count + " 次后" : Qt.formatDate(new Date(control.until + "T12:00:00"), "yyyy年M月d日")
                        color: Theme.secondary; font.pixelSize: 13; elide: Text.ElideRight
                    }
                    Icon { visible: !control.readOnly; name: "right"; width: 15; height: 15; color: Theme.muted }
                }
            }
        }
    }

    Dialog {
        id: repeatPicker
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(430, parent.width - 32)
        padding: 18
        modal: true; focus: true
        closePolicy: Popup.CloseOnEscape
        property bool customPage: false
        property string draftFrequency: "daily"
        property var draftWeekdays: []
        property string draftError: ""
        function openCustom() {
            draftFrequency = control.repeating ? control.frequency : "daily"
            customFrequency.currentIndex = control.frequencyOptions.findIndex(option => option.id === draftFrequency)
            intervalField.text = String(control.interval)
            draftWeekdays = control.weekdays.length ? control.weekdays.slice() : [control.weekday(control.startDate)]
            draftError = ""
            customPage = true
        }
        function toggleDay(day) {
            let next = draftWeekdays.filter(value => value !== day)
            if (next.length === draftWeekdays.length) next.push(day)
            if (next.length) draftWeekdays = next.sort((a,b) => a-b)
        }
        function applyCustom() {
            if (!intervalField.acceptableInput) { draftError = "重复间隔请填写 1–999。"; return }
            control.frequency = draftFrequency
            control.interval = Number(intervalField.text)
            control.weekdays = draftFrequency === "weekly" ? draftWeekdays.slice() : []
            control.weekStart = "MO"
            close()
        }
        background: Rectangle { radius: 16; color: Theme.panel; border.color: Theme.lineStrong }
        Overlay.modal: Rectangle { color: Theme.overlay }
        contentItem: ColumnLayout {
            spacing: 14
            RowLayout {
                Layout.fillWidth: true
                ActionButton { text: repeatPicker.customPage ? "返回" : "取消"; quiet: true; onClicked: repeatPicker.customPage ? repeatPicker.customPage = false : repeatPicker.close() }
                Text { Layout.fillWidth: true; text: repeatPicker.customPage ? "自定义重复" : "重复"; horizontalAlignment: Text.AlignHCenter; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                ActionButton { text: "完成"; quiet: true; visible: repeatPicker.customPage; onClicked: repeatPicker.applyCustom() }
                Item { visible: !repeatPicker.customPage; Layout.preferredWidth: 50 }
            }
            EditorGroup {
                visible: !repeatPicker.customPage
                Layout.fillWidth: true
                Repeater {
                    model: control.presets
                    delegate: EditorRow {
                        required property var modelData
                        Layout.fillWidth: true; separator: true
                        ActionButton {
                            Layout.fillWidth: true; quiet: true; padding: 0
                            Accessible.name: modelData.name
                            onClicked: control.selectPreset(modelData)
                            contentItem: RowLayout {
                                Text { Layout.fillWidth: true; text: modelData.name; font.pixelSize: 13; color: Theme.text }
                                Icon { visible: control.matchesPreset(modelData); name: "check"; width: 18; height: 18; color: Theme.accent }
                            }
                        }
                    }
                }
                EditorRow {
                    Layout.fillWidth: true
                    ActionButton {
                        Layout.fillWidth: true; quiet: true; padding: 0
                        Accessible.name: "自定义重复"
                        onClicked: repeatPicker.openCustom()
                        contentItem: RowLayout {
                            Text { Layout.fillWidth: true; text: "自定义…"; color: Theme.text; font.pixelSize: 13 }
                            Icon { name: "right"; width: 15; height: 15; color: Theme.muted }
                        }
                    }
                }
            }
            EditorGroup {
                visible: repeatPicker.customPage
                Layout.fillWidth: true
                EditorRow {
                    Layout.fillWidth: true; label: "频率"; separator: true
                    Item { Layout.fillWidth: true }
                    SelectField {
                        id: customFrequency
                        Layout.preferredWidth: 156
                        model: control.frequencyOptions; textRole: "name"
                        onActivated: repeatPicker.draftFrequency = control.frequencyOptions[currentIndex].id
                        Accessible.name: "自定义重复频率"
                    }
                }
                EditorRow {
                    Layout.fillWidth: true; label: "每隔"
                    Item { Layout.fillWidth: true }
                    ActionButton { text: "−"; enabled: Number(intervalField.text) > 1; onClicked: intervalField.text = String(Number(intervalField.text) - 1); Accessible.name: "减少重复间隔" }
                    Field {
                        id: intervalField
                        Layout.preferredWidth: 64
                        horizontalAlignment: TextInput.AlignHCenter
                        inputMethodHints: Qt.ImhDigitsOnly
                        validator: IntValidator { bottom: 1; top: 999 }
                        Accessible.name: "重复间隔"
                    }
                    ActionButton { text: "+"; enabled: Number(intervalField.text) < 999; onClicked: intervalField.text = String(Number(intervalField.text) + 1); Accessible.name: "增加重复间隔" }
                    Text { text: control.frequencyOptions.find(option => option.id === repeatPicker.draftFrequency).unit; color: Theme.secondary; font.pixelSize: 13 }
                }
            }
            ColumnLayout {
                visible: repeatPicker.customPage && repeatPicker.draftFrequency === "weekly"
                Layout.fillWidth: true; spacing: 10
                Text { text: "重复日期"; color: Theme.secondary; font.pixelSize: 12 }
                RowLayout {
                    Layout.fillWidth: true; spacing: 5
                    Repeater {
                        model: ["一", "二", "三", "四", "五", "六", "日"]
                        delegate: ActionButton {
                            required property int index
                            required property string modelData
                            Layout.fillWidth: true; implicitWidth: 0; implicitHeight: 38; padding: 0
                            text: modelData
                            primary: repeatPicker.draftWeekdays.indexOf(index + 1) >= 0
                            onClicked: repeatPicker.toggleDay(index + 1)
                            Accessible.name: control.dayNames[index]
                            Accessible.checkable: true
                            Accessible.checked: primary
                        }
                    }
                }
            }
            Text {
                visible: repeatPicker.customPage && (repeatPicker.draftFrequency === "monthly" || repeatPicker.draftFrequency === "yearly")
                Layout.fillWidth: true
                text: repeatPicker.draftFrequency === "monthly" ? "每次在开始日期的同一天重复；没有该日期的月份跳过。" : "每次在开始日期的同月同日重复；没有该日期的年份跳过。"
                color: Theme.secondary; font.pixelSize: 11; wrapMode: Text.Wrap
            }
            InlineNotice { visible: repeatPicker.draftError.length > 0; Layout.fillWidth: true; error: true; text: repeatPicker.draftError }
        }
    }

    Dialog {
        id: endPicker
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(430, parent.width - 32)
        padding: 18
        modal: true; focus: true
        closePolicy: Popup.CloseOnEscape
        property string draftMode: "never"
        property string draftError: ""
        onAboutToShow: {
            draftMode = control.endMode
            untilField.value = control.until || control.startDate
            countField.text = String(control.count)
            draftError = ""
        }
        function apply() {
            if (draftMode === "until" && untilField.value < control.startDate) { draftError = "结束日期不能早于首次日程。"; return }
            if (draftMode === "count" && !countField.acceptableInput) { draftError = "请填写 1–9999 次（含首次）。"; return }
            control.endMode = draftMode
            control.until = untilField.value
            control.count = Number(countField.text)
            close()
        }
        background: Rectangle { radius: 16; color: Theme.panel; border.color: Theme.lineStrong }
        Overlay.modal: Rectangle { color: Theme.overlay }
        contentItem: ColumnLayout {
            spacing: 14
            RowLayout {
                Layout.fillWidth: true
                ActionButton { text: "取消"; quiet: true; onClicked: endPicker.close() }
                Text { Layout.fillWidth: true; text: "结束重复"; horizontalAlignment: Text.AlignHCenter; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                ActionButton { text: "完成"; quiet: true; onClicked: endPicker.apply() }
            }
            EditorGroup {
                Layout.fillWidth: true
                Repeater {
                    model: [{id:"never", name:"永不"}, {id:"until", name:"在指定日期"}, {id:"count", name:"重复指定次数"}]
                    delegate: EditorRow {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true; separator: index < 2
                        ActionButton {
                            Layout.fillWidth: true; quiet: true; padding: 0
                            Accessible.name: modelData.name
                            onClicked: endPicker.draftMode = modelData.id
                            contentItem: RowLayout {
                                Text { Layout.fillWidth: true; text: modelData.name; font.pixelSize: 13; color: Theme.text }
                                Icon { visible: endPicker.draftMode === modelData.id; name: "check"; width: 18; height: 18; color: Theme.accent }
                            }
                        }
                    }
                }
            }
            DateField { id: untilField; visible: endPicker.draftMode === "until"; Layout.fillWidth: true; label: "重复结束日期" }
            RowLayout {
                visible: endPicker.draftMode === "count"
                Layout.fillWidth: true
                Field {
                    id: countField
                    Layout.fillWidth: true
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 1; top: 9999 }
                    Accessible.name: control.workdaysOnly ? "实际工作日日程次数" : "重复次数（含首次）"
                }
                    Text { text: control.workdaysOnly ? "次（仅计工作日）" : "次（含首次）"; color: Theme.secondary; font.pixelSize: 12 }
                }
            Text { visible: endPicker.draftMode === "until"; text: "包含所选日期当天的日程。"; color: Theme.secondary; font.pixelSize: 11 }
            Text {
                visible: control.workdaysOnly
                Layout.fillWidth: true
                text: endPicker.draftMode === "never" ? "仅工作日需要指定结束日期或次数，未来年份的节假日安排尚未确定。"
                    : endPicker.draftMode === "count" ? "次数按实际保留的日程计算，休息日不计入次数；若首次落在休息日，也会跳过。"
                    : "结束日期需在已收录的节假日年份内；休息日会自动跳过。"
                color: Theme.secondary; font.pixelSize: 11; wrapMode: Text.Wrap
            }
            InlineNotice { visible: endPicker.draftError.length > 0; Layout.fillWidth: true; error: true; text: endPicker.draftError }
        }
    }
}
