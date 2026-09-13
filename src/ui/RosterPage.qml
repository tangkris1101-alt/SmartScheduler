import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 名单管理页：班级/课程/老师/教室 标签切换 + 自绘表格（行内编辑、斑马纹、悬停）
Item {
    id: root

    property int currentTab: 0
    property int selectedRow: -1
    // 刷新计数：QAbstractListModel::get() 不参与绑定，数据变更后递增它强制单元格重读
    property int rev: 0

    readonly property var tabModels: [classModel, courseModel, teacherModel, classroomModel]
    readonly property var tabColumns: [
        [ { role: "name", label: qsTr("名称"), w: 3 }, { role: "shortName", label: qsTr("简称"), w: 2 },
          { role: "code", label: qsTr("拼音码"), w: 2 }, { role: "priority", label: qsTr("优先次序"), w: 1.5 },
          { role: "remark", label: qsTr("备注"), w: 4 } ],
        [ { role: "name", label: qsTr("名称"), w: 3 }, { role: "code", label: qsTr("拼音码"), w: 2 },
          { role: "priority", label: qsTr("优先次序"), w: 1.5 }, { role: "amPmText", label: qsTr("上下午"), w: 2 },
          { role: "remark", label: qsTr("备注"), w: 4 } ],
        [ { role: "name", label: qsTr("姓名"), w: 3 }, { role: "code", label: qsTr("拼音码"), w: 2 },
          { role: "priority", label: qsTr("优先次序"), w: 1.5 },
          { role: "maxPeriodsPerDay", label: qsTr("日最多课"), w: 1.5 },
          { role: "remark", label: qsTr("备注"), w: 3.5 } ],
        [ { role: "name", label: qsTr("名称"), w: 3 }, { role: "code", label: qsTr("拼音码"), w: 2 },
          { role: "capacity", label: qsTr("容量"), w: 1.5 }, { role: "priority", label: qsTr("优先次序"), w: 1.5 },
          { role: "remark", label: qsTr("备注"), w: 4 } ]
    ]
    readonly property var tabNames: [qsTr("班级"), qsTr("课程"), qsTr("老师"), qsTr("教室")]
    readonly property var currentModel: tabModels[currentTab]
    readonly property var currentColumns: tabColumns[currentTab]

    onVisibleChanged: {
        if (visible)
            currentModel.filter = searchField.text
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingXL
        spacing: Theme.spacingM

        // 工具条
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingM

            AppTabs {
                model: root.tabNames
                currentIndex: root.currentTab
                onTabClicked: function (index) {
                    root.currentTab = index
                    root.selectedRow = -1
                    searchField.text = ""
                    root.currentModel.filter = ""
                }
            }
            Item { Layout.fillWidth: true }
            AppTextField {
                id: searchField
                Layout.preferredWidth: 240
                placeholder: qsTr("搜索名称 / 拼音码…")
                onTextChanged: root.currentModel.filter = text
            }
            AppButton {
                text: qsTr("✚ 添加")
                variant: "primary"
                enabled: app.hasProject
                onClicked: root.currentModel.appendRow()
            }
            AppButton {
                text: qsTr("📥 Excel 导入")
                enabled: app.hasProject
                onClicked: importDialog.open()
            }
            AppButton {
                text: qsTr("删除")
                variant: "danger"
                enabled: root.selectedRow >= 0 && root.selectedRow < root.currentModel.count
                onClicked: deleteDialog.open()
            }
        }

        // 表格卡片
        AppCard {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // 表头
                Row {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    Repeater {
                        model: root.currentColumns
                        delegate: Text {
                            required property var modelData
                            width: headerRowWidth() * modelData.w
                            height: 40
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: Theme.spacingM
                            text: modelData.label
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                            font.bold: true
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.border
                }

                // 数据行
                ListView {
                    id: table
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: root.currentModel

                    delegate: Rectangle {
                        id: rowItem
                        required property int index
                        width: table.width
                        height: 42
                        color: root.selectedRow === index ? Theme.selectedBg
                             : (rowHover.containsMouse ? Theme.overlayHover
                             : (index % 2 ? Theme.surfaceAlt : "transparent"))
                        Behavior on color { ColorAnimation { duration: 90 } }

                        Row {
                            anchors.fill: parent
                            Repeater {
                                model: root.currentColumns
                                delegate: RosterCell {
                                    required property var modelData
                                    width: headerRowWidth() * modelData.w
                                    height: 42
                                    text: {
                                        root.rev // 建立依赖：数据变更后重新读取
                                        const v = root.currentModel.get(rowItem.index, modelData.role)
                                        return v === null || v === undefined ? "" : String(v)
                                    }
                                    onCommit: function (value) {
                                        root.currentModel.set(rowItem.index, modelData.role, value)
                                    }
                                }
                            }
                        }

                        MouseArea {
                            id: rowHover
                            anchors.fill: parent
                            hoverEnabled: true
                            propagateComposedEvents: true
                            onClicked: function (mouse) {
                                root.selectedRow = rowItem.index
                                mouse.accepted = false
                            }
                        }
                    }

                    AppEmptyState {
                        anchors.centerIn: parent
                        visible: root.currentModel.count === 0
                        icon: "🗂️"
                        title: app.hasProject ? qsTr("还没有%1").arg(root.tabNames[root.currentTab])
                                              : qsTr("请先新建或打开项目")
                        hint: app.hasProject
                              ? qsTr("点击右上角「添加」新建一行，或点击「Excel 导入」批量导入")
                              : ""
                    }
                }
            }
        }
    }

    // 单元格宽度分配函数（列权重 → 像素）
    function headerRowWidth() {
        return table.width / totalWeight()
    }
    function totalWeight() {
        let sum = 0
        for (let i = 0; i < root.currentColumns.length; ++i)
            sum += root.currentColumns[i].w
        return sum
    }

    // 模型数据变更 → 递增刷新计数，让单元格绑定重读
    Connections {
        target: root.currentModel
        function onDataChanged() { root.rev++ }
        function onModelReset() { root.rev++ }
        function onCountChanged() { root.rev++ }
    }

    AppConfirmDialog {
        id: deleteDialog
        parent: Overlay.overlay
        title: qsTr("删除%1").arg(root.tabNames[root.currentTab])
        message: root.selectedRow >= 0
                 ? qsTr("将删除「%1」。引用它的教学计划、限制条件、预排与合班会被一并清理，此操作不可撤销。确定删除吗？")
                   .arg(root.currentModel.get(root.selectedRow, "name"))
                 : ""
        okText: qsTr("删除")
        danger: true
        onAccepted: {
            root.currentModel.removeRow(root.selectedRow)
            root.selectedRow = -1
        }
    }

    ImportExcelDialog {
        id: importDialog
        parent: Overlay.overlay
    }
}
