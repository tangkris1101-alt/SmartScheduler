import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 时段选择器（批量预排 / 批量不排共用）：
//   mode 0「每天第 N 节」：选一个节次 + 勾选星期
//   mode 1「自选格子」：在 星期×节次 网格上任意点选（点表头可整行/整列）
// 用 slots() 取最终的 slot 列表；changed() 在同一选择变化时发出。
Item {
    id: picker
    implicitHeight: column.implicitHeight

    property int days: app.daysPerWeek
    property int periods: app.periodsPerDay
    property int mode: 0
    property int periodIndex: 7          // 0 基（「每天第 N 节」用）
    property var dayEnabled: ({})        // {day: true}
    property var picked: ({})            // {slot: true}（自选模式）
    signal changed()

    function slotOf(day, period) { return day * picker.periods + period }
    function slots() {
        let out = []
        if (picker.mode === 0) {
            for (let d = 0; d < picker.days; ++d) {
                if (picker.dayEnabled[d] === true && picker.periodIndex >= 0
                        && picker.periodIndex < picker.periods)
                    out.push(picker.slotOf(d, picker.periodIndex))
            }
        } else {
            for (let d = 0; d < picker.days; ++d) {
                for (let p = 0; p < picker.periods; ++p) {
                    if (picker.picked[picker.slotOf(d, p)] === true)
                        out.push(picker.slotOf(d, p))
                }
            }
        }
        return out
    }
    function reset() {
        let days = {}
        for (let d = 0; d < picker.days; ++d)
            days[d] = true
        picker.dayEnabled = days
        picker.picked = ({})
        picker.periodIndex = Math.max(0, picker.periods - 1)
        picker.changed()
    }
    function toggleDay(d) {
        const m = picker.dayEnabled
        m[d] = !(m[d] === true)
        picker.dayEnabled = m
        picker.changed()
    }

    readonly property var dayNames: [qsTr("周一"), qsTr("周二"), qsTr("周三"), qsTr("周四"),
                                     qsTr("周五"), qsTr("周六"), qsTr("周日")]

    ColumnLayout {
        id: column
        width: parent.width
        spacing: Theme.spacingS

        AppTabs {
            model: [qsTr("每天第 N 节"), qsTr("自选格子")]
            currentIndex: picker.mode
            onTabClicked: function (index) { picker.mode = index }
        }

        // 模式一：节次 + 星期
        RowLayout {
            Layout.fillWidth: true
            visible: picker.mode === 0
            spacing: Theme.spacingM

            Text { text: qsTr("第"); color: Theme.text; font.pixelSize: Theme.fontBody }
            AppStepper {
                id: periodStepper
                from: 1
                to: picker.periods
                value: picker.periodIndex + 1
                onValueModified: {
                    picker.periodIndex = value - 1
                    picker.changed()
                }
            }
            Text { text: qsTr("节"); color: Theme.text; font.pixelSize: Theme.fontBody }
            Repeater {
                model: picker.days
                delegate: Rectangle {
                    required property int index
                    Layout.preferredWidth: 46
                    Layout.preferredHeight: 28
                    radius: Theme.radiusS
                    color: picker.dayEnabled[index] === true
                           ? Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.16)
                           : Theme.surfaceAlt
                    border.color: picker.dayEnabled[index] === true ? Theme.primary : Theme.border
                    Text {
                        anchors.centerIn: parent
                        text: picker.dayNames[index]
                        color: picker.dayEnabled[index] === true ? Theme.primary : Theme.textSecondary
                        font.pixelSize: Theme.fontCaption
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: picker.toggleDay(index)
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }

        // 模式二：网格自选
        ColumnLayout {
            Layout.fillWidth: true
            visible: picker.mode === 1
            spacing: 3

            RowLayout {
                Layout.fillWidth: true
                spacing: 3
                Item { Layout.preferredWidth: 52 }
                Repeater {
                    model: picker.days
                    delegate: Rectangle {
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredHeight: 26
                        radius: Theme.radiusS
                        color: dayHeaderHover.containsMouse ? Theme.overlayHover : Theme.surfaceAlt
                        Text {
                            anchors.centerIn: parent
                            text: picker.dayNames[index]
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                        }
                        MouseArea {
                            id: dayHeaderHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { // 整列切换
                                const m = picker.picked
                                let all = true
                                for (let p = 0; p < picker.periods; ++p) {
                                    if (m[picker.slotOf(index, p)] !== true)
                                        all = false
                                }
                                for (let p = 0; p < picker.periods; ++p) {
                                    if (all)
                                        delete m[picker.slotOf(index, p)]
                                    else
                                        m[picker.slotOf(index, p)] = true
                                }
                                picker.picked = m
                                picker.changed()
                            }
                        }
                    }
                }
            }
            Repeater {
                model: picker.periods
                delegate: RowLayout {
                    required property int index
                    Layout.fillWidth: true
                    spacing: 3
                    Rectangle {
                        Layout.preferredWidth: 52
                        Layout.preferredHeight: 26
                        radius: Theme.radiusS
                        color: periodHeaderHover.containsMouse ? Theme.overlayHover : Theme.surfaceAlt
                        Text {
                            anchors.centerIn: parent
                            text: qsTr("第%1节").arg(index + 1)
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                        }
                        MouseArea {
                            id: periodHeaderHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { // 整行切换
                                const m = picker.picked
                                let all = true
                                for (let d = 0; d < picker.days; ++d) {
                                    if (m[picker.slotOf(d, index)] !== true)
                                        all = false
                                }
                                for (let d = 0; d < picker.days; ++d) {
                                    if (all)
                                        delete m[picker.slotOf(d, index)]
                                    else
                                        m[picker.slotOf(d, index)] = true
                                }
                                picker.picked = m
                                picker.changed()
                            }
                        }
                    }
                    Repeater {
                        model: picker.days
                        delegate: Rectangle {
                            required property int index
                            Layout.fillWidth: true
                            Layout.preferredHeight: 26
                            radius: Theme.radiusS
                            color: picker.picked[picker.slotOf(index, parent.index)] === true
                                   ? Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.20)
                                   : (cellHover.containsMouse ? Theme.overlayHover : "transparent")
                            border.color: picker.picked[picker.slotOf(index, parent.index)] === true
                                          ? Theme.primary : Theme.border
                            MouseArea {
                                id: cellHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    const m = picker.picked
                                    const slot = picker.slotOf(index, parent.index)
                                    if (m[slot] === true)
                                        delete m[slot]
                                    else
                                        m[slot] = true
                                    picker.picked = m
                                    picker.changed()
                                }
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingS
            Text {
                Layout.fillWidth: true
                text: qsTr("已选 %1 个时段").arg(picker.slots().length)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontCaption
            }
            AppButton {
                variant: "subtle"
                text: qsTr("清空")
                visible: picker.mode === 1
                onClicked: { picker.picked = ({}); picker.changed() }
            }
            AppButton {
                variant: "subtle"
                text: qsTr("全选")
                visible: picker.mode === 1
                onClicked: {
                    const m = {}
                    for (let d = 0; d < picker.days; ++d)
                        for (let p = 0; p < picker.periods; ++p)
                            m[picker.slotOf(d, p)] = true
                    picker.picked = m
                    picker.changed()
                }
            }
        }
    }
}
