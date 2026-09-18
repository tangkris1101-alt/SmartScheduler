import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// Excel 批量导入对话框：选文件 → 预览报告 → 确认执行
Popup {
    id: root
    property var report: null
    property string filePath: ""

    signal importFinished()

    modal: true
    anchors.centerIn: parent
    width: 520
    padding: 0

    background: Rectangle { radius: Theme.radiusL; color: Theme.surface; border.color: Theme.border }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 }
        NumberAnimation { property: "scale"; from: 0.92; to: 1; duration: 140; easing.type: Easing.OutCubic }
    }
    Overlay.modal: Rectangle { color: Theme.dark ? Qt.rgba(0,0,0,0.55) : Qt.rgba(15,23,42,0.28) }

    onOpened: {
        root.report = null
        root.filePath = ""
    }
    onClosed: app.excelImportCancel()

    FileDialog {
        id: importFileDialog
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Excel 工作簿 (*.xlsx)")]
        onAccepted: {
            root.filePath = selectedFile.toString()
            root.report = app.excelImportPreview(selectedFile.toString(), overwriteCombo.currentIndex === 1)
        }
    }
    FileDialog {
        id: templateDialog
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Excel 工作簿 (*.xlsx)")]
        onAccepted: app.writeImportTemplate(selectedFile.toString())
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingM

        Text {
            Layout.topMargin: Theme.spacingL; Layout.leftMargin: Theme.spacingXL
            text: qsTr("从 Excel 批量导入")
            font.pixelSize: Theme.fontSubtitle; font.bold: true; color: Theme.text
        }
        Text {
            Layout.leftMargin: Theme.spacingXL; Layout.rightMargin: Theme.spacingXL
            Layout.fillWidth: true
            text: qsTr("按模板填写 5 张工作表（班级/课程/老师/教室/教学计划），按名称匹配已有数据。")
            color: Theme.textSecondary; font.pixelSize: Theme.fontCaption; wrapMode: Text.WordWrap
        }

        // 字段对照说明（可展开）
        ColumnLayout {
            id: fieldHelp
            Layout.leftMargin: Theme.spacingXL; Layout.rightMargin: Theme.spacingXL
            Layout.fillWidth: true
            spacing: 4
            property bool expanded: false

            RowLayout {
                spacing: 4
                Text {
                    text: fieldHelp.expanded ? "▾" : "▸"
                    color: Theme.primary; font.pixelSize: Theme.fontCaption
                }
                Text {
                    text: qsTr("字段对照说明（* 为必填，留空取默认）")
                    color: Theme.primary; font.pixelSize: Theme.fontCaption
                }
                Item { Layout.fillWidth: true }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: fieldHelp.expanded = !fieldHelp.expanded
                }
            }
            Flickable {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(helpText.implicitHeight, 170)
                visible: fieldHelp.expanded
                contentHeight: helpText.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Text {
                    id: helpText
                    width: fieldHelp.width
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textSecondary
                    lineHeight: 1.35
                    text: qsTr("【班级】名称*、全称、简称、拼音码、优先次序、年级、备注\n"
                             + "【课程】名称*、拼音码、教研组、主课(是/否)、理科(是/否)、平均分布(是/否)、进度一致班级数、上下午(自动/优先上午/优先下午)、备注\n"
                             + "【老师】姓名*、拼音码、教研组、日最多课(空=不限)、互斥组(同组同时只上一人)、备注\n"
                             + "【教室】名称*、容量(同时容纳班级数)、拼音码、备注\n"
                             + "【教学计划】班级*、课程*、任课老师(顿号分隔)、周节数*、连课次数、连课节数、上午节数(空=自动)、合班(是/否)、教室、开课周、结束周(空=学期末)、进度组")
                }
            }
        }

        RowLayout {
            Layout.leftMargin: Theme.spacingXL; Layout.rightMargin: Theme.spacingXL
            spacing: Theme.spacingS
            AppButton {
                text: qsTr("📄 下载导入模板")
                onClicked: templateDialog.open()
            }
            Item { Layout.fillWidth: true }
            Text { text: qsTr("重复条目"); color: Theme.text; font.pixelSize: Theme.fontBody }
            AppComboBox {
                id: overwriteCombo
                Layout.preferredWidth: 120
                model: [qsTr("跳过"), qsTr("覆盖更新")]
                onActivated: {
                    // 切换策略后需重新预览
                    if (root.filePath.length > 0)
                        root.report = app.excelImportPreview(root.filePath, index === 1)
                }
            }
        }

        AppButton {
            Layout.leftMargin: Theme.spacingXL; Layout.rightMargin: Theme.spacingXL
            Layout.fillWidth: true
            text: root.filePath.length === 0 ? qsTr("📂 选择 Excel 文件…") : qsTr("📂 重新选择文件")
            variant: "primary"
            onClicked: importFileDialog.open()
        }

        // 预览报告
        ColumnLayout {
            Layout.leftMargin: Theme.spacingXL; Layout.rightMargin: Theme.spacingXL
            Layout.fillWidth: true
            spacing: Theme.spacingS
            visible: root.report !== null

            RowLayout {
                spacing: Theme.spacingL
                Text {
                    text: qsTr("新建 %1").arg(root.report ? root.report.created : 0)
                    color: Theme.success; font.pixelSize: Theme.fontBody; font.bold: true
                }
                Text {
                    text: qsTr("更新 %1").arg(root.report ? root.report.updated : 0)
                    color: Theme.primary; font.pixelSize: Theme.fontBody; font.bold: true
                }
                Text {
                    text: qsTr("跳过 %1").arg(root.report ? root.report.skipped : 0)
                    color: Theme.textSecondary; font.pixelSize: Theme.fontBody
                }
                Text {
                    visible: root.report && root.report.errors.length > 0
                    text: qsTr("错误 %1 行").arg(root.report ? root.report.errors.length : 0)
                    color: Theme.danger; font.pixelSize: Theme.fontBody; font.bold: true
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(errList.contentHeight + 8, 140)
                visible: root.report && root.report.errors.length > 0
                radius: Theme.radiusS
                color: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.08)
                ListView {
                    id: errList
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true
                    model: root.report ? root.report.errors : []
                    delegate: Text {
                        required property string modelData
                        width: errList.width - 8
                        text: modelData
                        color: Theme.danger
                        font.pixelSize: Theme.fontCaption
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingS; Layout.bottomMargin: Theme.spacingL
            Layout.leftMargin: Theme.spacingXL; Layout.rightMargin: Theme.spacingXL
            spacing: Theme.spacingS
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("取消")
                onClicked: root.close()
            }
            AppButton {
                text: qsTr("✔ 确认导入")
                variant: "primary"
                enabled: root.report !== null && !root.report.fatal
                         && (root.report.created + root.report.updated) > 0
                onClicked: {
                    app.excelImportConfirm()
                    root.close()
                    root.importFinished()
                }
            }
        }
    }
}
