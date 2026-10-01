import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 批量不排时间：按「实体集合 × 时段集合」一次禁排/恢复
// 老师支持按教研组一键全选（对应"开会、科组教研时段这些老师不上课"这类需求）
Popup {
    id: root
    signal applied()

    property int entityType: 2    // 0 班级 1 课程 2 老师 3 教室（默认老师：开会教研最常见）
    property var checked: ({})    // {entityId: true}
    property bool forbidden: true
    property string resultText: ""
    property bool undoVisible: false

    readonly property var typeNames: [qsTr("班级"), qsTr("课程"), qsTr("老师"), qsTr("教室")]
    readonly property var typeModels: [classModel, courseModel, teacherModel, classroomModel]
    readonly property var currentModel: typeModels[entityType]
    // 与实体类型匹配的组类型：班级→年级(0)、老师→教研组(1)、课程→课程组(2)
    readonly property int groupType: entityType === 0 ? 0 : (entityType === 2 ? 1 : 2)

    function groupsForType() {
        let out = []
        for (let i = 0; i < groupModel.count; ++i) {
            if (groupModel.get(i, "type") === root.groupType) {
                out.push({ "id": groupModel.get(i, "id"),
                           "name": groupModel.get(i, "name"),
                           "memberCount": groupModel.get(i, "memberCount") })
            }
        }
        return out
    }
    function checkedIds() {
        let out = []
        for (const key of Object.keys(checked)) {
            if (checked[key] === true)
                out.push(Number(key))
        }
        return out
    }
    function selectAllInModel() {
        const m = root.checked
        for (let i = 0; i < root.currentModel.count; ++i)
            m[root.currentModel.get(i, "id")] = true
        root.checked = m
    }
    function selectGroup() {
        const groups = groupsForType()
        const g = groups[groupCombo.currentIndex]
        if (!g)
            return
        const m = root.checked
        for (const id of app.groupMemberIds(g.id))
            m[id] = true
        root.checked = m
    }
    function clearChecked() {
        root.checked = ({})
    }
    function openDialog() {
        root.entityType = 2
        root.checked = ({})
        root.forbidden = true
        root.resultText = ""
        root.undoVisible = false
        slotPicker.reset()
        open()
    }
    function apply() {
        const ids = checkedIds()
        const slots = slotPicker.slots()
        if (ids.length === 0 || slots.length === 0) {
            resultText = qsTr("请先勾选要处理的%1与时段").arg(root.typeNames[root.entityType])
            return
        }
        const r = app.batchSetNoSchedule(root.entityType, ids, slots, root.forbidden)
        resultText = root.forbidden
                ? qsTr("已禁排 %1 格（%2 个%3）").arg(r.set).arg(r.affected).arg(root.typeNames[root.entityType])
                : qsTr("已恢复 %1 格（%2 个%3）").arg(r.cleared).arg(r.affected).arg(root.typeNames[root.entityType])
        undoVisible = app.canUndoBatch()
        root.applied()
    }

    modal: true
    anchors.centerIn: parent
    // 与批量预排同一处理：内容里有固定宽度的行，写死宽度会让它们冲出卡片
    width: Math.min(Math.max(660, implicitWidth),
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
            text: qsTr("批量不排时间")
            font.pixelSize: Theme.fontTitle
            font.bold: true
            color: Theme.text
        }
        Text {
            Layout.leftMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingL
            Layout.fillWidth: true
            Layout.preferredWidth: 0 // 不参与对话框隐式宽度，否则整段未换行宽度会把卡片顶宽
            text: qsTr("「不排」也是硬规定：这些对象在这些时段不上课。老师可先按教研组一键勾选（如「周一第 1 节开班主任会」）。")
            font.pixelSize: Theme.fontCaption
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.leftMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingL
            Layout.fillWidth: true
            spacing: Theme.spacingS
            AppTabs {
                model: root.typeNames
                currentIndex: root.entityType
                onTabClicked: function (index) {
                    root.entityType = index
                    root.checked = ({})
                    root.resultText = ""
                }
            }
            Item { Layout.fillWidth: true }
            AppButton {
                visible: root.entityType !== 3
                variant: "subtle"
                text: qsTr("按教研组勾选…")
                enabled: root.groupsForType().length > 0
                onClicked: groupPop.open()
            }
            AppButton {
                variant: "subtle"
                text: qsTr("全选")
                onClicked: root.selectAllInModel()
            }
            AppButton {
                variant: "subtle"
                text: qsTr("清空勾选")
                onClicked: root.clearChecked()
            }
        }

        // 实体勾选列表
        Rectangle {
            Layout.leftMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingL
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            radius: Theme.radiusS
            color: Theme.surfaceAlt
            border.color: Theme.border
            ListView {
                anchors.fill: parent
                anchors.margins: 4
                clip: true
                model: root.currentModel
                spacing: 2
                delegate: RowLayout {
                    required property int index
                    width: ListView.view.width
                    height: 30
                    AppSwitch {
                        checked: root.checked[root.currentModel.get(index, "id")] === true
                        onToggled: function (isChecked) {
                            const m = root.checked
                            m[root.currentModel.get(index, "id")] = isChecked
                            root.checked = m
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: String(root.currentModel.get(index, "name") ?? "")
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: root.currentModel.count === 0
                text: qsTr("还没有%1，请先到「名单管理」添加").arg(root.typeNames[root.entityType])
                color: Theme.textDisabled
                font.pixelSize: Theme.fontCaption
            }
        }
        Text {
            Layout.leftMargin: Theme.spacingL
            Layout.fillWidth: true
            text: qsTr("已勾选 %1 个%2").arg(root.checkedIds().length).arg(root.typeNames[root.entityType])
            color: Theme.textSecondary
            font.pixelSize: Theme.fontCaption
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
            spacing: Theme.spacingS
            AppTabs {
                model: [qsTr("禁排"), qsTr("恢复")]
                currentIndex: root.forbidden ? 0 : 1
                onTabClicked: function (index) { root.forbidden = index === 0 }
            }
            Item { Layout.fillWidth: true }
            Text {
                text: qsTr("%1 个对象 × %2 个时段 = %3 格")
                        .arg(root.checkedIds().length)
                        .arg(slotPicker.slots().length)
                        .arg(root.checkedIds().length * slotPicker.slots().length)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontCaption
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
                text: root.forbidden ? qsTr("禁排这些时段") : qsTr("恢复这些时段")
                variant: "primary"
                onClicked: root.apply()
            }
        }
    }

    // 按组批量勾选
    Popup {
        id: groupPop
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 380
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
                text: qsTr("按组批量勾选")
                font.pixelSize: Theme.fontSubtitle
                font.bold: true
                color: Theme.text
            }
            Text {
                Layout.leftMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                Layout.fillWidth: true
                text: qsTr("勾选一个组后点「加入勾选」，组内全部%1会被选中（可继续叠加其他组）。")
                        .arg(root.typeNames[root.entityType])
                font.pixelSize: Theme.fontCaption
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
            }
            AppComboBox {
                id: groupCombo
                Layout.leftMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                Layout.fillWidth: true
                model: {
                    let names = []
                    for (const g of root.groupsForType())
                        names.push(qsTr("%1（%2 人/项）").arg(g.name).arg(g.memberCount))
                    return names
                }
            }
            RowLayout {
                Layout.leftMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                Layout.bottomMargin: Theme.spacingL
                Layout.fillWidth: true
                spacing: Theme.spacingS
                Item { Layout.fillWidth: true }
                AppButton { text: qsTr("关闭"); onClicked: groupPop.close() }
                AppButton {
                    text: qsTr("加入勾选")
                    variant: "primary"
                    enabled: root.groupsForType().length > 0
                    onClicked: {
                        root.selectGroup()
                        groupPop.close()
                    }
                }
            }
        }
    }
}
