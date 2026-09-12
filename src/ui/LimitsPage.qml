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

        // 提醒横幅
        Rectangle {
            Layout.fillWidth: true
            height: 36
            radius: Theme.radiusM
            color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.12)
            visible: root.mode === 0
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: Theme.spacingM
                text: qsTr("提醒：「不排」设置过多可能导致排不出课表，请只设置确实必要的限制。")
                color: Theme.warning
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
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
}
