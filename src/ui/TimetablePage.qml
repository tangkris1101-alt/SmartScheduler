import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// 课表查看页：总课表 / 班级 / 老师 / 教室 四视图 + 班级调整模式（拖拽/点选对调）+ 临时调课
Item {
    id: root
    property int viewMode: 0     // 0 总课表 1 班级 2 老师 3 教室
    property int entityIndex: 0
    property int rev: 0
    property var highlightCourse: -1 // 点格高亮同课分布；-1 无

    // 调整模式状态
    property bool adjustMode: false
    property int selectedSlot: -1   // 点选中的课程格
    property int selectedHalf: 0
    property var armPlace: null     // 未排课落子模式：{courseId, name, half}
    property var targets: []        // 当前目标表（targetsFor 结果）
    property string reasonText: ""

    readonly property var viewNames: [qsTr("总课表"), qsTr("班级"), qsTr("老师"), qsTr("教室")]

    function currentEntityModel() {
        return root.viewMode === 1 ? classModel : root.viewMode === 2 ? teacherModel : classroomModel
    }
    function currentEntityId() {
        const m = currentEntityModel()
        if (root.entityIndex < 0 || root.entityIndex >= m.count)
            return -1
        return m.get(root.entityIndex, "id")
    }
    function entityRow() {
        const id = currentEntityId()
        if (id < 0)
            return []
        if (root.viewMode === 1) return app.classRow(id)
        if (root.viewMode === 2) return app.teacherRow(id)
        return app.roomRow(id)
    }
    function refreshTargets() {
        if (root.armPlace) {
            root.targets = adjust.placeTargetsFor(root.currentEntityId(), root.armPlace.courseId,
                                                  root.armPlace.half)
        } else if (root.selectedSlot >= 0) {
            root.targets = adjust.targetsFor(root.currentEntityId(), root.selectedSlot,
                                             root.selectedHalf)
        } else {
            root.targets = []
        }
        root.rev++
    }
    function showReason(text) {
        root.reasonText = text
        shakeAnim.restart()
        reasonTimer.restart()
    }
    function clearSelection() {
        root.selectedSlot = -1
        root.armPlace = null
        root.targets = []
        root.rev++
    }

    onVisibleChanged: if (visible) root.rev++
    Connections {
        target: app
        function onScheduleChanged() { root.rev++; root.refreshTargets() }
        function onProjectChanged() {
            root.rev++
            root.entityIndex = 0
            root.highlightCourse = -1
            root.clearSelection()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingXL
        spacing: Theme.spacingM

        // 临时调课横幅
        Rectangle {
            Layout.fillWidth: true
            height: 40
            radius: Theme.radiusM
            visible: adjust.temporaryMode
            color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.14)
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingM
                anchors.rightMargin: Theme.spacingM
                Text {
                    text: qsTr("⏱ 临时调课进行中：所有改动退出即丢弃，不影响已保存课表")
                    color: Theme.warning
                    font.pixelSize: Theme.fontBody
                }
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("导出调课通知…")
                    variant: "default"
                    onClicked: noticeDialog.open()
                }
                AppButton {
                    text: qsTr("退出临时调课")
                    variant: "danger"
                    onClicked: {
                        adjust.exitTemporary()
                        root.adjustMode = false
                        root.clearSelection()
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingM

            AppTabs {
                model: root.viewNames
                currentIndex: root.viewMode
                onTabClicked: function (index) {
                    root.viewMode = index
                    root.entityIndex = 0
                    root.clearSelection()
                    root.rev++
                }
            }
            AppComboBox {
                Layout.preferredWidth: 180
                visible: root.viewMode > 0
                model: {
                    root.rev
                    let names = []
                    const m = root.currentEntityModel()
                    for (let i = 0; i < m.count; ++i)
                        names.push(m.get(i, "name"))
                    return names
                }
                currentIndex: root.entityIndex
                onActivated: {
                    root.entityIndex = index
                    root.clearSelection()
                    root.rev++
                }
            }

            // 撤销/重做
            AppButton {
                visible: root.adjustMode
                text: "↶"
                enabled: adjust.canUndo
                onClicked: adjust.undo()
            }
            AppButton {
                visible: root.adjustMode
                text: "↷"
                enabled: adjust.canRedo
                onClicked: adjust.redo()
            }

            AppButton {
                visible: root.viewMode === 1 && app.hasSchedule && !adjust.temporaryMode
                text: root.adjustMode ? qsTr("✔ 完成调整") : qsTr("✏️ 调整模式")
                variant: root.adjustMode ? "primary" : "default"
                onClicked: {
                    root.adjustMode = !root.adjustMode
                    root.clearSelection()
                }
            }
            AppButton {
                visible: root.viewMode === 1 && app.hasSchedule && !adjust.temporaryMode
                text: qsTr("⏱ 临时调课")
                variant: "default"
                onClicked: {
                    adjust.enterTemporary()
                    root.adjustMode = true
                }
            }

            Item { Layout.fillWidth: true }
            Text {
                visible: root.highlightCourse >= 0
                text: qsTr("高亮分布中，再点一次课程格取消")
                color: Theme.primary
                font.pixelSize: Theme.fontCaption
            }
        }

        // 原因提示气泡 + 抖动
        Rectangle {
            Layout.fillWidth: true
            height: 34
            radius: Theme.radiusS
            visible: root.reasonText.length > 0
            color: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.12)
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingM
                text: root.reasonText
                color: Theme.danger
                font.pixelSize: Theme.fontCaption
            }
            Timer {
                id: reasonTimer
                interval: 2600
                onTriggered: root.reasonText = ""
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingL

            AppCard {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Item {
                    id: gridWrap
                    anchors.fill: parent
                    anchors.margins: Theme.spacingM

                    SequentialAnimation {
                        id: shakeAnim
                        NumberAnimation { target: gridWrap; property: "x"; from: 0; to: -9; duration: 55 }
                        NumberAnimation { target: gridWrap; property: "x"; from: -9; to: 8; duration: 55 }
                        NumberAnimation { target: gridWrap; property: "x"; from: 8; to: -4; duration: 55 }
                        NumberAnimation { target: gridWrap; property: "x"; from: -4; to: 0; duration: 55 }
                    }

                    // ---- 总课表 ----
                    Flickable {
                        id: masterFlick
                        anchors.fill: parent
                        visible: root.viewMode === 0 && app.hasSchedule
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        contentWidth: 110 + app.daysPerWeek * app.periodsPerDay * cellW
                        contentHeight: masterColumn.implicitHeight

                        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        WheelHandler {
                            acceptedModifiers: Qt.NoModifier | Qt.ShiftModifier
                            onWheel: function (wheel) {
                                const shift = wheel.modifiers & Qt.ShiftModifier
                                const delta = wheel.pixelDelta.y !== 0 ? wheel.pixelDelta.y : wheel.angleDelta.y / 4
                                if (shift) {
                                    const maxX = Math.max(0, masterFlick.contentWidth - masterFlick.width)
                                    masterFlick.contentX = Math.max(0, Math.min(maxX, masterFlick.contentX - delta))
                                } else {
                                    const maxY = Math.max(0, masterFlick.contentHeight - masterFlick.height)
                                    if (masterFlick.contentHeight > masterFlick.height) {
                                        masterFlick.contentY = Math.max(0, Math.min(maxY, masterFlick.contentY - delta))
                                    } else {
                                        const maxX2 = Math.max(0, masterFlick.contentWidth - masterFlick.width)
                                        masterFlick.contentX = Math.max(0, Math.min(maxX2, masterFlick.contentX - delta))
                                    }
                                }
                                wheel.accepted = true
                            }
                        }

                        property int cellW: 64
                        property var masterRows: { root.rev; return app.masterGrid() }

                        Column {
                            id: masterColumn
                            width: masterFlick.contentWidth

                            Row {
                                Item {
                                    width: 110; height: 30
                                    Text {
                                        anchors.centerIn: parent
                                        text: qsTr("班级 \\ 节次")
                                        color: Theme.textDisabled
                                        font.pixelSize: Theme.fontCaption
                                    }
                                }
                                Repeater {
                                    model: app.daysPerWeek * app.periodsPerDay
                                    delegate: Item {
                                        required property int index
                                        width: masterFlick.cellW; height: 30
                                        Text {
                                            anchors.centerIn: parent
                                            text: {
                                                const day = Math.floor(index / app.periodsPerDay)
                                                const period = index % app.periodsPerDay
                                                const days = ["一", "二", "三", "四", "五", "六", "日"]
                                                return qsTr("%1.%2").arg(days[day]).arg(period + 1)
                                            }
                                            color: Theme.textSecondary
                                            font.pixelSize: 10
                                        }
                                    }
                                }
                            }

                            ListView {
                                id: masterList
                                width: parent.width
                                height: contentHeight
                                interactive: false
                                model: masterFlick.masterRows
                                delegate: Row {
                                    required property var modelData
                                    Item {
                                        width: 110; height: 34
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.left: parent.left
                                            anchors.leftMargin: Theme.spacingS
                                            text: modelData.className
                                            color: Theme.text
                                            font.pixelSize: Theme.fontCaption
                                            font.bold: true
                                            elide: Text.ElideRight
                                            width: 100
                                        }
                                    }
                                    Repeater {
                                        model: modelData.cells
                                        delegate: Rectangle {
                                            required property var modelData
                                            width: masterFlick.cellW; height: 34
                                            radius: 3
                                            color: {
                                                if (modelData.courseId === undefined) return "transparent"
                                                if (root.highlightCourse >= 0 && modelData.courseId === root.highlightCourse)
                                                    return Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.22)
                                                return modelData.major ? Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.08)
                                                                       : Theme.surfaceAlt
                                            }
                                            border.color: root.highlightCourse >= 0 && modelData.courseId === root.highlightCourse
                                                          ? Theme.primary : "transparent"
                                            Text {
                                                anchors.centerIn: parent
                                                text: (modelData.text ?? "")
                                                      + (modelData.half === 1 ? " 单" : modelData.half === 2 ? " 双" : "")
                                                color: Theme.text
                                                font.pixelSize: 10
                                                elide: Text.ElideRight
                                                width: parent.width - 6
                                                horizontalAlignment: Text.AlignHCenter
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: {
                                                    if (modelData.courseId === undefined) return
                                                    root.highlightCourse =
                                                        root.highlightCourse === modelData.courseId ? -1 : modelData.courseId
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ---- 班级/老师/教室视图 ----
                    TimetableGrid {
                        id: viewGrid
                        anchors.fill: parent
                        visible: root.viewMode > 0 && app.hasSchedule && root.currentEntityId() >= 0
                        revision: root.rev
                        editable: root.adjustMode && root.viewMode === 1
                        cellText: function (day, period) {
                            const slot = day * app.periodsPerDay + period
                            const row = root.entityRow()
                            const cell = row[slot]
                            if (!cell) return ""
                            if (root.viewMode === 1)
                                return cell.text ?? ""
                            const items = cell
                            if (items.length === 0) return ""
                            const first = items[0]
                            return first.text + (items.length > 1 ? qsTr(" 等%1项").arg(items.length) : "")
                        }
                        cellSubText: function (day, period) {
                            const slot = day * app.periodsPerDay + period
                            const row = root.entityRow()
                            const cell = row[slot]
                            if (!cell) return ""
                            if (root.viewMode === 1) {
                                let sub = cell.sub ?? ""
                                if (cell.half === 1) sub = (sub ? sub + " " : "") + qsTr("◐单周")
                                else if (cell.half === 2) sub = (sub ? sub + " " : "") + qsTr("◑双周")
                                if (cell.pre) sub = "🔒 " + sub
                                return sub
                            }
                            return (cell.length > 0 && cell[0].sub) ? cell[0].sub : ""
                        }
                        cellAccent: function (day, period) {
                            const slot = day * app.periodsPerDay + period
                            const row = root.entityRow()
                            const cell = row[slot]
                            if (!cell) return false
                            return root.viewMode === 1 ? (cell.major === true) : false
                        }
                        cellHighlight: function (day, period) {
                            if (root.highlightCourse < 0) return false
                            const slot = day * app.periodsPerDay + period
                            const row = root.entityRow()
                            const cell = row[slot]
                            if (!cell) return false
                            if (root.viewMode === 1)
                                return cell.courseId === root.highlightCourse
                            for (const item of cell) {
                                if (item.courseId === root.highlightCourse)
                                    return true
                            }
                            return false
                        }
                        cellTint: function (day, period) {
                            const slot = day * app.periodsPerDay + period
                            if (root.targets.length === 0) return 0
                            const t = root.targets[slot]
                            if (t === 1) return 1
                            if (t === 2) return 2
                            if (t === 0 && (root.selectedSlot >= 0 || root.armPlace)) return 3
                            return 0
                        }
                        cellPayload: function (day, period) {
                            if (root.viewMode !== 1) return null
                            const slot = day * app.periodsPerDay + period
                            const row = root.entityRow()
                            const cell = row[slot]
                            if (!cell || cell.courseId === undefined) return null
                            return { "slot": slot, "half": cell.half ?? 0, "courseId": cell.courseId }
                        }
                        onCellClicked: function (day, period) {
                            const slot = day * app.periodsPerDay + period
                            if (!root.adjustMode || root.viewMode !== 1) {
                                // 只读：点击切换高亮分布
                                const row = root.entityRow()
                                const cell = row[slot]
                                if (!cell) return
                                let cid = -1
                                if (root.viewMode === 1)
                                    cid = cell.courseId ?? -1
                                else if (cell.length > 0)
                                    cid = cell[0].courseId ?? -1
                                if (cid >= 0)
                                    root.highlightCourse = root.highlightCourse === cid ? -1 : cid
                                return
                            }
                            const classId = root.currentEntityId()
                            // 未排课落子模式
                            if (root.armPlace) {
                                const res = adjust.placeUnplaced(classId, root.armPlace.courseId,
                                                                 slot, root.armPlace.half)
                                if (res.ok) {
                                    root.armPlace = null
                                    root.refreshTargets()
                                } else {
                                    root.showReason(res.reason)
                                }
                                return
                            }
                            // 点选式调课
                            if (root.selectedSlot < 0) {
                                const row = root.entityRow()
                                const cell = row[slot]
                                if (cell && cell.courseId !== undefined) {
                                    root.selectedSlot = slot
                                    root.selectedHalf = cell.half ?? 0
                                    root.refreshTargets()
                                }
                            } else if (root.selectedSlot === slot) {
                                root.clearSelection()
                            } else {
                                const res = adjust.moveCell(classId, root.selectedSlot,
                                                            root.selectedHalf, slot,
                                                            root.selectedHalf)
                                if (res.ok)
                                    root.clearSelection()
                                else
                                    root.showReason(res.reason)
                            }
                        }
                        onDropRequested: function (fromPayload, toSlot) {
                            const classId = root.currentEntityId()
                            let res
                            if (fromPayload.fromBuffer === true)
                                res = adjust.bufferPlace(toSlot, 0)
                            else
                                res = adjust.moveCell(classId, fromPayload.slot, fromPayload.half,
                                                      toSlot, fromPayload.half)
                            if (!res.ok)
                                root.showReason(res.reason)
                            root.clearSelection()
                        }
                        onCellRightClicked: function (day, period, px, py) {
                            if (!root.adjustMode || root.viewMode !== 1)
                                return
                            const slot = day * app.periodsPerDay + period
                            const row = root.entityRow()
                            const cell = row[slot]
                            if (!cell || cell.courseId === undefined)
                                return
                            contextMenu.cellSlot = slot
                            contextMenu.cellCourseId = cell.courseId
                            contextMenu.cellHalf = cell.half ?? 0
                            contextMenu.x = px + viewGrid.x
                            contextMenu.y = py + viewGrid.y
                            contextMenu.open()
                        }
                    }

                    AppEmptyState {
                        anchors.centerIn: parent
                        visible: root.viewMode > 0 && app.hasSchedule && root.currentEntityId() < 0
                        icon: "📊"
                        title: qsTr("请先添加%1").arg(root.viewNames[root.viewMode])
                    }

                    // ---- 空课表引导 ----
                    ColumnLayout {
                        anchors.centerIn: parent
                        visible: !app.hasSchedule
                        spacing: Theme.spacingM
                        AppEmptyState {
                            icon: "🗓️"
                            title: qsTr("尚未排课")
                            hint: qsTr("完成教学计划录入后，先到「自动排课」页生成课表")
                        }
                        AppButton {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("去自动排课 →")
                            variant: "primary"
                            enabled: app.hasProject
                            onClicked: {
                                const w = ApplicationWindow.window
                                if (w) w.currentPage = 6
                            }
                        }
                    }
                }
            }

            // ---- 调整侧栏 ----
            AppCard {
                Layout.preferredWidth: 290
                Layout.fillHeight: true
                visible: root.adjustMode && root.viewMode === 1

                Flickable {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingM
                    contentHeight: sideColumn.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    ColumnLayout {
                        id: sideColumn
                        width: 290 - Theme.spacingM * 2
                        spacing: Theme.spacingM

                        // 未排课程
                        Text {
                            text: qsTr("未排课程")
                            font.pixelSize: Theme.fontBody
                            font.bold: true
                            color: Theme.text
                        }
                        Repeater {
                            model: {
                                root.rev
                                const cid = root.currentEntityId()
                                return cid >= 0 ? adjust.unplacedCourses(cid) : []
                            }
                            delegate: Rectangle {
                                required property var modelData
                                width: sideColumn.width
                                height: 38
                                radius: Theme.radiusM
                                color: root.armPlace && root.armPlace.courseId === modelData.courseId
                                       ? Theme.selectedBg
                                       : (upHover.containsMouse ? Theme.overlayHover : Theme.surfaceAlt)
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.spacingM
                                    anchors.rightMargin: Theme.spacingM
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.name
                                        color: Theme.text
                                        font.pixelSize: Theme.fontBody
                                    }
                                    Text {
                                        text: qsTr("缺 %1 节").arg(modelData.missing)
                                        color: Theme.warning
                                        font.pixelSize: Theme.fontCaption
                                    }
                                }
                                MouseArea {
                                    id: upHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (root.armPlace && root.armPlace.courseId === modelData.courseId) {
                                            root.armPlace = null
                                        } else {
                                            root.armPlace = {
                                                "courseId": modelData.courseId,
                                                "half": modelData.missing === 0.5 ? 1 : 0
                                            }
                                        }
                                        root.selectedSlot = -1
                                        root.refreshTargets()
                                    }
                                }
                            }
                        }
                        Text {
                            visible: {
                                root.rev
                                const cid = root.currentEntityId()
                                return cid >= 0 && adjust.unplacedCourses(cid).length === 0
                            }
                            text: qsTr("该班课节已全部排完 ✓")
                            color: Theme.success
                            font.pixelSize: Theme.fontCaption
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                        // 缓冲节次（暂存区）
                        Text {
                            text: qsTr("缓冲节次")
                            font.pixelSize: Theme.fontBody
                            font.bold: true
                            color: Theme.text
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            height: 46
                            radius: Theme.radiusM
                            color: bufferDrop.containsDrag ? Theme.selectedBg : Theme.surfaceAlt
                            border.color: bufferDrop.containsDrag ? Theme.primary : Theme.border
                            border.width: 1.5

                            Text {
                                anchors.centerIn: parent
                                visible: adjust.bufferContent.text === undefined
                                text: qsTr("拖入课程暂存")
                                color: Theme.textDisabled
                                font.pixelSize: Theme.fontCaption
                            }
                            // 暂存内容（可拖出）
                            Item {
                                id: bufferItem
                                anchors.centerIn: parent
                                width: bufferLabel.implicitWidth + 24
                                height: 30
                                visible: adjust.bufferContent.text !== undefined
                                property var srcPayload: ({ "fromBuffer": true })
                                Drag.keys: ["course"]
                                Drag.active: bufferMouse.drag.active
                                Drag.hotSpot.x: width / 2
                                Drag.hotSpot.y: height / 2
                                Rectangle {
                                    anchors.fill: parent
                                    radius: Theme.radiusS
                                    color: Theme.primary
                                }
                                Text {
                                    id: bufferLabel
                                    anchors.centerIn: parent
                                    text: adjust.bufferContent.text ?? ""
                                    color: Theme.onPrimary
                                    font.pixelSize: Theme.fontCaption
                                }
                                MouseArea {
                                    id: bufferMouse
                                    anchors.fill: parent
                                    drag.target: bufferItem
                                    cursorShape: Qt.PointingHandCursor
                                    onReleased: {
                                        bufferItem.Drag.drop()
                                        bufferItem.x = 0
                                        bufferItem.y = 0
                                    }
                                }
                            }
                            DropArea {
                                id: bufferDrop
                                anchors.fill: parent
                                keys: ["course"]
                                onDropped: function (drop) {
                                    const src = drop.source
                                    if (src && src.srcPayload && src.srcPayload.fromBuffer !== true) {
                                        const res = adjust.bufferStore(root.currentEntityId(),
                                                                       src.srcPayload.slot,
                                                                       src.srcPayload.half)
                                        if (!res.ok)
                                            root.showReason(res.reason)
                                    }
                                }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                        // 老师当周占用（选中课时联动）
                        Text {
                            visible: root.selectedSlot >= 0
                            text: qsTr("任课老师当周占用")
                            font.pixelSize: Theme.fontBody
                            font.bold: true
                            color: Theme.text
                        }
                        Repeater {
                            model: root.selectedSlot >= 0
                                   ? adjust.teacherBusyText(root.currentEntityId(), root.selectedSlot) : []
                            delegate: Text {
                                required property string modelData
                                Layout.fillWidth: true
                                text: modelData
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontCaption
                                wrapMode: Text.WordWrap
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true; height: 1; color: Theme.border
                            visible: root.selectedSlot >= 0
                        }

                        // 缺陷提示
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: qsTr("缺陷提示")
                                font.pixelSize: Theme.fontBody
                                font.bold: true
                                color: Theme.text
                            }
                            Item { Layout.fillWidth: true }
                            AppSwitch {
                                checked: adjust.issueHintsEnabled
                                onToggled: adjust.issueHintsEnabled = checked
                            }
                        }
                        Repeater {
                            model: {
                                root.rev
                                if (!adjust.issueHintsEnabled) return []
                                const cid = root.currentEntityId()
                                return cid >= 0 ? adjust.cellIssues(cid) : []
                            }
                            delegate: Text {
                                required property var modelData
                                Layout.fillWidth: true
                                text: "⚠ " + modelData.text
                                color: Theme.warning
                                font.pixelSize: Theme.fontCaption
                                wrapMode: Text.WordWrap
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }
    }

    // 右键菜单
    Popup {
        id: contextMenu
        padding: 4
        property int cellSlot: -1
        property int cellCourseId: -1
        property int cellHalf: 0
        background: Rectangle { radius: Theme.radiusM; color: Theme.surface; border.color: Theme.border }
        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 100 }
        }

        contentItem: Column {
            spacing: 0
            Repeater {
                model: [qsTr("查看该课老师当周占用"), qsTr("切换到老师视图"), qsTr("加亮同课分布"), qsTr("清除此课")]
                delegate: Rectangle {
                    required property int index
                    required property string modelData
                    width: 180
                    height: 34
                    radius: Theme.radiusS
                    color: menuHover.containsMouse ? Theme.overlayHover : "transparent"
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacingM
                        text: modelData
                        color: index === 3 ? Theme.danger : Theme.text
                        font.pixelSize: Theme.fontBody
                    }
                    MouseArea {
                        id: menuHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            contextMenu.close()
                            const classId = root.currentEntityId()
                            if (index === 0) {
                                root.selectedSlot = contextMenu.cellSlot
                                root.selectedHalf = contextMenu.cellHalf
                                root.refreshTargets()
                            } else if (index === 1) {
                                // 切换到该课第一位老师的老师视图
                                const tids = app.courseTeachers(classId, contextMenu.cellCourseId)
                                if (tids.length > 0) {
                                    for (let i = 0; i < teacherModel.count; ++i) {
                                        if (teacherModel.get(i, "id") === tids[0]) {
                                            root.viewMode = 2
                                            root.entityIndex = i
                                            root.clearSelection()
                                            root.rev++
                                            break
                                        }
                                    }
                                } else {
                                    root.showReason(qsTr("该课未设任课老师"))
                                }
                            } else if (index === 2) {
                                root.highlightCourse = contextMenu.cellCourseId
                            } else if (index === 3) {
                                const res = adjust.unscheduleCell(classId, contextMenu.cellSlot,
                                                                  contextMenu.cellHalf)
                                if (!res.ok)
                                    root.showReason(res.reason)
                                root.clearSelection()
                            }
                        }
                    }
                }
            }
        }
    }

    // 导出调课通知对话框
    FileDialog {
        id: noticeDialog
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("文本文件 (*.txt)"), qsTr("PDF 文档 (*.pdf)")]
        onAccepted: {
            if (!adjust.exportTempNotice(selectedFile.toString()))
                root.showReason(adjust.lastExportError())
        }
    }
}
