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

    function navigateTo(page) {
        const w = ApplicationWindow.window
        if (w)
            w.currentPage = page
    }

    FileDialog {
        id: openDialog
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("SmartScheduler 项目 (*.spk)")]
        onAccepted: app.openProject(selectedFile.toString())
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
            Layout.preferredHeight: 200

            ColumnLayout {
                anchors.left: parent.left
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
                        onClicked: app.openProject(app.recentFiles[0])
                    }
                    AppButton {
                        text: qsTr("📥 Excel 导入")
                        variant: "subtle"
                        enabled: app.hasProject
                        onClicked: homeImportDialog.open()
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
                            onClicked: app.openProject(modelData)
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
        onCreated: root.navigateTo(1)
    }

    ImportExcelDialog {
        id: homeImportDialog
        parent: Overlay.overlay
    }
}
