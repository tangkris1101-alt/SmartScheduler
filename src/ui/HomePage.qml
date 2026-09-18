import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// 首页：欢迎卡（新建/打开/继续上次）+ 最近文件 + 项目概览
Flickable {
    id: root
    contentHeight: column.implicitHeight + Theme.spacingXL * 2
    contentWidth: width
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    property bool openImportAfterCreate: false

    function navigateTo(page) {
        const w = ApplicationWindow.window
        if (w)
            w.currentPage = page
    }

    FileDialog {
        id: openDialog
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("SmartScheduler 项目 (*.spk)")]
        onAccepted: root.openProjectGuarded(selectedFile.toString())
    }

    // 脏项目守护：打开/继续上次前提示保存，避免未保存更改被静默丢弃
    function openProjectGuarded(path) {
        if (app.projectDirty) {
            guardDialog.pendingPath = path
            guardDialog.open()
        } else {
            app.openProject(path)
        }
    }

    AppConfirmDialog {
        id: guardDialog
        parent: Overlay.overlay
        property string pendingPath: ""
        title: qsTr("未保存的更改")
        message: qsTr("当前项目「%1」有未保存的更改，继续打开将丢弃这些更改。是否先保存？")
            .arg(app.schoolName)
        okText: qsTr("保存并继续")
        thirdText: qsTr("不保存继续")
        cancelText: qsTr("取消")
        onAccepted: {
            if (app.saveProject())
                app.openProject(pendingPath)
            else
                saveAsGuardDialog.open()
        }
        onThirdAction: app.openProject(pendingPath)
    }

    FileDialog {
        id: saveAsGuardDialog
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("SmartScheduler 项目 (*.spk)")]
        onAccepted: {
            if (app.saveProjectAs(selectedFile.toString()))
                app.openProject(guardDialog.pendingPath)
        }
    }

    ColumnLayout {
        id: column
        width: root.width - Theme.spacingXL * 2
        x: Theme.spacingXL
        y: Theme.spacingXL
        spacing: Theme.spacingL

        // 欢迎卡
        AppCard {
            Layout.fillWidth: true
            Layout.preferredHeight: welcomeColumn.implicitHeight + Theme.spacingXL * 2

            ColumnLayout {
                id: welcomeColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: Theme.spacingXL
                spacing: Theme.spacingS

                Text {
                    text: qsTr("欢迎使用 智排课系统")
                    font.pixelSize: Theme.fontBig
                    font.bold: true
                    color: Theme.text
                }
                Text {
                    text: qsTr("现代化自动排课：约束保证、质量可度量、结果可复现")
                    font.pixelSize: Theme.fontBody
                    color: Theme.textSecondary
                }
                RowLayout {
                    Layout.topMargin: Theme.spacingM
                    spacing: Theme.spacingS
                    AppButton {
                        text: qsTr("✚ 新建项目")
                        variant: "primary"
                        onClicked: newProjectDialog.open()
                    }
                    AppButton {
                        text: qsTr("📂 打开项目…")
                        onClicked: openDialog.open()
                    }
                    AppButton {
                        text: qsTr("⏱ 继续上次")
                        variant: "subtle"
                        enabled: app.recentFiles.length > 0
                        onClicked: root.openProjectGuarded(app.recentFiles[0])
                    }
                    AppButton {
                        text: qsTr("❓ 新手引导")
                        variant: "subtle"
                        onClicked: {
                            onboardingPageDialog.page = 0
                            onboardingPageDialog.open()
                        }
                    }
                }

                // 空项目快速上手引导
                Rectangle {
                    Layout.topMargin: Theme.spacingM
                    Layout.fillWidth: true
                    height: quickRow.implicitHeight + Theme.spacingM * 2
                    radius: Theme.radiusM
                    visible: !app.hasProject
                    color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.08)
                    border.color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.35)

                    RowLayout {
                        id: quickRow
                        anchors.fill: parent
                        anchors.margins: Theme.spacingM
                        spacing: Theme.spacingM
                        Text {
                            text: "🚀"
                            font.pixelSize: Theme.fontTitle
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("3 分钟上手：创建示例学校立即体验完整流程；或先新建项目，再用 Excel 模板批量导入你的数据")
                            color: Theme.text
                            font.pixelSize: Theme.fontBody
                            wrapMode: Text.WordWrap
                        }
                        AppButton {
                            text: qsTr("🎓 创建示例学校")
                            variant: "primary"
                            onClicked: app.createSampleSchool(true)
                        }
                        AppButton {
                            text: qsTr("📥 Excel 导入")
                            onClicked: {
                                if (app.hasProject)
                                    homeImportDialog.open()
                                else
                                    importGuardDialog.open()
                            }
                        }
                    }
                }
            }
        }

        // 项目概览
        GridLayout {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: Theme.spacingM
            rowSpacing: Theme.spacingM
            visible: app.hasProject

            Repeater {
                model: [
                    { icon: "🏫", label: qsTr("班级"), value: app.classCount },
                    { icon: "📚", label: qsTr("课程"), value: app.courseCount },
                    { icon: "👩‍🏫", label: qsTr("老师"), value: app.teacherCount },
                    { icon: "📋", label: qsTr("计划条目"), value: app.planEntryCount }
                ]
                delegate: AppCard {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 110

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingL
                        spacing: Theme.spacingXS
                        Text {
                            text: modelData.icon + "  " + modelData.label
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                        }
                        Text {
                            text: modelData.value
                            color: Theme.text
                            font.pixelSize: Theme.fontTitle
                            font.bold: true
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.navigateTo(1)
                    }
                }
            }
        }

        // 最近文件
        AppCard {
            Layout.fillWidth: true
            Layout.preferredHeight: recentColumn.implicitHeight + Theme.spacingL * 2
            visible: app.recentFiles.length > 0

            ColumnLayout {
                id: recentColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingL
                spacing: Theme.spacingXS

                Text {
                    text: qsTr("最近打开")
                    font.pixelSize: Theme.fontSubtitle
                    font.bold: true
                    color: Theme.text
                }
                Repeater {
                    model: app.recentFiles
                    delegate: Rectangle {
                        required property string modelData
                        required property int index
                        Layout.fillWidth: true
                        height: 36
                        radius: Theme.radiusM
                        color: recentHover.containsMouse ? Theme.overlayHover : "transparent"
                        Behavior on color { ColorAnimation { duration: 100 } }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingM
                            anchors.rightMargin: Theme.spacingM
                            Text {
                                text: "📄"
                                font.pixelSize: Theme.fontBody
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData
                                color: Theme.text
                                font.pixelSize: Theme.fontBody
                                elide: Text.ElideMiddle
                            }
                            Text {
                                visible: index === 0
                                text: qsTr("最近")
                                color: Theme.primary
                                font.pixelSize: Theme.fontCaption
                            }
                        }
                        MouseArea {
                            id: recentHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.openProjectGuarded(modelData)
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    NewProjectDialog {
        id: newProjectDialog
        parent: Overlay.overlay
        onCreated: {
            root.navigateTo(1)
            if (root.openImportAfterCreate) {
                root.openImportAfterCreate = false
                homeImportDialog.open()
            }
        }
    }

    // 未建项目时点 Excel 导入：说明前提并引导新建，创建后直接接续导入
    AppConfirmDialog {
        id: importGuardDialog
        parent: Overlay.overlay
        title: qsTr("还没有打开的项目")
        message: qsTr("Excel 导入会把表格数据灌入当前项目。请先新建一个项目（可选小学/初中/高中模板起步），创建完成后即可导入。")
        okText: qsTr("新建项目并导入")
        onAccepted: {
            root.openImportAfterCreate = true
            newProjectDialog.open()
        }
    }

    ImportExcelDialog {
        id: homeImportDialog
        parent: Overlay.overlay
    }

    OnboardingDialog {
        id: onboardingPageDialog
        parent: Overlay.overlay
    }
}
