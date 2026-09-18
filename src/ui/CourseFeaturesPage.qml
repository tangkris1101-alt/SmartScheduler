import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 课程设置页：课程特征 + 老师特征（左列表 + 右侧详情表单）
Item {
    id: root
    property int currentTab: 0
    property int selectedRow: -1

    readonly property var currentModel: currentTab === 0 ? courseModel : teacherModel
    readonly property var tabNames: [qsTr("课程特征"), qsTr("老师特征")]

    onVisibleChanged: if (visible) refreshForm()
    onSelectedRowChanged: refreshForm()

    // 提交名称编辑：回车 / 失焦 / 切换条目或标签前调用（仅变化时写模型）
    function commitName() {
        if (root.selectedRow < 0 || root.selectedRow >= root.currentModel.count)
            return
        const name = nameField.text.trim()
        const cur = String(root.currentModel.get(root.selectedRow, "name") ?? "")
        if (name.length > 0 && name !== cur)
            root.currentModel.set(root.selectedRow, "name", name)
        else if (name !== cur)
            nameField.text = cur // 空名等无效输入：还原为模型当前值
    }

    function refreshForm() {
        if (root.currentTab === 0 && root.selectedRow >= 0 && root.selectedRow < courseModel.count) {
            nameField.text = courseModel.get(root.selectedRow, "name")
            majorSwitch.checked = courseModel.get(root.selectedRow, "isMajor")
            scienceSwitch.checked = courseModel.get(root.selectedRow, "isScience")
            spreadSwitch.checked = courseModel.get(root.selectedRow, "evenSpread")
            alignStepper.value = courseModel.get(root.selectedRow, "progressAlignCount")
            ampmCombo.currentIndex = courseModel.get(root.selectedRow, "amPmPolicy")
        } else if (root.currentTab === 1 && root.selectedRow >= 0 && root.selectedRow < teacherModel.count) {
            nameField.text = teacherModel.get(root.selectedRow, "name")
            const maxP = teacherModel.get(root.selectedRow, "maxPeriodsPerDay")
            unlimitedSwitch.checked = (maxP < 0)
            maxStepper.value = maxP < 0 ? 6 : maxP
            mutexCombo.currentIndex = Math.min(8, Math.max(0, teacherModel.get(root.selectedRow, "mutualExclusionGroup")))
        }
    }

    Connections {
        target: courseModel
        function onDataChanged() { if (root.currentTab === 0) root.refreshForm() }
    }
    Connections {
        target: teacherModel
        function onDataChanged() { if (root.currentTab === 1) root.refreshForm() }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingXL
        spacing: Theme.spacingL

        // 左：实体列表
        AppCard {
            Layout.preferredWidth: 320
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingL
                spacing: Theme.spacingM

                AppTabs {
                    model: root.tabNames
                    currentIndex: root.currentTab
                    onTabClicked: function (index) {
                        root.commitName() // 切换标签前提交未确认的名称编辑
                        root.currentTab = index
                        root.selectedRow = -1
                        featureSearch.text = ""
                        courseModel.filter = ""
                        teacherModel.filter = ""
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingS
                    AppTextField {
                        id: featureSearch
                        Layout.fillWidth: true
                        placeholder: qsTr("搜索…")
                        onTextChanged: root.currentModel.filter = text
                    }
                    AppButton {
                        text: qsTr("✚ 添加")
                        variant: "primary"
                        enabled: app.hasProject
                        onClicked: {
                            featureSearch.text = ""
                            root.currentModel.filter = ""
                            root.currentModel.appendRow()
                            root.selectedRow = root.currentModel.count - 1
                        }
                    }
                }

                ListView {
                    id: entityList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: root.currentModel
                    spacing: 2
                    activeFocusOnTab: true
                    keyNavigationEnabled: false

                    // 键盘导航：↑/↓ 移动并选中
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

                    Accessible.role: Accessible.List
                    Accessible.name: root.currentTab === 0 ? qsTr("课程列表") : qsTr("老师列表")

                    delegate: Rectangle {
                        id: eRow
                        required property int index
                        width: entityList.width
                        height: 44
                        radius: Theme.radiusM
                        color: root.selectedRow === index ? Theme.selectedBg
                             : (eHover.containsMouse ? Theme.overlayHover : "transparent")
                        Behavior on color { ColorAnimation { duration: 90 } }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingM
                            anchors.rightMargin: Theme.spacingM
                            Text {
                                Layout.fillWidth: true
                                text: root.currentModel.get(eRow.index, "name")
                                color: Theme.text
                                font.pixelSize: Theme.fontBody
                                elide: Text.ElideRight
                            }
                            Text {
                                visible: text.length > 0
                                text: root.currentTab === 0 && courseModel.get(eRow.index, "isMajor")
                                      ? qsTr("主课") : ""
                                color: Theme.primary
                                font.pixelSize: Theme.fontCaption
                            }
                        }
                        MouseArea {
                            id: eHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (eRow.index !== root.selectedRow)
                                    root.commitName() // 切换前提交未确认的名称编辑
                                root.selectedRow = eRow.index
                                entityList.currentIndex = eRow.index
                            }
                        }
                    }

                    AppEmptyState {
                        anchors.centerIn: parent
                        visible: root.currentModel.count === 0
                        icon: "📭"
                        title: root.currentTab === 0 ? qsTr("还没有课程") : qsTr("还没有老师")
                        hint: qsTr("点击上方「添加」新建，或在「名单管理」页批量导入")
                    }
                }

                // 键盘焦点环（叠在列表视口上，不随内容滚动）
                Rectangle {
                    anchors.fill: entityList
                    anchors.margins: -3
                    radius: Theme.radiusS
                    color: "transparent"
                    border.width: entityList.activeFocus ? 2 : 0
                    border.color: Theme.primary
                }
            }
        }

        // 右：详情表单
        AppCard {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingXL
                spacing: Theme.spacingM
                visible: root.selectedRow >= 0 && root.selectedRow < root.currentModel.count

                Text {
                    text: root.currentTab === 0 ? qsTr("课程特征") : qsTr("老师特征")
                    font.pixelSize: Theme.fontSubtitle
                    font.bold: true
                    color: Theme.text
                }

                // 名称
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.preferredWidth: 130
                        text: qsTr("名称")
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                    }
                    AppTextField {
                        id: nameField
                        Layout.fillWidth: true
                        showClear: false
                        commitOnFocusLoss: true
                        onAccepted: root.commitName()
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                // ---- 课程特征 ----
                GridLayout {
                    visible: root.currentTab === 0
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.spacingL
                    rowSpacing: Theme.spacingL

                    Text { text: qsTr("主课"); color: Theme.text; font.pixelSize: Theme.fontBody }
                    RowLayout {
                        AppSwitch {
                            id: majorSwitch
                            onToggled: { majorSwitch.checked = checked
                                courseModel.set(root.selectedRow, "isMajor", checked) }
                        }
                        Text { text: qsTr("优先排上午黄金时段"); color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                        Item { Layout.fillWidth: true }
                    }

                    Text { text: qsTr("理科"); color: Theme.text; font.pixelSize: Theme.fontBody }
                    RowLayout {
                        AppSwitch {
                            id: scienceSwitch
                            onToggled: { scienceSwitch.checked = checked
                                courseModel.set(root.selectedRow, "isScience", checked) }
                        }
                        Text { text: qsTr("用于文理搭配分布"); color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                        Item { Layout.fillWidth: true }
                    }

                    Text { text: qsTr("平均分布"); color: Theme.text; font.pixelSize: Theme.fontBody }
                    RowLayout {
                        AppSwitch {
                            id: spreadSwitch
                            onToggled: { spreadSwitch.checked = checked
                                courseModel.set(root.selectedRow, "evenSpread", checked) }
                        }
                        Text { text: qsTr("同一节次全校尽量少班同上"); color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                        Item { Layout.fillWidth: true }
                    }

                    Text { text: qsTr("进度一致班级数"); color: Theme.text; font.pixelSize: Theme.fontBody }
                    AppStepper {
                        id: alignStepper
                        from: 1; to: 8
                        suffix: qsTr(" 个班")
                        onValueModified: courseModel.set(root.selectedRow, "progressAlignCount", value)
                    }

                    Text { text: qsTr("上下午分配"); color: Theme.text; font.pixelSize: Theme.fontBody }
                    AppComboBox {
                        id: ampmCombo
                        model: [qsTr("自动"), qsTr("优先上午"), qsTr("优先下午")]
                        onActivated: courseModel.set(root.selectedRow, "amPmPolicy", index)
                    }
                }

                // ---- 老师特征 ----
                GridLayout {
                    visible: root.currentTab === 1
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.spacingL
                    rowSpacing: Theme.spacingL

                    Text { text: qsTr("日课时限制"); color: Theme.text; font.pixelSize: Theme.fontBody }
                    RowLayout {
                        spacing: Theme.spacingM
                        Text { text: qsTr("不限"); color: Theme.text; font.pixelSize: Theme.fontBody }
                        AppSwitch {
                            id: unlimitedSwitch
                            onToggled: { unlimitedSwitch.checked = checked
                                teacherModel.set(root.selectedRow, "maxPeriodsPerDay",
                                                 checked ? -1 : maxStepper.value) }
                        }
                        AppStepper {
                            id: maxStepper
                            from: 1; to: 12
                            suffix: qsTr(" 节")
                            enabled: !unlimitedSwitch.checked
                            opacity: enabled ? 1 : 0.4
                            onValueModified: teacherModel.set(root.selectedRow, "maxPeriodsPerDay", value)
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Text { text: qsTr("互斥组"); color: Theme.text; font.pixelSize: Theme.fontBody }
                    RowLayout {
                        spacing: Theme.spacingM
                        AppComboBox {
                            id: mutexCombo
                            model: [qsTr("无（不互斥）"), qsTr("互斥组 1"), qsTr("互斥组 2"), qsTr("互斥组 3"),
                                    qsTr("互斥组 4"), qsTr("互斥组 5"), qsTr("互斥组 6"), qsTr("互斥组 7"), qsTr("互斥组 8")]
                            onActivated: teacherModel.set(root.selectedRow, "mutualExclusionGroup",
                                                          index === 0 ? -1 : index)
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("同组老师同一节次只能一人有课（如夫妻同校）")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }

            AppEmptyState {
                anchors.centerIn: parent
                visible: root.selectedRow < 0 || root.selectedRow >= root.currentModel.count
                icon: "⚙️"
                title: qsTr("选择要编辑的%1").arg(root.currentTab === 0 ? qsTr("课程") : qsTr("老师"))
                hint: qsTr("从左侧列表选择，或点击「添加」新建")
            }
        }
    }
}
