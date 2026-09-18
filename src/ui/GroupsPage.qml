import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 归类分组页：名单 CRUD + 成员勾选
Item {
    id: root
    objectName: "groupsPage"
    property int selectedRow: -1
    property string memberFilter: ""
    property var candidateList: []
    // 成员版本号：成员勾选后递增，强制开关的 checked 绑定重算
    //（isMember() 是函数调用，模型内部变化不会触发绑定重算，代理也不会随 candidateList 重建）
    property int memberVersion: 0

    readonly property var typeOptions: [qsTr("年级"), qsTr("老师教研组"), qsTr("课程教研组")]

    function selectedGroupId() {
        return groupModel.idAt(root.selectedRow)
    }
    function refreshCandidates() {
        const gid = selectedGroupId()
        root.candidateList = gid >= 0 ? groupModel.candidates(gid) : []
        // 同步名称编辑框（直接赋值，不用绑定，避免输入中被重置）
        nameField.text = root.selectedRow >= 0 && root.selectedRow < groupModel.count
                         ? String(groupModel.get(root.selectedRow, "name") ?? "") : ""
    }
    // 提交名称编辑：回车 / 失焦 / 切换名单前均走这里（仅在真正变化时写模型，避免误标未保存）
    function commitName() {
        if (root.selectedRow < 0 || root.selectedRow >= groupModel.count)
            return
        const name = nameField.text.trim()
        const cur = String(groupModel.get(root.selectedRow, "name") ?? "")
        if (name.length > 0 && name !== cur)
            groupModel.set(root.selectedRow, "name", name)
        else if (name !== cur)
            nameField.text = cur // 空名等无效输入：还原为模型当前值
    }
    onSelectedRowChanged: refreshCandidates()

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingXL
        spacing: Theme.spacingL

        // 左：名单列表
        AppCard {
            Layout.preferredWidth: 340
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingL
                spacing: Theme.spacingM

                Text {
                    text: qsTr("归类名单")
                    font.pixelSize: Theme.fontSubtitle
                    font.bold: true
                    color: Theme.text
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingS
                    AppComboBox {
                        id: typeCombo
                        objectName: "typeCombo"
                        Layout.fillWidth: true
                        model: root.typeOptions
                        currentIndex: 0
                    }
                    AppButton {
                        text: qsTr("✚ 添加")
                        variant: "primary"
                        enabled: app.hasProject
                        onClicked: {
                            groupModel.addGroup(typeCombo.currentIndex,
                                                qsTr("新%1").arg(root.typeOptions[typeCombo.currentIndex]))
                            root.selectedRow = groupModel.count - 1
                        }
                    }
                }

                ListView {
                    id: groupList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: groupModel
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
                    Accessible.name: qsTr("归类名单")

                    delegate: Rectangle {
                        id: gRow
                        objectName: "groupRow"
                        required property int index
                        required property string name
                        required property string typeName
                        required property int memberCount
                        width: groupList.width
                        height: 52
                        radius: Theme.radiusM
                        color: root.selectedRow === index ? Theme.selectedBg
                             : (gHover.containsMouse ? Theme.overlayHover : "transparent")
                        Behavior on color { ColorAnimation { duration: 90 } }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingM
                            anchors.rightMargin: Theme.spacingM
                            spacing: Theme.spacingM

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: gRow.name
                                    color: Theme.text
                                    font.pixelSize: Theme.fontBody
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: gRow.typeName + " · " + qsTr("%1 名成员").arg(gRow.memberCount)
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontCaption
                                }
                            }
                        }
                        MouseArea {
                            id: gHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (gRow.index !== root.selectedRow)
                                    root.commitName() // 切换前提交未确认的名称编辑
                                root.selectedRow = gRow.index
                                groupList.currentIndex = gRow.index
                            }
                        }
                    }

                    AppEmptyState {
                        anchors.centerIn: parent
                        visible: groupModel.count === 0
                        icon: "🏷️"
                        title: qsTr("还没有归类名单")
                        hint: qsTr("选择类型后点击「添加」，例如：高一年级、语文教研组")
                    }
                }

                // 键盘焦点环（叠在列表视口上，不随内容滚动）
                Rectangle {
                    anchors.fill: groupList
                    anchors.margins: -3
                    radius: Theme.radiusS
                    color: "transparent"
                    border.width: groupList.activeFocus ? 2 : 0
                    border.color: Theme.primary
                }

                AppButton {
                    Layout.fillWidth: true
                    text: qsTr("删除选中名单")
                    variant: "danger"
                    enabled: root.selectedRow >= 0 && root.selectedRow < groupModel.count
                    onClicked: deleteGroupDialog.open()
                }
            }
        }

        // 右：成员管理
        AppCard {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingL
                spacing: Theme.spacingM
                visible: root.selectedRow >= 0 && root.selectedRow < groupModel.count

                Text {
                    text: qsTr("成员管理")
                    font.pixelSize: Theme.fontSubtitle
                    font.bold: true
                    color: Theme.text
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingM
                    Text {
                        text: qsTr("名单名称")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontCaption
                    }
                    AppTextField {
                        id: nameField
                        Layout.fillWidth: true
                        placeholder: qsTr("输入名称，回车或点击别处确认")
                        showClear: false
                        commitOnFocusLoss: true
                        onAccepted: root.commitName()
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("勾选属于该名单的条目；成员来自对应的班级/老师/课程名单")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.WordWrap
                }
                AppTextField {
                    Layout.fillWidth: true
                    placeholder: qsTr("搜索成员…")
                    onTextChanged: root.memberFilter = text
                }

                ListView {
                    id: memberList
                    objectName: "memberList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    spacing: 2
                    model: {
                        if (root.memberFilter.length === 0)
                            return root.candidateList
                        return root.candidateList.filter(function (c) {
                            return c.name.indexOf(root.memberFilter) >= 0
                        })
                    }

                    delegate: Rectangle {
                        id: mRow
                        required property var modelData
                        width: memberList.width
                        height: 40
                        radius: Theme.radiusM
                        color: mHover.containsMouse ? Theme.overlayHover : "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingM
                            anchors.rightMargin: Theme.spacingM
                            Text {
                                Layout.fillWidth: true
                                text: mRow.modelData.name
                                color: Theme.text
                                font.pixelSize: Theme.fontBody
                            }
                            AppSwitch {
                                objectName: "memberSwitch"
                                checked: {
                                    void root.memberVersion // 成员变动后强制重算
                                    return groupModel.isMember(root.selectedGroupId(),
                                                               mRow.modelData.id)
                                }
                                onToggled: {
                                    groupModel.toggleMember(root.selectedGroupId(),
                                                            mRow.modelData.id)
                                    ++root.memberVersion // 驱动 checked 绑定重算，开关视觉即时跟随
                                    root.refreshCandidates()
                                }
                            }
                        }
                        MouseArea {
                            id: mHover
                            anchors.fill: parent
                            hoverEnabled: true
                            propagateComposedEvents: true
                            onClicked: function (mouse) { mouse.accepted = false }
                        }
                    }

                    AppEmptyState {
                        anchors.centerIn: parent
                        visible: memberList.count === 0
                        icon: "🫙"
                        title: qsTr("没有可添加的条目")
                        hint: qsTr("请先在「名单管理」页添加对应的班级/老师/课程")
                    }
                }
            }

            AppEmptyState {
                anchors.centerIn: parent
                visible: root.selectedRow < 0 || root.selectedRow >= groupModel.count
                icon: "👈"
                title: qsTr("选择一个名单")
                hint: qsTr("从左侧选择或新建一个归类名单")
            }
        }
    }

    AppConfirmDialog {
        id: deleteGroupDialog
        parent: Overlay.overlay
        title: qsTr("删除名单")
        message: root.selectedRow >= 0
                 ? qsTr("将删除名单「%1」，实体上对它的引用会被清除。确定删除吗？")
                   .arg(groupModel.get(root.selectedRow, "name"))
                 : ""
        okText: qsTr("删除")
        danger: true
        onAccepted: {
            groupModel.removeRow(root.selectedRow)
            root.selectedRow = -1
            root.refreshCandidates()
        }
    }
}
