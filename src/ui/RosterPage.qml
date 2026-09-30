import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 名单管理页：班级/课程/老师/教室 标签切换 + 自绘表格（行内编辑、斑马纹、悬停）
Item {
    id: root
    objectName: "rosterPage"

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
                text: qsTr("⊞ 批量添加…")
                enabled: app.hasProject
                onClicked: batchDialog.open()
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
                    objectName: "rosterTable"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: root.currentModel
                    activeFocusOnTab: true
                    keyNavigationEnabled: false

                    // 键盘导航：↑/↓ 移动并选中；Enter/Space 保持选中当前行
                    function keySelect(d) {
                        if (count === 0) return
                        const base = currentIndex < 0 ? Math.max(0, root.selectedRow) : currentIndex
                        currentIndex = Math.max(0, Math.min(count - 1, base + d))
                        root.selectedRow = currentIndex
                        positionViewAtIndex(currentIndex, ListView.Contain)
                    }
                    Keys.onUpPressed: keySelect(-1)
                    Keys.onDownPressed: keySelect(1)
                    Keys.onReturnPressed: if (currentIndex >= 0) root.selectedRow = currentIndex
                    Keys.onSpacePressed: if (currentIndex >= 0) root.selectedRow = currentIndex

                    Accessible.role: Accessible.Table
                    Accessible.name: root.tabNames[root.currentTab]

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
                                table.currentIndex = rowItem.index
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
                              ? qsTr("「✚ 添加」加一行；「⊞ 批量添加」粘贴名单或按编号一次生成多条；表格内单击单元格即可改名")
                              : ""
                    }
                }

                // 键盘焦点环（叠在列表视口上，不随内容滚动）
                Rectangle {
                    anchors.fill: table
                    anchors.margins: -3
                    radius: Theme.radiusS
                    color: "transparent"
                    border.width: table.activeFocus ? 2 : 0
                    border.color: Theme.primary
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
    // 批量添加：粘贴多行名单 / 按「前缀+编号+后缀」批量生成
    Popup {
        id: batchDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 560
        padding: 0

        property int mode: 0      // 0 粘贴名单，1 按编号生成
        property string status: ""

        function generatedNames() {
            let out = []
            const n = Math.max(0, genCount.value)
            for (let i = 0; i < n; ++i)
                out.push(genPrefix.text + (genStart.value + i) + genSuffix.text)
            return out
        }
        function pendingNames() {
            if (mode === 0)
                return pasteArea.text.split(/\r?\n/)
            return generatedNames()
        }
        function pendingCount() {
            let n = 0
            for (const s of pendingNames())
                if (s.trim().length > 0) n++
            return n
        }
        function apply() {
            const added = root.currentModel.appendRows(pendingNames())
            if (added > 0) {
                status = qsTr("已添加 %1 行").arg(added)
                pasteArea.text = ""
                batchDialog.close()
            } else {
                status = qsTr("没有可添加的名称，请检查输入")
            }
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
        onOpened: {
            root.rev
            batchDialog.mode = 0
            batchDialog.status = ""
            pasteArea.text = ""
            genPrefix.text = qsTr("新%1").arg(root.tabNames[root.currentTab])
            genStart.value = 1
            genCount.value = 4
            genSuffix.text = ""
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingS

            Text {
                Layout.topMargin: Theme.spacingL
                Layout.leftMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                text: qsTr("批量添加%1").arg(root.tabNames[root.currentTab])
                font.pixelSize: Theme.fontTitle
                font.bold: true
                color: Theme.text
            }
            Text {
                Layout.leftMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                Layout.fillWidth: true
                text: qsTr("一次建多条省时间；建好后在表格里单击任意单元格即可改名")
                font.pixelSize: Theme.fontCaption
                color: Theme.textSecondary
            }

            AppTabs {
                Layout.leftMargin: Theme.spacingL
                model: [qsTr("粘贴名单"), qsTr("按编号生成")]
                currentIndex: batchDialog.mode
                onTabClicked: function (index) { batchDialog.mode = index }
            }

            // 模式一：粘贴多行
            ColumnLayout {
                Layout.leftMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                Layout.fillWidth: true
                spacing: Theme.spacingXS
                visible: batchDialog.mode === 0

                Text {
                    text: qsTr("每行一个名称（可从 Excel 或文本里直接粘贴）")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 180
                    radius: Theme.radiusS
                    color: Theme.surfaceAlt
                    border.color: Theme.border
                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingXS
                        TextArea {
                            id: pasteArea
                            placeholderText: qsTr("高一1班\n高一2班\n高一3班")
                            color: Theme.text
                            font.pixelSize: Theme.fontBody
                            wrapMode: TextArea.NoWrap
                            background: null
                        }
                    }
                }
            }

            // 模式二：前缀 + 编号 + 后缀
            ColumnLayout {
                Layout.leftMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                Layout.fillWidth: true
                spacing: Theme.spacingXS
                visible: batchDialog.mode === 1

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.spacingM
                    rowSpacing: Theme.spacingS

                    Text { text: qsTr("名称前缀"); color: Theme.text; font.pixelSize: Theme.fontBody }
                    AppTextField { id: genPrefix; Layout.fillWidth: true; showClear: false }

                    Text { text: qsTr("起始编号"); color: Theme.text; font.pixelSize: Theme.fontBody }
                    AppStepper { id: genStart; from: 1; to: 999; value: 1 }

                    Text { text: qsTr("生成数量"); color: Theme.text; font.pixelSize: Theme.fontBody }
                    AppStepper { id: genCount; from: 1; to: 200; value: 4; suffix: qsTr(" 条") }

                    Text { text: qsTr("名称后缀"); color: Theme.text; font.pixelSize: Theme.fontBody }
                    AppTextField { id: genSuffix; Layout.fillWidth: true; placeholder: qsTr("例如：班（可留空）"); showClear: false }
                }
                Text {
                    Layout.fillWidth: true
                    text: {
                        const names = batchDialog.generatedNames()
                        const head = names.slice(0, 4).join(qsTr("、"))
                        return qsTr("将生成：%1%2").arg(head).arg(names.length > 4 ? qsTr(" …") : "")
                    }
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.WordWrap
                }
            }

            RowLayout {
                Layout.leftMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                Layout.bottomMargin: Theme.spacingL
                Layout.topMargin: Theme.spacingS
                Layout.fillWidth: true
                spacing: Theme.spacingS

                Text {
                    Layout.fillWidth: true
                    text: batchDialog.status
                    color: batchDialog.status.indexOf(qsTr("已添加")) === 0 ? Theme.success : Theme.warning
                    font.pixelSize: Theme.fontCaption
                    visible: batchDialog.status !== ""
                }
                AppButton {
                    text: qsTr("取消")
                    onClicked: batchDialog.close()
                }
                AppButton {
                    text: qsTr("添加 %1 行").arg(batchDialog.pendingCount())
                    variant: "primary"
                    enabled: batchDialog.pendingCount() > 0
                    onClicked: batchDialog.apply()
                }
            }
        }
    }

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
