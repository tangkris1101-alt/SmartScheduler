import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 批量固定预排：选课程 + 选范围（全部班级 / 年级组 / 手选）+ 选时段，一次钉住 N 班 × M 格
// 设置与清除共用；结果区显示条数并可直接「撤销本次」
Popup {
    id: root
    signal applied()

    property int mode: 0          // 0 设置预排，1 清除预排
    property int scopeMode: 0     // 0 全部班级，1 年级组，2 手选班级
    property int groupIndex: 0
    property var checkedClasses: ({})
    property int conflictMode: 0  // 0 跳过已有，1 覆盖
    property int half: 0          // 0 整节 1 单周 2 双周
    property string resultText: ""
    property bool undoVisible: false

    readonly property var gradeOptions: {
        let out = []
        for (let i = 0; i < groupModel.count; ++i) {
            if (groupModel.get(i, "type") === 0) { // Group::Type::Grade
                out.push({ "id": groupModel.get(i, "id"),
                           "name": groupModel.get(i, "name"),
                           "classCount": groupModel.get(i, "memberCount") })
            }
        }
        return out
    }

    function targetClassIds() {
        let ids = []
        if (scopeMode === 0) {
            for (let i = 0; i < classModel.count; ++i)
                ids.push(classModel.get(i, "id"))
        } else if (scopeMode === 1) {
            const g = gradeOptions[groupIndex]
            if (g)
                ids = app.groupMemberIds(g.id)
        } else {
            for (const key of Object.keys(checkedClasses)) {
                if (checkedClasses[key] === true)
                    ids.push(Number(key))
            }
        }
        return ids
    }
    function currentCourseId() {
        if (courseCombo.currentIndex < 0 || courseCombo.currentIndex >= courseModel.count)
            return -1
        return courseModel.get(courseCombo.currentIndex, "id")
    }
    function openDialog() {
        root.mode = 0
        root.scopeMode = 0
        root.groupIndex = 0
        root.checkedClasses = ({})
        root.conflictMode = 0
        root.half = 0
        root.resultText = ""
        root.undoVisible = false
        slotPicker.reset()
        open()
    }
    function apply() {
        const courseId = currentCourseId()
        const ids = targetClassIds()
        const slots = slotPicker.slots()
        if (courseId < 0 || ids.length === 0 || slots.length === 0) {
            resultText = qsTr("请先选好课程、范围与时段")
            return
        }
        if (mode === 0) {
            const r = app.batchSetPreassignments(courseId, ids, slots, conflictMode, half)
            resultText = qsTr("已设置 %1 条预排（%2 个班）；跳过 %3 条%4")
                    .arg(r.set).arg(r.affected).arg(r.skipped)
                    .arg(r.occupied > 0 ? qsTr("；其中 %1 格当前课表已有课，需重新排课才会生效").arg(r.occupied) : "")
        } else {
            const r = app.batchClearPreassignments(courseId >= 0 ? courseId : -1, ids, slots)
            resultText = qsTr("已清除 %1 条预排（%2 个班）").arg(r.cleared).arg(r.affected)
        }
        undoVisible = app.canUndoBatch()
        root.applied()
    }

    modal: true
    anchors.centerIn: parent
    // 宽度按内容自适应：里面几行是固定宽度控件（时段选择、半周/冲突策略），
    // 写死 620 会让它们冲出卡片；上限留出窗口边距，窄窗口下不超出屏幕
    width: Math.min(Math.max(620, implicitWidth),
                    parent && parent.width > 0 ? parent.width - 48 : 900)
    padding: 0
    background: Rectangle {
        radius: Theme.radiusL
        color: Theme.surface
        border.color: Theme.border
    }
    Overlay.modal: Rectangle {
        color: Theme.dark ? Qt.rgba(0, 0, 0, 0.55) : Qt.rgba(15, 23, 42, 0.28)
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingS

        Text {
            Layout.topMargin: Theme.spacingL
            Layout.leftMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingL
            text: qsTr("批量固定预排")
            font.pixelSize: Theme.fontTitle
            font.bold: true
            color: Theme.text
        }
        Text {
            Layout.leftMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingL
            Layout.fillWidth: true
            Layout.preferredWidth: 0 // 不参与对话框隐式宽度，否则整段未换行宽度会把卡片顶宽
            text: qsTr("预排是硬规定：所选班级的这些格子必须排这门课。适合「全年级每天第 8 节自习」「各班班会固定在周一第 8 节」这类需求；两门课共用一格时，分别用「单周」「双周」各设一次即可。")
            font.pixelSize: Theme.fontCaption
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
        }

        AppTabs {
            Layout.leftMargin: Theme.spacingL
            model: [qsTr("设置预排"), qsTr("清除预排")]
            currentIndex: root.mode
            onTabClicked: function (index) { root.mode = index; root.resultText = "" }
        }

        GridLayout {
            Layout.leftMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingL
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacingM
            rowSpacing: Theme.spacingS

            Text { text: qsTr("课程"); color: Theme.text; font.pixelSize: Theme.fontBody }
            AppComboBox {
                id: courseCombo
                Layout.fillWidth: true
                model: {
                    let names = []
                    for (let i = 0; i < courseModel.count; ++i)
                        names.push(courseModel.get(i, "name"))
                    return names
                }
            }
        }

        RowLayout {
            Layout.leftMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingL
            Layout.fillWidth: true
            spacing: Theme.spacingS
            Text { text: qsTr("范围"); color: Theme.text; font.pixelSize: Theme.fontBody }
            AppTabs {
                model: [qsTr("全部班级"), qsTr("年级组"), qsTr("手选班级")]
                currentIndex: root.scopeMode
                onTabClicked: function (index) { root.scopeMode = index }
            }
            AppComboBox {
                Layout.preferredWidth: 200
                visible: root.scopeMode === 1
                model: {
                    let names = []
                    for (const g of root.gradeOptions)
                        names.push(qsTr("%1（%2 个班）").arg(g.name).arg(g.classCount))
                    return names
                }
                currentIndex: root.groupIndex
                onActivated: root.groupIndex = index
            }
            Item { Layout.fillWidth: true }
        }

        // 手选班级
        Rectangle {
            Layout.leftMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingL
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 132 : 0
            visible: root.scopeMode === 2
            radius: Theme.radiusS
            color: Theme.surfaceAlt
            border.color: Theme.border
            ListView {
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: classModel
                spacing: 2
                delegate: RowLayout {
                    required property int index
                    width: ListView.view.width
                    height: 30
                    AppSwitch {
                        checked: root.checkedClasses[classModel.get(index, "id")] === true
                        onToggled: function (checked) {
                            const m = root.checkedClasses
                            m[classModel.get(index, "id")] = checked
                            root.checkedClasses = m
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: String(classModel.get(index, "name") ?? "")
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                    }
                }
            }
        }

        SlotPicker {
            id: slotPicker
            Layout.leftMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingL
            Layout.fillWidth: true
            onChanged: root.resultText = ""
        }

        RowLayout {
            Layout.leftMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingL
            Layout.fillWidth: true
            visible: root.mode === 0
            spacing: Theme.spacingM
            Text { text: qsTr("半周"); color: Theme.text; font.pixelSize: Theme.fontBody }
            AppTabs {
                model: [qsTr("整节"), qsTr("单周"), qsTr("双周")]
                currentIndex: root.half
                onTabClicked: function (index) { root.half = index }
            }
            Text { text: qsTr("已有预排"); color: Theme.text; font.pixelSize: Theme.fontBody }
            AppTabs {
                model: [qsTr("跳过"), qsTr("覆盖")]
                currentIndex: root.conflictMode
                onTabClicked: function (index) { root.conflictMode = index }
            }
            Item { Layout.fillWidth: true }
            Text {
                text: root.mode === 0
                      ? qsTr("将生成 %1 条（%2 班 × %3 格）")
                            .arg(root.targetClassIds().length * slotPicker.slots().length)
                            .arg(root.targetClassIds().length)
                            .arg(slotPicker.slots().length)
                      : qsTr("将清除匹配的预排（%1 班 × %2 格）")
                            .arg(root.targetClassIds().length)
                            .arg(slotPicker.slots().length)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
                // 窄窗口时先压缩这句统计，别把这一行顶出卡片
                Layout.maximumWidth: Math.max(120, root.width * 0.34)
            }
        }

        Text {
            Layout.leftMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingL
            Layout.fillWidth: true
            visible: root.resultText !== ""
            text: root.resultText
            color: Theme.success
            font.pixelSize: Theme.fontCaption
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.leftMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingL
            Layout.bottomMargin: Theme.spacingL
            Layout.topMargin: Theme.spacingS
            Layout.fillWidth: true
            spacing: Theme.spacingS
            AppButton {
                visible: root.undoVisible
                text: qsTr("撤销本次")
                onClicked: {
                    if (app.undoLastBatch()) {
                        root.resultText = qsTr("已撤销上一次批量操作")
                        root.undoVisible = false
                    }
                }
            }
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("关闭"); onClicked: root.close() }
            AppButton {
                text: root.mode === 0 ? qsTr("设置预排") : qsTr("清除预排")
                variant: "primary"
                onClicked: root.apply()
            }
        }
    }
}
