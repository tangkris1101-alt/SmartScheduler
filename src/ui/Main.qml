import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// 应用外壳：顶部工具栏 + 左侧导航 + 页面区（淡入/滑动过渡）
ApplicationWindow {
    id: win
    visible: true
    width: 1240
    height: 780
    minimumWidth: 1024
    minimumHeight: 680
    title: qsTr("智排课系统 SmartScheduler")
    color: Theme.background

    property int currentPage: 0

    readonly property var navModel: [
        { icon: "🏠", label: qsTr("首页") },
        { icon: "📋", label: qsTr("名单管理") },
        { icon: "🏷️", label: qsTr("归类分组") },
        { icon: "⚙️", label: qsTr("课程设置") },
        { icon: "📅", label: qsTr("教学计划") },
        { icon: "🚫", label: qsTr("限制条件") },
        { icon: "▶️", label: qsTr("自动排课") },
        { icon: "📊", label: qsTr("课表查看") },
        { icon: "🖨️", label: qsTr("打印输出") }
    ]

    // ---- 页面组件（StackLayout 顺序与导航一致）----
    Component { id: homeComp; HomePage {} }
    Component { id: rosterComp; RosterPage {} }
    Component { id: groupsComp; GroupsPage {} }
    Component { id: coursesComp; CourseFeaturesPage {} }
    Component { id: planComp; PlanEditorPage {} }
    Component { id: limitComp; LimitsPage {} }
    Component { id: engineComp; SchedulerPage {} }
    Component { id: tableComp; TimetablePage {} }
    Component { id: printComp; PrintPage {} }
    readonly property var pageComponents: [homeComp, rosterComp, groupsComp, coursesComp,
                                           planComp, limitComp, engineComp, tableComp, printComp]

    // ---- 关闭确认 ----
    onClosing: function (close) {
        if (app.projectDirty) {
            close.accepted = false
            closeDialog.open()
        }
    }

    FileDialog {
        id: saveAsDialog
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("SmartScheduler 项目 (*.spk)")]
        property bool quitAfterSave: false
        onAccepted: {
            if (app.saveProjectAs(selectedFile.toString()) && quitAfterSave)
                Qt.quit()
        }
    }

    AppConfirmDialog {
        id: closeDialog
        parent: Overlay.overlay
        title: qsTr("未保存的更改")
        message: qsTr("当前项目「%1」有未保存的更改。退出前是否保存？").arg(app.schoolName)
        okText: qsTr("保存并退出")
        thirdText: qsTr("不保存退出")
        cancelText: qsTr("取消")
        onAccepted: {
            if (app.saveProject())
                Qt.quit()
            else {
                saveAsDialog.quitAfterSave = true
                saveAsDialog.open()
            }
        }
        onThirdAction: Qt.quit()
    }

    // 全局撤销/重做快捷键
    Shortcut { sequence: "Ctrl+Z"; onActivated: adjust.undo() }
    Shortcut { sequence: "Ctrl+Y"; onActivated: adjust.redo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; onActivated: adjust.redo() }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- 顶部工具栏 ----
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.headerHeight
            color: Theme.surface
            Behavior on color { ColorAnimation { duration: 150 } }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingL
                anchors.rightMargin: Theme.spacingL
                spacing: Theme.spacingM

                Text {
                    text: "🗓️"
                    font.pixelSize: Theme.fontTitle
                }
                Text {
                    text: qsTr("智排课系统")
                    font.pixelSize: Theme.fontSubtitle
                    font.bold: true
                    color: Theme.text
                }

                Rectangle {
                    Layout.leftMargin: Theme.spacingM
                    width: 1
                    height: 24
                    color: Theme.border
                    visible: app.hasProject
                }
                Text {
                    visible: app.hasProject
                    text: app.schoolName
                    font.pixelSize: Theme.fontBody
                    color: Theme.textSecondary
                    elide: Text.ElideRight
                    Layout.maximumWidth: 260
                }
                Rectangle {
                    visible: app.hasProject && app.projectDirty
                    width: 8
                    height: 8
                    radius: 4
                    color: Theme.warning
                    ToolTip.visible: dirtyHover.containsMouse
                    ToolTip.text: qsTr("有未保存的更改")
                    MouseArea { id: dirtyHover; anchors.fill: parent; hoverEnabled: true }
                }

                Item { Layout.fillWidth: true }

                // 排课运行指示
                RowLayout {
                    visible: app.schedulerRunning
                    spacing: Theme.spacingXS
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: Theme.primary
                        SequentialAnimation on opacity {
                            running: app.schedulerRunning
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.3; duration: 600 }
                            NumberAnimation { to: 1; duration: 600 }
                        }
                    }
                    Text {
                        text: qsTr("排课中 %1%").arg(app.schedulerProgress)
                        color: Theme.primary
                        font.pixelSize: Theme.fontCaption
                    }
                }

                AppButton {
                    text: "↶"
                    variant: "subtle"
                    enabled: adjust.canUndo
                    onClicked: adjust.undo()
                }
                AppButton {
                    text: "↷"
                    variant: "subtle"
                    enabled: adjust.canRedo
                    onClicked: adjust.redo()
                }
                AppButton {
                    text: qsTr("💾 保存")
                    variant: "subtle"
                    enabled: app.hasProject
                    onClicked: {
                        if (!app.saveProject()) {
                            saveAsDialog.quitAfterSave = false
                            saveAsDialog.open()
                        }
                    }
                }
                AppButton {
                    text: Theme.dark ? "☀️" : "🌙"
                    variant: "subtle"
                    onClicked: Theme.dark = !Theme.dark
                }
            }
        }

        // ---- 主体：导航 + 页面区 ----
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // 左侧导航
            Rectangle {
                Layout.preferredWidth: Theme.navWidth
                Layout.fillHeight: true
                color: Theme.sidebar
                Behavior on color { ColorAnimation { duration: 150 } }

                Rectangle {
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    width: 1
                    color: Theme.border
                }

                ListView {
                    id: nav
                    anchors.fill: parent
                    anchors.margins: Theme.spacingS
                    model: win.navModel
                    currentIndex: win.currentPage
                    clip: true
                    spacing: 2

                    highlight: Rectangle {
                        radius: Theme.radiusM
                        color: Theme.selectedBg
                        Behavior on color { ColorAnimation { duration: 150 } }
                    }
                    highlightMoveDuration: 160
                    highlightResizeDuration: 0

                    delegate: Item {
                        id: navItem
                        required property int index
                        required property var modelData
                        width: nav.width
                        height: 44

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusM
                            visible: navItem.index !== win.currentPage
                            color: navHover.containsMouse ? Theme.overlayHover : "transparent"
                            Behavior on color { ColorAnimation { duration: 100 } }
                        }
                        Rectangle {
                            visible: navItem.index === win.currentPage
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 3
                            height: 20
                            radius: 1.5
                            color: Theme.primary
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingM
                            spacing: Theme.spacingM
                            Text {
                                text: navItem.modelData.icon
                                font.pixelSize: Theme.fontSubtitle
                            }
                            Text {
                                text: navItem.modelData.label
                                font.pixelSize: Theme.fontBody
                                font.bold: navItem.index === win.currentPage
                                color: navItem.index === win.currentPage ? Theme.primary : Theme.text
                                Behavior on color { ColorAnimation { duration: 120 } }
                            }
                        }
                        MouseArea {
                            id: navHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: win.currentPage = navItem.index
                        }
                    }
                }
            }

            // 页面区
            StackLayout {
                id: stack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: win.currentPage

                Repeater {
                    model: win.pageComponents.length
                    delegate: Item {
                        id: cell
                        required property int index
                        opacity: StackLayout.isCurrentItem ? 1 : 0
                        Behavior on opacity {
                            NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
                        }

                        Loader {
                            anchors.fill: parent
                            x: cell.StackLayout.isCurrentItem ? 0 : 28
                            Behavior on x {
                                NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
                            }
                            sourceComponent: win.pageComponents[cell.index]
                            active: true
                        }
                    }
                }
            }
        }
    }
}
