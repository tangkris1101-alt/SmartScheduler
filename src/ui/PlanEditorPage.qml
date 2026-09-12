import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 教学计划页：按班级编辑 / 按老师任课 / 合班关联
Item {
    id: root
    property int mode: 0 // 0 按班级 1 按老师 2 合班关联
    property int rev: 0  // 数据版本号：模型变更时递增，驱动委托重算

    // ---- 按班级状态 ----
    property string classFilter: ""
    property int gradeFilterId: -1
    property var courseOptions: []
    property var courseIds: []
    property var roomOptions: []
    property var roomIds: []
    property var gradeOptions: [qsTr("全部年级")]
    property var gradeIds: [-1]

    // ---- 按老师状态 ----
    property string teacherFilter: ""

    // ---- 合班关联状态 ----
    property int selectedGroupRow: -1
    property var unlinked: []
    property string combineError: ""

    function rebuildOptions() {
        // 课程下拉（带教研组前缀）
        let names = [qsTr("（未选课程）")]
        let ids = [-1]
        for (let i = 0; i < courseModel.count; ++i) {
            const gid = courseModel.get(i, "groupId")
            let prefix = ""
            for (let g = 0; g < groupModel.count; ++g) {
                if (groupModel.get(g, "id") === gid && gid >= 0) {
                    prefix = groupModel.get(g, "name") + "·"
                    break
                }
            }
            names.push(prefix + courseModel.get(i, "name"))
            ids.push(courseModel.get(i, "id"))
        }
        root.courseOptions = names
        root.courseIds = ids

        let rooms = [qsTr("本班教室")]
        let rids = [-1]
        for (let i = 0; i < classroomModel.count; ++i) {
            rooms.push(classroomModel.get(i, "name"))
            rids.push(classroomModel.get(i, "id"))
        }
        root.roomOptions = rooms
        root.roomIds = rids

        let grades = [qsTr("全部年级")]
        let gids = [-1]
        for (let g = 0; g < groupModel.count; ++g) {
            if (groupModel.get(g, "type") === 0) {
                grades.push(groupModel.get(g, "name"))
                gids.push(groupModel.get(g, "id"))
            }
        }
        root.gradeOptions = grades
        root.gradeIds = gids
    }

    function refreshUnlinked() {
        root.unlinked = combinedModel.unlinkedEntries()
    }

    Component.onCompleted: { rebuildOptions(); refreshUnlinked() }

    Connections {
        target: app
        function onProjectChanged() { root.rebuildOptions(); root.refreshUnlinked(); root.rev++ }
    }
    Connections {
        target: courseModel
        function onDataChanged() { root.rebuildOptions() }
        function onCountChanged() { root.rebuildOptions() }
    }
    Connections {
        target: groupModel
        function onDataChanged() { root.rebuildOptions() }
        function onCountChanged() { root.rebuildOptions() }
    }
    Connections {
        target: classroomModel
        function onDataChanged() { root.rebuildOptions() }
        function onCountChanged() { root.rebuildOptions() }
    }
    Connections {
        target: planModel
        function onDataChanged() { root.rev++; root.refreshUnlinked() }
        function onCountChanged() { root.rev++; root.refreshUnlinked() }
    }
    Connections {
        target: combinedModel
        function onDataChanged() { root.rev++; root.refreshUnlinked() }
        function onCountChanged() { root.rev++; root.refreshUnlinked() }
    }
    Connections {
        target: dutyModel
        function onCountChanged() { root.rev++ }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingXL
        spacing: Theme.spacingM

        RowLayout {
            Layout.fillWidth: true
            AppTabs {
                model: [qsTr("按班级编辑"), qsTr("按老师任课"), qsTr("合班关联")]
                currentIndex: root.mode
                onTabClicked: function (index) { root.mode = index }
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: root.mode === 0 && planModel.count > 0
                property double total: { root.rev; return planModel.totalWeeklyPeriods }
                property double limit: app.daysPerWeek * app.periodsPerDay
                text: qsTr("该班周课时合计 %1 / 容量 %2 节").arg(total).arg(limit)
                color: total > limit ? Theme.danger : Theme.textSecondary
                font.pixelSize: Theme.fontCaption
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.mode

            // ==================== 按班级编辑 ====================
            RowLayout {
                spacing: Theme.spacingL

                // 左：班级列表
                AppCard {
                    Layout.preferredWidth: 250
                    Layout.fillHeight: true
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingM
                        spacing: Theme.spacingS
                        AppTextField {
                            Layout.fillWidth: true
                            placeholder: qsTr("搜索班级…")
                            onTextChanged: { root.classFilter = text; classModel.filter = text }
                        }
                        AppComboBox {
                            Layout.fillWidth: true
                            model: root.gradeOptions
                            currentIndex: 0
                            onActivated: root.gradeFilterId = root.gradeIds[index]
                        }
                        ListView {
                            id: classList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: classModel
                            spacing: 2
                            delegate: Rectangle {
                                id: clsRow
                                required property int index
                                property bool gradeVisible: root.gradeFilterId < 0
                                    || classModel.get(index, "gradeId") === root.gradeFilterId
                                visible: gradeVisible
                                width: classList.width
                                height: gradeVisible ? 40 : 0
                                radius: Theme.radiusM
                                color: planModel.currentClassId === classModel.get(index, "id")
                                       ? Theme.selectedBg
                                       : (clsHover.containsMouse ? Theme.overlayHover : "transparent")
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: Theme.spacingM
                                    text: classModel.get(clsRow.index, "name")
                                    color: Theme.text
                                    font.pixelSize: Theme.fontBody
                                }
                                MouseArea {
                                    id: clsHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: planModel.currentClassId = classModel.get(clsRow.index, "id")
                                }
                            }
                            AppEmptyState {
                                anchors.centerIn: parent
                                visible: classModel.count === 0
                                icon: "🏫"
                                title: qsTr("还没有班级")
                                hint: qsTr("请先在「名单管理」页添加")
                            }
                        }
                    }
                }

                // 右：计划条目表
                AppCard {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingM
                        spacing: Theme.spacingS
                        visible: planModel.currentClassId >= 0

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingS
                            AppButton {
                                text: qsTr("✚ 添加课程行")
                                variant: "primary"
                                onClicked: planModel.addEntry(-1)
                            }
                            AppButton {
                                text: qsTr("📑 复制到班级…")
                                enabled: planModel.count > 0
                                onClicked: copyDialog.open()
                            }
                            Item { Layout.fillWidth: true }
                        }

                        // 表格（横向可滚动）
                        Flickable {
                            id: tableFlick
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            contentWidth: 1250
                            contentHeight: planColumn.implicitHeight
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds

                            readonly property var colWidths: [170, 160, 104, 86, 86, 96, 52, 130, 84, 96, 120, 48]
                            readonly property var colLabels: [qsTr("课程"), qsTr("任课老师"), qsTr("周节数"),
                                qsTr("连课次数"), qsTr("连课节数"), qsTr("上午节数"), qsTr("合班"), qsTr("教室"),
                                qsTr("开课周"), qsTr("结束周"), qsTr("同步课"), ""]

                            Column {
                                id: planColumn
                                width: 1250

                                // 表头
                                Row {
                                    height: 36
                                    Repeater {
                                        model: tableFlick.colLabels.length
                                        Text {
                                            width: tableFlick.colWidths[index]
                                            height: 36
                                            verticalAlignment: Text.AlignVCenter
                                            leftPadding: Theme.spacingS
                                            text: tableFlick.colLabels[index]
                                            color: Theme.textSecondary
                                            font.pixelSize: Theme.fontCaption
                                            font.bold: true
                                        }
                                    }
                                }
                                Rectangle { width: parent.width; height: 1; color: Theme.border }

                                ListView {
                                    id: planTable
                                    width: parent.width
                                    height: contentHeight
                                    interactive: false
                                    model: planModel

                                    delegate: Rectangle {
                                        id: planRow
                                        required property int index
                                        width: 1250
                                        height: 50
                                        color: planRow.index % 2 ? Theme.surfaceAlt : "transparent"
                                        property bool duplicate: { root.rev; return planModel.get(index, "duplicate") }
                                        property bool teacherWarn: { root.rev; return planModel.get(index, "teacherWarn") }

                                        Rectangle {
                                            visible: planRow.duplicate
                                            anchors.left: parent.left
                                            anchors.top: parent.top
                                            anchors.bottom: parent.bottom
                                            width: 3
                                            color: Theme.danger
                                        }

                                        Row {
                                            anchors.fill: parent

                                            // 课程
                                            Item {
                                                width: tableFlick.colWidths[0]; height: 50
                                                AppComboBox {
                                                    anchors.fill: parent; anchors.margins: 4
                                                    model: root.courseOptions
                                                    currentIndex: {
                                                        root.rev
                                                        const cid = planModel.get(planRow.index, "courseId")
                                                        const i = root.courseIds.indexOf(cid)
                                                        return i < 0 ? 0 : i
                                                    }
                                                    onActivated: planModel.set(planRow.index, "courseId", root.courseIds[index])
                                                }
                                                Text {
                                                    visible: planRow.duplicate
                                                    anchors.right: parent.right; anchors.top: parent.top
                                                    anchors.margins: 4
                                                    text: "⚠ 重复"
                                                    color: Theme.danger
                                                    font.pixelSize: 10
                                                }
                                            }
                                            // 任课老师（多选弹层）
                                            Item {
                                                width: tableFlick.colWidths[1]; height: 50
                                                AppButton {
                                                    anchors.fill: parent; anchors.margins: 4
                                                    variant: "default"
                                                    text: {
                                                        root.rev
                                                        const n = planModel.get(planRow.index, "teacherNames")
                                                        return n.length > 0 ? n : qsTr("选择老师…")
                                                    }
                                                    onClicked: teacherPopup.open()
                                                }
                                                Text {
                                                    visible: planRow.teacherWarn
                                                    anchors.right: parent.right; anchors.top: parent.top
                                                    anchors.margins: 4
                                                    text: "⚠ 组外"
                                                    color: Theme.warning
                                                    font.pixelSize: 10
                                                }
                                                Popup {
                                                    id: teacherPopup
                                                    y: parent.height + 4
                                                    width: 220
                                                    height: Math.min(tpList.contentHeight + 8, 280)
                                                    padding: 4
                                                    background: Rectangle {
                                                        radius: Theme.radiusM; color: Theme.surface
                                                        border.color: Theme.border
                                                    }
                                                    contentItem: ListView {
                                                        id: tpList
                                                        clip: true
                                                        model: teacherModel
                                                        delegate: Rectangle {
                                                            id: tpRow
                                                            required property int index
                                                            width: tpList.width
                                                            height: 34
                                                            radius: Theme.radiusS
                                                            color: tpHover.containsMouse ? Theme.overlayHover : "transparent"
                                                            RowLayout {
                                                                anchors.fill: parent
                                                                anchors.leftMargin: Theme.spacingS
                                                                anchors.rightMargin: Theme.spacingS
                                                                Text {
                                                                    Layout.fillWidth: true
                                                                    text: teacherModel.get(tpRow.index, "name")
                                                                    color: Theme.text
                                                                    font.pixelSize: Theme.fontBody
                                                                }
                                                                AppSwitch {
                                                                    checked: {
                                                                        root.rev
                                                                        return planModel.get(planRow.index, "teacherIds").indexOf(
                                                                            teacherModel.get(tpRow.index, "id")) >= 0
                                                                    }
                                                                    onToggled: planModel.toggleTeacher(planRow.index, teacherModel.get(tpRow.index, "id"))
                                                                }
                                                            }
                                                            MouseArea {
                                                                id: tpHover
                                                                anchors.fill: parent
                                                                hoverEnabled: true
                                                                propagateComposedEvents: true
                                                                onClicked: function (m) { m.accepted = false }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                            // 周节数
                                            Item {
                                                width: tableFlick.colWidths[2]; height: 50
                                                AppStepper {
                                                    anchors.centerIn: parent
                                                    stepSize: 0.5; from: 0.5; to: 15
                                                    value: { root.rev; return planModel.get(planRow.index, "weeklyPeriods") }
                                                    onValueModified: planModel.set(planRow.index, "weeklyPeriods", value)
                                                }
                                            }
                                            // 连课次数
                                            Item {
                                                width: tableFlick.colWidths[3]; height: 50
                                                AppStepper {
                                                    anchors.centerIn: parent
                                                    from: 0; to: 4
                                                    value: { root.rev; return planModel.get(planRow.index, "chainedCount") }
                                                    onValueModified: planModel.set(planRow.index, "chainedCount", value)
                                                }
                                            }
                                            // 连课节数
                                            Item {
                                                width: tableFlick.colWidths[4]; height: 50
                                                AppStepper {
                                                    anchors.centerIn: parent
                                                    from: 2; to: 4
                                                    enabled: planModel.get(planRow.index, "chainedCount") > 0
                                                    opacity: enabled ? 1 : 0.35
                                                    value: { root.rev; return planModel.get(planRow.index, "chainLength") }
                                                    onValueModified: planModel.set(planRow.index, "chainLength", value)
                                                }
                                            }
                                            // 上午节数
                                            Item {
                                                width: tableFlick.colWidths[5]; height: 50
                                                AppComboBox {
                                                    anchors.fill: parent; anchors.margins: 4
                                                    model: {
                                                        let m = [qsTr("自动")]
                                                        for (let i = 1; i <= app.periodsPerDay; ++i)
                                                            m.push(qsTr("%1 节").arg(i))
                                                        return m
                                                    }
                                                    currentIndex: {
                                                        root.rev
                                                        return planModel.get(planRow.index, "morningPeriods") + 1
                                                    }
                                                    onActivated: planModel.set(planRow.index, "morningPeriods", index - 1)
                                                }
                                            }
                                            // 合班
                                            Item {
                                                width: tableFlick.colWidths[6]; height: 50
                                                AppSwitch {
                                                    anchors.centerIn: parent
                                                    checked: { root.rev; return planModel.get(planRow.index, "isCombined") }
                                                    onToggled: planModel.set(planRow.index, "isCombined", checked)
                                                }
                                            }
                                            // 教室
                                            Item {
                                                width: tableFlick.colWidths[7]; height: 50
                                                AppComboBox {
                                                    anchors.fill: parent; anchors.margins: 4
                                                    model: root.roomOptions
                                                    currentIndex: {
                                                        root.rev
                                                        const rid = planModel.get(planRow.index, "classroomId")
                                                        const i = root.roomIds.indexOf(rid)
                                                        return i < 0 ? 0 : i
                                                    }
                                                    onActivated: planModel.set(planRow.index, "classroomId", root.roomIds[index])
                                                }
                                            }
                                            // 开课周
                                            Item {
                                                width: tableFlick.colWidths[8]; height: 50
                                                AppStepper {
                                                    anchors.centerIn: parent
                                                    from: 1; to: app.weeksPerTerm
                                                    value: { root.rev; return planModel.get(planRow.index, "startWeek") }
                                                    onValueModified: planModel.set(planRow.index, "startWeek", value)
                                                }
                                            }
                                            // 结束周
                                            Item {
                                                width: tableFlick.colWidths[9]; height: 50
                                                AppComboBox {
                                                    anchors.fill: parent; anchors.margins: 4
                                                    model: {
                                                        let m = [qsTr("学期末")]
                                                        for (let i = 1; i <= app.weeksPerTerm; ++i)
                                                            m.push(qsTr("第%1周").arg(i))
                                                        return m
                                                    }
                                                    currentIndex: {
                                                        root.rev
                                                        return planModel.get(planRow.index, "endWeek")
                                                    }
                                                    onActivated: planModel.set(planRow.index, "endWeek", index)
                                                }
                                            }
                                            // 同步课
                                            Item {
                                                width: tableFlick.colWidths[10]; height: 50
                                                AppComboBox {
                                                    anchors.fill: parent; anchors.margins: 4
                                                    model: {
                                                        let m = [qsTr("无")]
                                                        for (let i = 1; i < root.courseOptions.length; ++i)
                                                            m.push(root.courseOptions[i])
                                                        return m
                                                    }
                                                    currentIndex: {
                                                        root.rev
                                                        const sid = planModel.get(planRow.index, "syncCourseId")
                                                        const i = root.courseIds.indexOf(sid)
                                                        return i < 0 ? 0 : i
                                                    }
                                                    onActivated: planModel.set(planRow.index, "syncCourseId",
                                                                               index === 0 ? -1 : root.courseIds[index])
                                                }
                                            }
                                            // 删除
                                            Item {
                                                width: tableFlick.colWidths[11]; height: 50
                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "✕"
                                                    color: delHover.containsMouse ? Theme.danger : Theme.textDisabled
                                                    MouseArea {
                                                        id: delHover
                                                        anchors.fill: parent
                                                        anchors.margins: -8
                                                        hoverEnabled: true
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: planModel.removeRow(planRow.index)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        AppEmptyState {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: planModel.count === 0
                            icon: "📅"
                            title: qsTr("该班还没有教学计划")
                            hint: qsTr("点击「添加课程行」开始，或使用「复制到班级」从其他班复制")
                        }
                    }

                    AppEmptyState {
                        anchors.centerIn: parent
                        visible: planModel.currentClassId < 0
                        icon: "👈"
                        title: qsTr("选择一个班级")
                        hint: qsTr("从左侧班级列表开始编辑教学计划")
                    }
                }
            }

            // ==================== 按老师任课 ====================
            RowLayout {
                spacing: Theme.spacingL

                AppCard {
                    Layout.preferredWidth: 250
                    Layout.fillHeight: true
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingM
                        spacing: Theme.spacingS
                        AppTextField {
                            Layout.fillWidth: true
                            placeholder: qsTr("搜索老师…")
                            onTextChanged: teacherModel.filter = text
                        }
                        ListView {
                            id: teacherList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: teacherModel
                            spacing: 2
                            delegate: Rectangle {
                                id: tRow
                                required property int index
                                width: teacherList.width
                                height: 40
                                radius: Theme.radiusM
                                color: dutyModel.currentTeacherId === teacherModel.get(index, "id")
                                       ? Theme.selectedBg
                                       : (tHover.containsMouse ? Theme.overlayHover : "transparent")
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: Theme.spacingM
                                    text: teacherModel.get(tRow.index, "name")
                                    color: Theme.text
                                    font.pixelSize: Theme.fontBody
                                }
                                MouseArea {
                                    id: tHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: dutyModel.currentTeacherId = teacherModel.get(tRow.index, "id")
                                }
                            }
                            AppEmptyState {
                                anchors.centerIn: parent
                                visible: teacherModel.count === 0
                                icon: "👩‍🏫"
                                title: qsTr("还没有老师")
                            }
                        }
                    }
                }

                AppCard {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingL
                        spacing: Theme.spacingM
                        visible: dutyModel.currentTeacherId >= 0

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: qsTr("任教条目")
                                font.pixelSize: Theme.fontSubtitle
                                font.bold: true
                                color: Theme.text
                            }
                            Item { Layout.fillWidth: true }
                            AppButton {
                                text: qsTr("✚ 设置任教…")
                                variant: "primary"
                                onClicked: dutyDialog.open()
                            }
                        }

                        ListView {
                            id: dutyList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: dutyModel
                            spacing: 4

                            readonly property var groupColors: ["#ef4444", "#f59e0b", "#22c55e", "#3b82f6",
                                                                "#a855f7", "#ec4899", "#14b8a6", "#f97316"]

                            delegate: Rectangle {
                                id: dRow
                                required property int index
                                width: dutyList.width
                                height: 48
                                radius: Theme.radiusM
                                color: dRow.index % 2 ? Theme.surfaceAlt : "transparent"

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.spacingM
                                    anchors.rightMargin: Theme.spacingM
                                    spacing: Theme.spacingL

                                    Text {
                                        Layout.preferredWidth: 110
                                        text: dutyModel.get(dRow.index, "className")
                                        color: Theme.text
                                        font.pixelSize: Theme.fontBody
                                        font.bold: true
                                    }
                                    Text {
                                        Layout.preferredWidth: 110
                                        text: dutyModel.get(dRow.index, "courseName")
                                        color: Theme.text
                                        font.pixelSize: Theme.fontBody
                                    }
                                    RowLayout {
                                        Text {
                                            text: qsTr("周节数")
                                            color: Theme.textSecondary
                                            font.pixelSize: Theme.fontCaption
                                        }
                                        AppStepper {
                                            stepSize: 0.5; from: 0.5; to: 15
                                            value: { root.rev; return dutyModel.get(dRow.index, "weeklyPeriods") }
                                            onValueModified: dutyModel.set(dRow.index, "weeklyPeriods", value)
                                        }
                                    }
                                    Item { Layout.fillWidth: true }
                                    Rectangle {
                                        width: 10; height: 10; radius: 5
                                        visible: dutyModel.get(dRow.index, "progressGroup") >= 0
                                        color: dutyList.groupColors[
                                            dutyModel.get(dRow.index, "progressGroup") % dutyList.groupColors.length]
                                    }
                                    AppComboBox {
                                        Layout.preferredWidth: 120
                                        model: [qsTr("未分组"), qsTr("进度组 1"), qsTr("进度组 2"), qsTr("进度组 3"),
                                                qsTr("进度组 4"), qsTr("进度组 5"), qsTr("进度组 6"), qsTr("进度组 7"), qsTr("进度组 8")]
                                        currentIndex: {
                                            root.rev
                                            return dutyModel.get(dRow.index, "progressGroup") + 1
                                        }
                                        onActivated: dutyModel.set(dRow.index, "progressGroup", index - 1)
                                    }
                                    AppButton {
                                        text: qsTr("解除任教")
                                        variant: "subtle"
                                        onClicked: dutyModel.removeRow(dRow.index)
                                    }
                                }
                            }

                            AppEmptyState {
                                anchors.centerIn: parent
                                visible: dutyModel.count === 0
                                icon: "📭"
                                title: qsTr("该老师暂无任教条目")
                                hint: qsTr("点击「设置任教」批量指定班级与课程")
                            }
                        }
                    }

                    AppEmptyState {
                        anchors.centerIn: parent
                        visible: dutyModel.currentTeacherId < 0
                        icon: "👈"
                        title: qsTr("选择一位老师")
                        hint: qsTr("从左侧老师列表查看与设置任教")
                    }
                }
            }

            // ==================== 合班关联 ====================
            RowLayout {
                spacing: Theme.spacingL

                // 左：待关联的合班课条目
                AppCard {
                    Layout.preferredWidth: 360
                    Layout.fillHeight: true
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingL
                        spacing: Theme.spacingM

                        Text {
                            text: qsTr("待关联的合班课")
                            font.pixelSize: Theme.fontSubtitle
                            font.bold: true
                            color: Theme.text
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("这些是教学计划中标记为「合班」、尚未加入合班组的条目。先在右侧选中一个组，再点「选入」。")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.WordWrap
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 34
                            radius: Theme.radiusS
                            visible: root.combineError.length > 0
                            color: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.12)
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.margins: Theme.spacingS
                                text: root.combineError
                                color: Theme.danger
                                font.pixelSize: Theme.fontCaption
                                elide: Text.ElideRight
                            }
                            Timer {
                                interval: 4000
                                running: root.combineError.length > 0
                                onTriggered: root.combineError = ""
                            }
                        }

                        ListView {
                            id: unlinkedList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: root.unlinked
                            spacing: 4
                            delegate: Rectangle {
                                id: uRow
                                required property var modelData
                                width: unlinkedList.width
                                height: 44
                                radius: Theme.radiusM
                                color: uHover.containsMouse ? Theme.overlayHover : "transparent"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.spacingM
                                    anchors.rightMargin: Theme.spacingM
                                    Text {
                                        Layout.fillWidth: true
                                        text: uRow.modelData.text
                                        color: Theme.text
                                        font.pixelSize: Theme.fontBody
                                        elide: Text.ElideRight
                                    }
                                    AppButton {
                                        text: qsTr("选入")
                                        enabled: root.selectedGroupRow >= 0
                                        onClicked: {
                                            const gid = combinedModel.idAt(root.selectedGroupRow)
                                            const err = combinedModel.addEntry(gid, uRow.modelData.entryId)
                                            root.combineError = err
                                            root.refreshUnlinked()
                                        }
                                    }
                                }
                                MouseArea {
                                    id: uHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    propagateComposedEvents: true
                                    onClicked: function (m) { m.accepted = false }
                                }
                            }
                            AppEmptyState {
                                anchors.centerIn: parent
                                visible: root.unlinked.length === 0
                                icon: "✅"
                                title: qsTr("没有待关联的合班课")
                                hint: qsTr("在「按班级编辑」中把条目标记为合班")
                            }
                        }
                    }
                }

                // 右：合班组卡片
                AppCard {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingL
                        spacing: Theme.spacingM

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: qsTr("合班组")
                                font.pixelSize: Theme.fontSubtitle
                                font.bold: true
                                color: Theme.text
                            }
                            Item { Layout.fillWidth: true }
                            AppButton {
                                text: qsTr("✚ 新建合班组")
                                variant: "primary"
                                enabled: app.hasProject
                                onClicked: {
                                    combinedModel.addGroup(qsTr("新合班组"))
                                    root.selectedGroupRow = combinedModel.count - 1
                                }
                            }
                        }

                        ListView {
                            id: combinedList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: combinedModel
                            spacing: Theme.spacingM

                            delegate: Rectangle {
                                id: cgCard
                                required property int index
                                width: combinedList.width
                                height: cgColumn.implicitHeight + Theme.spacingL * 2
                                radius: Theme.radiusL
                                color: root.selectedGroupRow === index ? Theme.selectedBg : Theme.surfaceAlt
                                border.color: root.selectedGroupRow === index ? Theme.primary : "transparent"
                                Behavior on color { ColorAnimation { duration: 100 } }

                                ColumnLayout {
                                    id: cgColumn
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: Theme.spacingL
                                    spacing: Theme.spacingS

                                    RowLayout {
                                        Layout.fillWidth: true
                                        Text {
                                            text: combinedModel.get(cgCard.index, "name")
                                            font.pixelSize: Theme.fontBody
                                            font.bold: true
                                            color: Theme.text
                                        }
                                        Text {
                                            text: combinedModel.get(cgCard.index, "courseName")
                                            color: Theme.primary
                                            font.pixelSize: Theme.fontCaption
                                        }
                                        Text {
                                            visible: !combinedModel.get(cgCard.index, "valid")
                                            text: qsTr("⚠ 校验未通过（课程需一致且老师有交集）")
                                            color: Theme.warning
                                            font.pixelSize: Theme.fontCaption
                                        }
                                        Item { Layout.fillWidth: true }
                                        AppButton {
                                            text: qsTr("解散")
                                            variant: "subtle"
                                            onClicked: {
                                                combinedModel.removeRow(cgCard.index)
                                                root.selectedGroupRow = -1
                                                root.refreshUnlinked()
                                            }
                                        }
                                    }
                                    Text {
                                        visible: text.length > 0
                                        text: qsTr("共同任课：") + combinedModel.get(cgCard.index, "teacherNames")
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontCaption
                                    }
                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: Theme.spacingS
                                        Repeater {
                                            model: combinedModel.get(cgCard.index, "memberList")
                                            delegate: Rectangle {
                                                required property var modelData
                                                width: memberLabel.implicitWidth + 44
                                                height: 28
                                                radius: Theme.radiusS
                                                color: Theme.surface
                                                border.color: Theme.border
                                                Text {
                                                    id: memberLabel
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    anchors.left: parent.left
                                                    anchors.leftMargin: Theme.spacingS
                                                    text: modelData.label
                                                    color: Theme.text
                                                    font.pixelSize: Theme.fontCaption
                                                }
                                                Text {
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    anchors.right: parent.right
                                                    anchors.rightMargin: Theme.spacingS
                                                    text: "✕"
                                                    color: memberDelHover.containsMouse ? Theme.danger : Theme.textDisabled
                                                    font.pixelSize: 10
                                                    MouseArea {
                                                        id: memberDelHover
                                                        anchors.fill: parent
                                                        anchors.margins: -4
                                                        hoverEnabled: true
                                                        cursorShape: Qt.PointingHandCursor
                                                        onClicked: {
                                                            combinedModel.removeEntry(
                                                                combinedModel.idAt(cgCard.index),
                                                                modelData.classId, modelData.courseId)
                                                            root.refreshUnlinked()
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    propagateComposedEvents: true
                                    onClicked: function (m) {
                                        root.selectedGroupRow = cgCard.index
                                        m.accepted = false
                                    }
                                }
                            }

                            AppEmptyState {
                                anchors.centerIn: parent
                                visible: combinedModel.count === 0
                                icon: "🏷️"
                                title: qsTr("还没有合班组")
                                hint: qsTr("新建合班组后，把左侧待关联条目选入")
                            }
                        }
                    }
                }
            }
        }
    }

    // 复制到班级对话框
    Popup {
        id: copyDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 380
        padding: 0
        background: Rectangle { radius: Theme.radiusL; color: Theme.surface; border.color: Theme.border }
        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 }
            NumberAnimation { property: "scale"; from: 0.92; to: 1; duration: 140; easing.type: Easing.OutCubic }
        }
        Overlay.modal: Rectangle { color: Theme.dark ? Qt.rgba(0,0,0,0.55) : Qt.rgba(15,23,42,0.28) }

        property var checkedClasses: ({})
        property string resultText: ""

        contentItem: ColumnLayout {
            spacing: 0
            Text {
                Layout.topMargin: Theme.spacingL; Layout.leftMargin: Theme.spacingXL
                text: qsTr("复制到班级")
                font.pixelSize: Theme.fontSubtitle; font.bold: true; color: Theme.text
            }
            Text {
                Layout.topMargin: Theme.spacingXS; Layout.leftMargin: Theme.spacingXL
                Layout.bottomMargin: Theme.spacingM
                text: qsTr("把当前班的全部计划条目复制到勾选班级")
                font.pixelSize: Theme.fontCaption; color: Theme.textSecondary
            }
            ListView {
                Layout.leftMargin: Theme.spacingXL; Layout.rightMargin: Theme.spacingXL
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                clip: true
                model: classModel
                spacing: 2
                delegate: Rectangle {
                    id: cpRow
                    required property int index
                    property bool isCurrent: classModel.get(index, "id") === planModel.currentClassId
                    visible: !isCurrent
                    width: ListView.view.width
                    height: visible ? 36 : 0
                    radius: Theme.radiusS
                    color: cpHover.containsMouse ? Theme.overlayHover : "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingS; anchors.rightMargin: Theme.spacingS
                        Text {
                            Layout.fillWidth: true
                            text: classModel.get(cpRow.index, "name")
                            color: Theme.text; font.pixelSize: Theme.fontBody
                        }
                        AppSwitch {
                            checked: copyDialog.checkedClasses[classModel.get(cpRow.index, "id")] === true
                            onToggled: {
                                const m = copyDialog.checkedClasses
                                m[classModel.get(cpRow.index, "id")] = checked
                                copyDialog.checkedClasses = m
                            }
                        }
                    }
                    MouseArea {
                        id: cpHover; anchors.fill: parent; hoverEnabled: true
                        propagateComposedEvents: true
                        onClicked: function (m) { m.accepted = false }
                    }
                }
            }
            RowLayout {
                Layout.leftMargin: Theme.spacingXL; Layout.rightMargin: Theme.spacingXL
                Layout.topMargin: Theme.spacingM
                Text { text: qsTr("同课已存在时覆盖"); color: Theme.text; font.pixelSize: Theme.fontBody }
                AppSwitch { id: overwriteSwitch }
                Item { Layout.fillWidth: true }
            }
            Text {
                Layout.leftMargin: Theme.spacingXL
                visible: copyDialog.resultText.length > 0
                text: copyDialog.resultText
                color: Theme.success; font.pixelSize: Theme.fontCaption
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingL; Layout.bottomMargin: Theme.spacingL
                Layout.leftMargin: Theme.spacingXL; Layout.rightMargin: Theme.spacingXL
                spacing: Theme.spacingS
                Item { Layout.fillWidth: true }
                AppButton { text: qsTr("关闭"); onClicked: copyDialog.close() }
                AppButton {
                    text: qsTr("开始复制")
                    variant: "primary"
                    onClicked: {
                        let total = 0
                        for (const key of Object.keys(copyDialog.checkedClasses)) {
                            if (copyDialog.checkedClasses[key] === true)
                                total += planModel.copyToClass(Number(key), overwriteSwitch.checked)
                        }
                        copyDialog.resultText = qsTr("已复制 %1 条").arg(total)
                    }
                }
            }
        }
    }

    // 设置任教对话框
    Popup {
        id: dutyDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 400
        padding: 0
        background: Rectangle { radius: Theme.radiusL; color: Theme.surface; border.color: Theme.border }
        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 }
            NumberAnimation { property: "scale"; from: 0.92; to: 1; duration: 140; easing.type: Easing.OutCubic }
        }
        Overlay.modal: Rectangle { color: Theme.dark ? Qt.rgba(0,0,0,0.55) : Qt.rgba(15,23,42,0.28) }

        property var checkedClasses: ({})

        contentItem: ColumnLayout {
            spacing: Theme.spacingM
            Text {
                Layout.topMargin: Theme.spacingL; Layout.leftMargin: Theme.spacingXL
                text: qsTr("设置任教")
                font.pixelSize: Theme.fontSubtitle; font.bold: true; color: Theme.text
            }
            RowLayout {
                Layout.leftMargin: Theme.spacingXL; Layout.rightMargin: Theme.spacingXL
                Layout.fillWidth: true
                Text { text: qsTr("课程"); color: Theme.text; font.pixelSize: Theme.fontBody }
                AppComboBox {
                    id: dutyCourseCombo
                    Layout.fillWidth: true
                    model: root.courseOptions.slice(1) // 去掉"未选课程"
                }
            }
            RowLayout {
                Layout.leftMargin: Theme.spacingXL; Layout.rightMargin: Theme.spacingXL
                Text { text: qsTr("周节数"); color: Theme.text; font.pixelSize: Theme.fontBody }
                AppStepper { id: dutyPeriods; stepSize: 0.5; from: 0.5; to: 15; value: 4 }
                Item { Layout.fillWidth: true }
            }
            Text {
                Layout.leftMargin: Theme.spacingXL
                text: qsTr("勾选班级（已存在条目将加入该老师）")
                color: Theme.textSecondary; font.pixelSize: Theme.fontCaption
            }
            ListView {
                Layout.leftMargin: Theme.spacingXL; Layout.rightMargin: Theme.spacingXL
                Layout.fillWidth: true
                Layout.preferredHeight: 180
                clip: true
                model: classModel
                spacing: 2
                delegate: Rectangle {
                    id: dcRow
                    required property int index
                    width: ListView.view.width
                    height: 34
                    radius: Theme.radiusS
                    color: dcHover.containsMouse ? Theme.overlayHover : "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacingS; anchors.rightMargin: Theme.spacingS
                        Text {
                            Layout.fillWidth: true
                            text: classModel.get(dcRow.index, "name")
                            color: Theme.text; font.pixelSize: Theme.fontBody
                        }
                        AppSwitch {
                            checked: dutyDialog.checkedClasses[classModel.get(dcRow.index, "id")] === true
                            onToggled: {
                                const m = dutyDialog.checkedClasses
                                m[classModel.get(dcRow.index, "id")] = checked
                                dutyDialog.checkedClasses = m
                            }
                        }
                    }
                    MouseArea {
                        id: dcHover; anchors.fill: parent; hoverEnabled: true
                        propagateComposedEvents: true
                        onClicked: function (m) { m.accepted = false }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.bottomMargin: Theme.spacingL
                Layout.leftMargin: Theme.spacingXL; Layout.rightMargin: Theme.spacingXL
                spacing: Theme.spacingS
                Item { Layout.fillWidth: true }
                AppButton { text: qsTr("取消"); onClicked: dutyDialog.close() }
                AppButton {
                    text: qsTr("确定")
                    variant: "primary"
                    onClicked: {
                        let classIds = []
                        for (const key of Object.keys(dutyDialog.checkedClasses)) {
                            if (dutyDialog.checkedClasses[key] === true)
                                classIds.push(Number(key))
                        }
                        if (classIds.length > 0) {
                            dutyModel.setDuty(root.courseIds[dutyCourseCombo.currentIndex + 1],
                                              classIds, dutyPeriods.value)
                        }
                        dutyDialog.checkedClasses = ({})
                        dutyDialog.close()
                    }
                }
            }
        }
    }
}
