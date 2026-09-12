import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 课表查看页：总课表 / 班级 / 老师 / 教室 四视图（只读）
Item {
    id: root
    property int viewMode: 0     // 0 总课表 1 班级 2 老师 3 教室
    property int entityIndex: 0
    property int rev: 0
    property var highlightCourse: -1 // 点格高亮同课分布；-1 无

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

    onVisibleChanged: if (visible) root.rev++
    Connections {
        target: app
        function onScheduleChanged() { root.rev++ }
        function onProjectChanged() { root.rev++; root.entityIndex = 0; root.highlightCourse = -1 }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingXL
        spacing: Theme.spacingM

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingM

            AppTabs {
                model: root.viewNames
                currentIndex: root.viewMode
                onTabClicked: function (index) {
                    root.viewMode = index
                    root.entityIndex = 0
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
                onActivated: { root.entityIndex = index; root.rev++ }
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: root.highlightCourse >= 0
                text: qsTr("高亮分布中，再点一次课程格取消")
                color: Theme.primary
                font.pixelSize: Theme.fontCaption
            }
        }

        AppCard {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // ---- 总课表：行=班级 列=节次，纵向 ListView 虚拟化 ----
            Flickable {
                id: masterFlick
                anchors.fill: parent
                anchors.margins: Theme.spacingM
                visible: root.viewMode === 0 && app.hasSchedule
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                contentWidth: 110 + app.daysPerWeek * app.periodsPerDay * cellW
                contentHeight: masterColumn.implicitHeight

                property int cellW: 64
                property var masterRows: { root.rev; return app.masterGrid() }

                Column {
                    id: masterColumn
                    width: masterFlick.contentWidth

                    // 表头：节次
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
            Item {
                anchors.fill: parent
                anchors.margins: Theme.spacingM
                visible: root.viewMode > 0 && app.hasSchedule

                TimetableGrid {
                    id: viewGrid
                    anchors.fill: parent
                    visible: root.currentEntityId() >= 0
                    revision: root.rev
                    cellText: function (day, period) {
                        const slot = day * app.periodsPerDay + period
                        const row = root.entityRow()
                        const cell = row[slot]
                        if (!cell) return ""
                        if (root.viewMode === 1)
                            return cell.text ?? ""
                        // 老师/教室视图：多条目取第一个 + 数量提示
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
                    onCellClicked: function (day, period) {
                        const slot = day * app.periodsPerDay + period
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
                    }
                }
                AppEmptyState {
                    anchors.centerIn: parent
                    visible: root.currentEntityId() < 0
                    icon: "📊"
                    title: qsTr("请先添加%1").arg(root.viewNames[root.viewMode])
                }
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
}
