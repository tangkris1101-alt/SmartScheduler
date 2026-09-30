import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 限制条件页：不排时间网格 + 固定预排/老师活动时间
Item {
    id: root
    property int mode: 0        // 0 不排时间，1 预排/活动
    property int entityType: 0  // 0班级 1课程 2老师 3教室
    property int entityIndex: 0 // 当前实体在其名单中的行
    property int rev: 0

    // 预排/活动状态
    property int preMode: 0     // 0 班级预排 1 老师活动
    property int courseIndex: 0 // 预排课程选择（该班计划内）
    property int actCourseIndex: 0

    // 网格状态表：{slot: true} / {slot: courseName}
    property var forbiddenMap: ({})
    property var preMap: ({})
    property var actMap: ({})

    readonly property var typeNames: [qsTr("班级"), qsTr("课程"), qsTr("老师"), qsTr("教室")]
    readonly property var typeModels: [classModel, courseModel, teacherModel, classroomModel]
    readonly property var currentTypeModel: typeModels[entityType]

    function currentEntityId() {
        if (root.entityIndex < 0 || root.entityIndex >= root.currentTypeModel.count)
            return -1
        return root.currentTypeModel.get(root.entityIndex, "id")
    }

    function refresh() {
        root.rev++
        if (root.mode === 0) {
            const id = currentEntityId()
            const slots = id >= 0 ? app.noScheduleSlots(root.entityType, id) : []
            let m = {}
            for (const s of slots)
                m[s] = true
            root.forbiddenMap = m
        } else if (root.preMode === 0) {
            const cid = root.classForPre()
            let m = {}
            if (cid >= 0) {
                for (const p of app.preAssignments(cid))
                    m[p.slotIndex] = p.courseName
            }
            root.preMap = m
        } else {
            const tid = root.teacherForActivity()
            let m = {}
            if (tid >= 0) {
                for (const a of app.teacherActivities(tid))
                    m[a.slotIndex] = a.courseName
            }
            root.actMap = m
        }
    }

    function classForPre() {
        if (preClassCombo.currentIndex < 0 || preClassCombo.currentIndex >= classModel.count)
            return -1
        return classModel.get(preClassCombo.currentIndex, "id")
    }
    function teacherForActivity() {
        if (actTeacherCombo.currentIndex < 0 || actTeacherCombo.currentIndex >= teacherModel.count)
            return -1
        return teacherModel.get(actTeacherCombo.currentIndex, "id")
    }
    function planCourseList() {
        const cid = classForPre()
        return cid >= 0 ? app.planCourses(cid) : []
    }

    onVisibleChanged: if (visible) refresh()
    Component.onCompleted: refresh()
    Connections {
        target: app
        function onProjectChanged() { root.refresh() }
        function onConstraintsChanged() { root.refresh() }
    }
    Connections {
        target: classModel
        function onDataChanged() { root.refresh() }
        function onCountChanged() { root.refresh() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingXL
        spacing: Theme.spacingM

        RowLayout {
            Layout.fillWidth: true
            AppTabs {
                model: [qsTr("不排时间"), qsTr("预排 / 老师活动")]
                currentIndex: root.mode
                onTabClicked: function (index) { root.mode = index; root.refresh() }
            }
            Item { Layout.fillWidth: true }
        }

        // 硬约束说明：让「限制条件」这一页的定位一目了然
        Rectangle {
            Layout.fillWidth: true
            height: hintCol.implicitHeight + Theme.spacingM * 2
            radius: Theme.radiusM
            color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.08)
            border.color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.35)

            ColumnLayout {
                id: hintCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingM
                spacing: Theme.spacingXS

                Text {
                    text: qsTr("这里设置的是「硬规定」（不许破）")
                    color: Theme.text
                    font.pixelSize: Theme.fontBody
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("不排时间 / 固定预排 / 老师活动时间：系统排课时必须遵守，违反会直接体现在「质量报告」的硬指标里。"
                               + "想按年级整体设置作息（如高三周六加课、小学每天少一节），用「按年级组批量设置…」。")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.WordWrap
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("另外两档供理解：「可以让，但要人点头」= 手工微调；「尽量」= 质量报告的软指标与老师五项，用来挑更好的方案。")
                    color: Theme.textDisabled
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.WordWrap
                }
            }
        }

        // 提醒横幅（高度随文案自适应，避免长文案被截断）
        Rectangle {
            Layout.fillWidth: true
            height: warnText.implicitHeight + Theme.spacingM * 1.5
            radius: Theme.radiusM
            color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.12)
            visible: root.mode === 0
            Text {
                id: warnText
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: Theme.spacingM
                text: qsTr("提醒：「不排」设置过多可能导致排不出课表，请只设置确实必要的限制。")
                color: Theme.warning
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.WordWrap
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.mode

            // ==================== 不排时间 ====================
            ColumnLayout {
                spacing: Theme.spacingM

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingM

                    AppComboBox {
                        Layout.preferredWidth: 120
                        model: root.typeNames
                        currentIndex: root.entityType
                        onActivated: {
                            root.entityType = index
                            root.entityIndex = 0
                            root.refresh()
                        }
                    }
                    AppComboBox {
                        id: entityCombo
                        Layout.preferredWidth: 200
                        model: {
                            root.rev
                            let names = []
                            for (let i = 0; i < root.currentTypeModel.count; ++i)
                                names.push(root.currentTypeModel.get(i, "name"))
                            return names
                        }
                        currentIndex: root.entityIndex
                        onActivated: { root.entityIndex = index; root.refresh() }
                    }
                    AppButton {
                        text: qsTr("清除全部")
                        enabled: root.currentEntityId() >= 0
                        onClicked: app.clearNoSchedule(root.entityType, root.currentEntityId())
                    }
                    AppButton {
                        text: qsTr("按年级组批量设置…")
                        visible: root.mode === 0
                        onClicked: groupMaskDialog.open()
                    }
                    AppButton {
                        text: qsTr("⊞ 批量设置…")
                        visible: root.mode === 0
                        variant: "primary"
                        onClicked: batchNoScheduleDialog.openDialog()
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: qsTr("点击单元格切换；点星期/节次表头可整列/整行切换")
                        color: Theme.textDisabled
                        font.pixelSize: Theme.fontCaption
                    }
                }

                AppCard {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Item {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingM
                        TimetableGrid {
                            id: forbidGrid
                            anchors.fill: parent
                            visible: root.currentEntityId() >= 0
                            headerActions: true
                            revision: root.rev
                            cellState: function (day, period) {
                                return root.forbiddenMap[slotOf(day, period)] === true ? 1 : 0
                            }
                            onCellClicked: function (day, period) {
                                const slot = slotOf(day, period)
                                app.setNoScheduleSlot(root.entityType, root.currentEntityId(), slot,
                                                      root.forbiddenMap[slot] !== true)
                            }
                            onDayHeaderClicked: function (day) {
                                // 整列切换：有未禁的则全禁，否则全恢复
                                const id = root.currentEntityId()
                                let allForbidden = true
                                for (let p = 0; p < forbidGrid.periods; ++p) {
                                    if (root.forbiddenMap[slotOf(day, p)] !== true)
                                        allForbidden = false
                                }
                                for (let p = 0; p < forbidGrid.periods; ++p)
                                    app.setNoScheduleSlot(root.entityType, id, slotOf(day, p), !allForbidden)
                            }
                            onPeriodHeaderClicked: function (period) {
                                const id = root.currentEntityId()
                                let allForbidden = true
                                for (let d = 0; d < forbidGrid.days; ++d) {
                                    if (root.forbiddenMap[slotOf(d, period)] !== true)
                                        allForbidden = false
                                }
                                for (let d = 0; d < forbidGrid.days; ++d)
                                    app.setNoScheduleSlot(root.entityType, id, slotOf(d, period), !allForbidden)
                            }
                        }
                        AppEmptyState {
                            anchors.centerIn: parent
                            visible: root.currentEntityId() < 0
                            icon: "🚫"
                            title: qsTr("请先添加%1").arg(root.typeNames[root.entityType])
                            hint: qsTr("在「名单管理」页添加后回到这里设置不排时间")
                        }
                    }
                }
            }

            // ==================== 预排 / 老师活动 ====================
            ColumnLayout {
                spacing: Theme.spacingM

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingM

                    AppComboBox {
                        Layout.preferredWidth: 140
                        model: [qsTr("班级预排"), qsTr("老师活动")]
                        currentIndex: root.preMode
                        onActivated: { root.preMode = index; root.refresh() }
                    }

                    AppComboBox {
                        id: preClassCombo
                        Layout.preferredWidth: 180
                        visible: root.preMode === 0
                        model: {
                            root.rev
                            let names = []
                            for (let i = 0; i < classModel.count; ++i)
                                names.push(classModel.get(i, "name"))
                            return names
                        }
                        onActivated: { root.courseIndex = 0; root.refresh() }
                    }
                    AppComboBox {
                        Layout.preferredWidth: 180
                        visible: root.preMode === 0
                        model: {
                            root.rev
                            let names = []
                            const list = root.planCourseList()
                            for (const c of list)
                                names.push(c.name)
                            return names
                        }
                        currentIndex: root.courseIndex
                        onActivated: root.courseIndex = index
                    }

                    AppComboBox {
                        id: actTeacherCombo
                        Layout.preferredWidth: 180
                        visible: root.preMode === 1
                        model: {
                            root.rev
                            let names = []
                            for (let i = 0; i < teacherModel.count; ++i)
                                names.push(teacherModel.get(i, "name"))
                            return names
                        }
                        onActivated: root.refresh()
                    }
                    AppComboBox {
                        Layout.preferredWidth: 180
                        visible: root.preMode === 1
                        model: {
                            root.rev
                            let names = []
                            for (let i = 0; i < courseModel.count; ++i)
                                names.push(courseModel.get(i, "name"))
                            return names
                        }
                        currentIndex: root.actCourseIndex
                        onActivated: root.actCourseIndex = index
                    }

                    Item { Layout.fillWidth: true }
                    Text {
                        text: root.preMode === 0
                              ? qsTr("选中课程后点击格子预排；点已预排格取消")
                              : qsTr("选中活动课程后点击格子登记活动；再次点击取消")
                        color: Theme.textDisabled
                        font.pixelSize: Theme.fontCaption
                    }
                    AppButton {
                        text: qsTr("⊞ 批量预排…")
                        visible: root.preMode === 0
                        variant: "primary"
                        onClicked: batchPreassignDialog.openDialog()
                    }
                }

                AppCard {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Item {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingM
                        TimetableGrid {
                            id: preGrid
                            anchors.fill: parent
                            visible: root.preMode === 0 ? root.classForPre() >= 0
                                                        : root.teacherForActivity() >= 0
                            revision: root.rev
                            cellState: function (day, period) {
                                const slot = slotOf(day, period)
                                if (root.preMode === 0)
                                    return root.preMap[slot] !== undefined ? 2 : 0
                                return root.actMap[slot] !== undefined ? 3 : 0
                            }
                            cellText: function (day, period) {
                                const slot = slotOf(day, period)
                                const m = root.preMode === 0 ? root.preMap : root.actMap
                                return m[slot] !== undefined ? m[slot] : ""
                            }
                            onCellClicked: function (day, period) {
                                const slot = slotOf(day, period)
                                if (root.preMode === 0) {
                                    const cid = root.classForPre()
                                    if (cid < 0) return
                                    if (root.preMap[slot] !== undefined) {
                                        app.togglePreAssignment(cid, -1, slot) // 已存在 → 取消
                                    } else {
                                        const list = root.planCourseList()
                                        if (root.courseIndex < list.length)
                                            app.togglePreAssignment(cid, list[root.courseIndex].id, slot)
                                    }
                                } else {
                                    const tid = root.teacherForActivity()
                                    if (tid < 0) return
                                    if (root.actMap[slot] !== undefined) {
                                        app.toggleTeacherActivity(tid, slot, -1)
                                    } else if (root.actCourseIndex < courseModel.count) {
                                        app.toggleTeacherActivity(tid, slot,
                                            courseModel.get(root.actCourseIndex, "id"))
                                    }
                                }
                            }
                        }
                        AppEmptyState {
                            anchors.centerIn: parent
                            visible: root.preMode === 0 ? root.classForPre() < 0
                                                        : root.teacherForActivity() < 0
                            icon: "📌"
                            title: root.preMode === 0 ? qsTr("请先添加班级") : qsTr("请先添加老师")
                            hint: qsTr("在「名单管理」页添加后回到这里")
                        }
                    }
                }
            }
        }
    }

    // 批量固定预排 / 批量不排时间
    BatchPreassignDialog {
        id: batchPreassignDialog
        parent: Overlay.overlay
        onApplied: root.refresh()
    }
    BatchNoScheduleDialog {
        id: batchNoScheduleDialog
        parent: Overlay.overlay
        onApplied: root.refresh()
    }

    // 年级组作息遮罩：把「每周天数 / 每天节数」范围外的槽位批量设为组内班级的不排
    Popup {
        id: groupMaskDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 500

        property var groups: []

        function reload() {
            groups = app.gradeGroups()
            groupCombo.currentIndex = groups.length > 0 ? 0 : -1
            prefill()
        }
        function prefill() {
            const i = groupCombo.currentIndex
            const g = (i >= 0 && i < groups.length) ? groups[i] : null
            if (g && g.maskDays > 0) {
                daysSpin.value = g.maskDays
                periodsSpin.value = g.maskPeriods
            } else {
                daysSpin.value = app.daysPerWeek
                periodsSpin.value = app.periodsPerDay
            }
        }
        function currentGroupId() {
            const i = groupCombo.currentIndex
            return (i >= 0 && i < groups.length) ? groups[i].id : -1
        }
        function hasMask() {
            const i = groupCombo.currentIndex
            return i >= 0 && i < groups.length && groups[i].maskDays > 0
        }

        background: Rectangle {
            radius: Theme.radiusL
            color: Theme.surface
            border.color: Theme.border
        }
        Overlay.modal: Rectangle {
            color: Theme.dark ? Qt.rgba(0, 0, 0, 0.55) : Qt.rgba(15, 23, 42, 0.28)
        }
        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 150 }
            NumberAnimation { property: "scale"; from: 0.92; to: 1; duration: 150; easing.type: Easing.OutCubic }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 110 }
        }
        onOpened: reload()

        contentItem: ColumnLayout {
            spacing: Theme.spacingM

            Text {
                Layout.topMargin: Theme.spacingL
                Layout.leftMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                text: qsTr("按年级组设置作息")
                font.pixelSize: Theme.fontTitle
                font.bold: true
                color: Theme.text
            }
            Text {
                Layout.leftMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                Layout.fillWidth: true
                text: qsTr("全局课表按最大作息设置；这里把组内班级超出本组作息的格子批量设为「不排」。应用后可在课表查看页看到灰格。")
                font.pixelSize: Theme.fontCaption
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
            }

            GridLayout {
                Layout.leftMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Theme.spacingL
                rowSpacing: Theme.spacingM

                Text { text: qsTr("年级组"); color: Theme.text; font.pixelSize: Theme.fontBody }
                AppComboBox {
                    id: groupCombo
                    Layout.fillWidth: true
                    model: {
                        let names = []
                        for (let i = 0; i < groupMaskDialog.groups.length; ++i) {
                            const g = groupMaskDialog.groups[i]
                            const mask = g.maskDays > 0
                                       ? qsTr("，已设 %1 天 × %2 节").arg(g.maskDays).arg(g.maskPeriods)
                                       : ""
                            names.push(qsTr("%1（%2 个班%3）").arg(g.name).arg(g.memberCount).arg(mask))
                        }
                        return names
                    }
                    onActivated: groupMaskDialog.prefill()
                }

                Text { text: qsTr("每周上课天数"); color: Theme.text; font.pixelSize: Theme.fontBody }
                AppStepper {
                    id: daysSpin
                    from: 1; to: app.daysPerWeek; value: app.daysPerWeek
                    suffix: qsTr(" 天")
                }

                Text { text: qsTr("每天节数"); color: Theme.text; font.pixelSize: Theme.fontBody }
                AppStepper {
                    id: periodsSpin
                    from: 1; to: app.periodsPerDay; value: app.periodsPerDay
                    suffix: qsTr(" 节")
                }
            }

            RowLayout {
                Layout.leftMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                Layout.bottomMargin: Theme.spacingL
                Layout.topMargin: Theme.spacingXS
                Layout.fillWidth: true
                spacing: Theme.spacingS

                Text {
                    Layout.fillWidth: true
                    visible: groupMaskDialog.groups.length === 0
                    text: qsTr("还没有年级组，请先到「归类分组」创建")
                    color: Theme.warning
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.WordWrap
                }
                AppButton {
                    text: qsTr("取消")
                    onClicked: groupMaskDialog.close()
                }
                AppButton {
                    text: qsTr("清除越界禁排")
                    enabled: groupMaskDialog.hasMask()
                    onClicked: {
                        app.applyGradeMask(groupMaskDialog.currentGroupId(), daysSpin.value,
                                           periodsSpin.value, false)
                        root.refresh()
                    }
                }
                AppButton {
                    text: qsTr("应用遮罩")
                    variant: "primary"
                    enabled: groupMaskDialog.currentGroupId() >= 0
                    onClicked: {
                        app.applyGradeMask(groupMaskDialog.currentGroupId(), daysSpin.value,
                                           periodsSpin.value, true)
                        root.refresh()
                        groupMaskDialog.close()
                    }
                }
            }
        }
    }
}
