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
    // 标题栏：项目名 + 未保存星号
    title: (app.projectDirty ? "* " : "")
           + (app.hasProject ? app.schoolName + " — " : "")
           + qsTr("智排课系统 SmartScheduler")
    color: Theme.background

    property int currentPage: 0

    // 导航模型：按工作流分三组，step>0 的项显示步骤序号徽标，完成时变绿勾
    readonly property var navModel: [
        { page: 0, group: "_top",    icon: "🏠", label: qsTr("首页"),     step: 0 },
        { page: 1, group: "_prep",   icon: "📋", label: qsTr("名单管理"), step: 1 },
        { page: 2, group: "_prep",   icon: "🏷️", label: qsTr("归类分组"), step: 2 },
        { page: 3, group: "_prep",   icon: "⚙️", label: qsTr("课程设置"), step: 3 },
        { page: 4, group: "_plan",   icon: "📅", label: qsTr("教学计划"), step: 4 },
        { page: 5, group: "_plan",   icon: "🚫", label: qsTr("限制条件"), step: 5 },
        { page: 6, group: "_output", icon: "▶️", label: qsTr("自动排课"), step: 6 },
        { page: 7, group: "_output", icon: "📊", label: qsTr("课表查看"), step: 0 },
        { page: 8, group: "_output", icon: "🖨️", label: qsTr("打印输出"), step: 0 },
        { page: 9, group: "_output", icon: "📈", label: qsTr("质量报告"), step: 0 },
        { page: 10, group: "_output", icon: "🤖", label: qsTr("AI 助手"), step: 0 },
        { page: 11, group: "_bottom", icon: "🛠️", label: qsTr("设置"),     step: 0 }
    ]

    function navGroupTitle(group) {
        if (group === "_prep")   return qsTr("数据准备")
        if (group === "_plan")   return qsTr("计划与约束")
        if (group === "_output") return qsTr("排课与输出")
        return ""
    }

    // 未打开项目时只保留「首页」与「设置」，其余流程项折叠
    readonly property var visibleNavModel: {
        if (app.hasProject)
            return navModel
        const out = []
        for (const item of navModel) {
            if (item.page === 0 || item.page === 11)
                out.push(item)
        }
        return out
    }
    function navIndexForPage(page) {
        const m = visibleNavModel
        for (let i = 0; i < m.length; ++i) {
            if (m[i].page === page)
                return i
        }
        return -1
    }

    // 关闭项目后如果当前停在被折叠的页面，回到首页
    Connections {
        target: app
        function onProjectChanged() {
            if (!app.hasProject && win.currentPage !== 0 && win.currentPage !== 11)
                win.currentPage = 0
        }
    }

    // 步骤完成判定：用真实数据状态驱动徽标变勾
    function stepDone(page) {
        if (page === 1) return app.classCount > 0
        if (page === 2) return groupModel.count > 0
        if (page === 3) return app.courseCount > 0 && app.teacherCount > 0 // 课程设置：有课程和老师才可配置特征
        if (page === 4) return app.planEntryCount > 0
        if (page === 5) return app.hasProject && app.constraintCount > 0 // hasProject 让项目切换时重算
        if (page === 6) return app.hasSchedule // 只有「自动排课」是流程步骤；课表/打印/报告是看结果的地方
        return false
    }

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
    Component { id: qualityComp; QualityReportPage {} }
    Component { id: aiComp; AiPage {} }
    Component { id: settingsComp; SettingsPage {} }
    readonly property var pageComponents: [homeComp, rosterComp, groupsComp, coursesComp,
                                           planComp, limitComp, engineComp, tableComp, printComp,
                                           qualityComp, aiComp, settingsComp]

    // ---- 关闭确认 ----
    // Qt.quit() 会再次触发窗口关闭事件 → 本守卫会把退出也拦下来；
    // 「不保存退出」需先置 skipCloseGuard 放行
    property bool skipCloseGuard: false
    onClosing: function (close) {
        if (app.projectDirty && !win.skipCloseGuard) {
            close.accepted = false
            closeDialog.open()
        }
    }

    FileDialog {
        id: saveAsDialog
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("SmartScheduler 项目 (*.spk)")]
        property bool quitAfterSave: false
        property bool installAfterSave: false
        onAccepted: {
            if (app.saveProjectAs(selectedFile.toString())) {
                if (installAfterSave) {
                    installAfterSave = false
                    app.installUpdate()
                } else if (quitAfterSave) {
                    Qt.quit()
                }
            }
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
        onThirdAction: {
            win.skipCloseGuard = true // 用户明确放弃修改，放行关闭事件
            Qt.quit()
        }
    }

    // 顶栏「新版本已就绪」的安装确认：先保存未保存的项目，再静默安装并退出
    AppConfirmDialog {
        id: installConfirmDialog
        parent: Overlay.overlay
        title: qsTr("安装新版本 %1").arg(app.latestVersion)
        message: qsTr("将关闭智排课系统并自动安装 %1，完成后会自动启动新版本。")
                     .arg(app.latestVersion)
                 + (app.projectDirty ? qsTr("\n当前项目有未保存更改，将先为你保存。") : "")
        okText: qsTr("开始安装")
        onAccepted: {
            if (app.projectDirty && !app.saveProject()) {
                saveAsDialog.quitAfterSave = false
                saveAsDialog.installAfterSave = true
                saveAsDialog.open()
            } else {
                app.installUpdate()
            }
        }
    }

    // 首次启动显示新手引导；内测门禁锁着时先把门禁弹窗顶上来
    Component.onCompleted: {
        if (app.betaLocked)
            betaGateDialog.open()
        if (!app.onboardingShown())
            onboardingDialog.open()
    }

    OnboardingDialog {
        id: onboardingDialog
        parent: Overlay.overlay
    }

    // 内测门禁：app.betaLocked 时挡住主界面。
    // 注意不能写 visible: app.betaLocked —— Popup 的 visible 是开关语义，绑定会自反馈成死循环；
    // 改为监听状态变化调 open/close，并在窗口建好后补一次初始状态（initBetaGate 早于 QML 加载）。
    BetaGateDialog {
        id: betaGateDialog
        parent: Overlay.overlay
    }
    Connections {
        target: app
        function onBetaStateChanged() {
            if (app.betaLocked)
                betaGateDialog.open()
            else
                betaGateDialog.close()
        }
    }

    FeedbackDialog {
        id: feedbackDialog
        parent: Overlay.overlay
    }

    // 全局撤销/重做快捷键
    Shortcut { sequence: "Ctrl+Z"; onActivated: adjust.undo() }
    Shortcut { sequence: "Ctrl+Y"; onActivated: adjust.redo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; onActivated: adjust.redo() }

    // 点击窗口空白处让输入框失焦（配合 AppTextField.commitOnFocusLoss 触发「点击别处确认」）；
    // 置于最底层，只接收未被按钮/列表等交互控件消费的点击
    MouseArea {
        anchors.fill: parent
        onClicked: shell.forceActiveFocus()
    }

    ColumnLayout {
        id: shell
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

                // 新版本已就绪：一键安装（唯一需要用户确认的环节）
                AppButton {
                    visible: app.updateReady
                    variant: "primary"
                    text: qsTr("🎉 新版本 %1 已就绪，安装").arg(app.latestVersion)
                    onClicked: installConfirmDialog.open()
                }

                AppButton {
                    text: qsTr("🐞 反馈")
                    variant: "subtle"
                    onClicked: feedbackDialog.open()
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

                // 折叠提示：没打开项目时说明其余入口去哪了
                Text {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: Theme.spacingM
                    visible: !app.hasProject
                    text: qsTr("新建或打开项目后，这里会显示完整流程（名单 → 计划 → 排课 → 课表 → 报告）")
                    wrapMode: Text.WordWrap
                    color: Theme.textDisabled
                    font.pixelSize: Theme.fontCaption
                }

                ListView {
                    id: nav
                    anchors.fill: parent
                    anchors.margins: Theme.spacingS
                    anchors.bottomMargin: app.hasProject ? Theme.spacingS : 72
                    model: win.visibleNavModel
                    currentIndex: win.navIndexForPage(win.currentPage)
                    clip: true
                    spacing: 2

                    // 分组小标题（_top/_bottom 为占位组，不显示标题）
                    section.property: "group"
                    section.criteria: ViewSection.FullString
                    section.delegate: Item {
                        width: nav.width
                        height: win.navGroupTitle(section).length > 0 ? 32 : 0
                        Text {
                            visible: win.navGroupTitle(section).length > 0
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spacingM
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 4
                            text: win.navGroupTitle(section)
                            color: Theme.textDisabled
                            font.pixelSize: Theme.fontCaption
                            font.bold: true
                        }
                    }

                    highlight: Rectangle {
                        radius: Theme.radiusM
                        color: Theme.selectedBg
                        Behavior on color { ColorAnimation { duration: 150 } }
                    }
                    highlightMoveDuration: 160
                    highlightResizeDuration: 0

                    delegate: Item {
                        id: navItem
                        objectName: "navItem"
                        required property int index
                        required property var modelData
                        property int page: modelData.page
                        width: nav.width
                        height: 44
                        activeFocusOnTab: true

                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.label
                            + (modelData.step > 0 ? qsTr("，第 %1 步").arg(modelData.step) : "")
                            + (modelData.step > 0 && win.stepDone(modelData.page) ? qsTr("，已完成") : "")
                        Accessible.onPressAction: win.currentPage = navItem.page

                        Keys.onSpacePressed: win.currentPage = navItem.page
                        Keys.onReturnPressed: win.currentPage = navItem.page
                        Keys.onEnterPressed: win.currentPage = navItem.page

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusM
                            visible: navItem.page !== win.currentPage
                            color: navHover.containsMouse ? Theme.overlayHover : "transparent"
                            Behavior on color { ColorAnimation { duration: 100 } }
                        }
                        Rectangle {
                            visible: navItem.page === win.currentPage
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 3
                            height: 20
                            radius: 1.5
                            color: Theme.primary
                        }
                        // 键盘焦点环
                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusM
                            visible: navItem.activeFocus
                            color: "transparent"
                            border.width: 2
                            border.color: Theme.primary
                        }
                        // 步骤序号徽标：未完成灰圈数字，完成绿底白勾
                        Rectangle {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spacingM - 2
                            anchors.verticalCenter: parent.verticalCenter
                            visible: navItem.modelData.step > 0
                            width: 18
                            height: 18
                            radius: 9
                            property bool done: win.stepDone(navItem.modelData.page)
                            color: done ? Theme.success : "transparent"
                            border.color: done ? Theme.success : Theme.textDisabled
                            border.width: 1.5
                            Text {
                                anchors.centerIn: parent
                                text: parent.done ? "✓" : String(navItem.modelData.step)
                                color: parent.done ? Theme.onPrimary : Theme.textDisabled
                                font.pixelSize: 10
                                font.bold: true
                            }
                        }
                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spacingM + 4
                                                + (navItem.modelData.step > 0 ? 20 : 0)
                            anchors.verticalCenter: parent.verticalCenter
                            text: navItem.modelData.icon
                            font.pixelSize: Theme.fontSubtitle
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.verticalCenter: parent.verticalCenter
                            text: navItem.modelData.label
                            font.pixelSize: Theme.fontBody
                            font.bold: navItem.page === win.currentPage
                            color: navItem.page === win.currentPage ? Theme.primary : Theme.text
                            Behavior on color { ColorAnimation { duration: 120 } }
                        }
                        MouseArea {
                            id: navHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: win.currentPage = navItem.page
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
