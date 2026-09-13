import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// 打印输出页：对象多选 + 选项 + 预览 + 打印/PDF/PNG/Excel 四通道
Item {
    id: root
    property int scopeIndex: 0 // 0班级 1老师 2教室 3总课表
    property var checkedIds: ({}) // {id: true}
    property int rev: 0
    property string statusText: ""

    readonly property var scopeKeys: ["class", "teacher", "room", "master"]
    readonly property var scopeNames: [qsTr("班级课表"), qsTr("老师任课表"), qsTr("教室使用表"), qsTr("总课表")]

    function optionsMap() {
        return {
            "landscape": orientCombo.currentIndex === 0,
            "tablesPerPage": perPageCombo.currentIndex + 1,
            "fontSize": fontStepper.value,
            "showTeachers": showTeachersSwitch.checked,
            "lunchLine": lunchSwitch.checked,
            "showLock": lockSwitch.checked,
            "headerNote": headerNoteField.text
        }
    }

    function persistOptions() {
        if (app.hasProject)
            app.printSettings = optionsMap()
    }

    function restoreOptions() {
        const s = app.printSettings
        if (!s || Object.keys(s).length === 0)
            return
        orientCombo.currentIndex = s.landscape === false ? 1 : 0
        perPageCombo.currentIndex = (s.tablesPerPage === 2) ? 1 : 0
        fontStepper.value = s.fontSize ?? 10
        showTeachersSwitch.checked = s.showTeachers !== false
        lunchSwitch.checked = s.lunchLine !== false
        lockSwitch.checked = s.showLock !== false
        headerNoteField.text = s.headerNote ?? ""
    }

    function selectedIds() {
        let ids = []
        for (const key of Object.keys(root.checkedIds)) {
            if (root.checkedIds[key] === true)
                ids.push(Number(key))
        }
        return ids
    }

    function currentListModel() {
        if (root.scopeIndex === 0) return classModel
        if (root.scopeIndex === 1) return teacherModel
        return classroomModel
    }

    function doExport(kind) {
        const ids = selectedIds()
        const scope = root.scopeKeys[root.scopeIndex]
        const opts = optionsMap()
        let ok = false
        if (kind === "print") {
            ok = printer.printNative(scope, ids, opts)
            root.statusText = ok ? qsTr("已发送到打印机") : ""
        } else if (kind === "pdf") {
            pdfDialog.open()
            return
        } else if (kind === "png") {
            pngDirDialog.open()
            return
        } else if (kind === "excel") {
            excelDialog.open()
            return
        }
    }

    onVisibleChanged: if (visible) { root.rev++; restoreOptions() }

    FileDialog {
        id: pdfDialog
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("PDF 文档 (*.pdf)")]
        onAccepted: {
            const ok = printer.exportPdf(selectedFile.toString(), root.scopeKeys[root.scopeIndex],
                                         root.selectedIds(), root.optionsMap())
            root.statusText = ok ? qsTr("✔ PDF 已导出") : qsTr("导出失败：%1").arg(printer.lastError())
            statusTimer.restart()
        }
    }
    FolderDialog {
        id: pngDirDialog
        onAccepted: {
            const ok = printer.exportPngs(selectedFolder.toString(), root.scopeKeys[root.scopeIndex],
                                          root.selectedIds(), root.optionsMap())
            root.statusText = ok ? qsTr("✔ PNG 已导出到所选目录") : qsTr("导出失败：%1").arg(printer.lastError())
            statusTimer.restart()
        }
    }
    FileDialog {
        id: excelDialog
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Excel 工作簿 (*.xlsx)")]
        onAccepted: {
            const ok = printer.exportExcel(selectedFile.toString(), root.scopeKeys[root.scopeIndex],
                                           root.selectedIds())
            root.statusText = ok ? qsTr("✔ Excel 已导出") : qsTr("导出失败：%1").arg(printer.lastError())
            statusTimer.restart()
        }
    }
    Timer {
        id: statusTimer
        interval: 4000
        onTriggered: root.statusText = ""
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingXL
        spacing: Theme.spacingM

        // 状态条
        Rectangle {
            Layout.fillWidth: true
            height: 34
            radius: Theme.radiusS
            visible: root.statusText.length > 0
            color: Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.12)
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingM
                text: root.statusText
                color: Theme.success
                font.pixelSize: Theme.fontCaption
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingL

            // 左：对象选择
            AppCard {
                Layout.preferredWidth: 336
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingM
                    spacing: Theme.spacingS

                    AppTabs {
                        Layout.fillWidth: true
                        stretch: true
                        model: root.scopeNames
                        currentIndex: root.scopeIndex
                        onTabClicked: function (index) {
                            root.scopeIndex = index
                            root.checkedIds = ({})
                        }
                    }

                    RowLayout {
                        visible: root.scopeIndex < 3
                        Layout.fillWidth: true
                        Text {
                            text: qsTr("选择输出对象")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                        }
                        Item { Layout.fillWidth: true }
                        AppButton {
                            text: qsTr("全选")
                            variant: "subtle"
                            onClicked: {
                                let m = {}
                                const model = root.currentListModel()
                                for (let i = 0; i < model.count; ++i)
                                    m[model.get(i, "id")] = true
                                root.checkedIds = m
                            }
                        }
                        AppButton {
                            text: qsTr("清空")
                            variant: "subtle"
                            onClicked: root.checkedIds = ({})
                        }
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: root.scopeIndex < 3
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        model: root.currentListModel()
                        spacing: 2
                        delegate: Rectangle {
                            id: pickRow
                            required property int index
                            width: ListView.view.width
                            height: 36
                            radius: Theme.radiusS
                            color: pickHover.containsMouse ? Theme.overlayHover : "transparent"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.spacingM
                                anchors.rightMargin: Theme.spacingM
                                Text {
                                    Layout.fillWidth: true
                                    text: root.currentListModel().get(pickRow.index, "name")
                                    color: Theme.text
                                    font.pixelSize: Theme.fontBody
                                }
                                AppSwitch {
                                    checked: root.checkedIds[root.currentListModel().get(pickRow.index, "id")] === true
                                    onToggled: {
                                        const m = root.checkedIds
                                        const id = root.currentListModel().get(pickRow.index, "id")
                                        if (checked) m[id] = true; else delete m[id]
                                        root.checkedIds = m
                                    }
                                }
                            }
                            MouseArea {
                                id: pickHover
                                anchors.fill: parent
                                hoverEnabled: true
                                propagateComposedEvents: true
                                onClicked: function (m) { m.accepted = false }
                            }
                        }
                    }
                    AppEmptyState {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: root.scopeIndex === 3
                        icon: "🗺️"
                        title: qsTr("总课表")
                        hint: qsTr("全校班级合成一张大表，自动分页")
                    }
                }
            }

            // 中：选项
            AppCard {
                Layout.preferredWidth: 320
                Layout.fillHeight: true
                Flickable {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingL
                    contentHeight: optColumn.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ColumnLayout {
                        id: optColumn
                        width: 320 - Theme.spacingL * 2
                        spacing: Theme.spacingM

                        Text {
                            text: qsTr("格式选项")
                            font.pixelSize: Theme.fontSubtitle
                            font.bold: true
                            color: Theme.text
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: Theme.spacingM
                            rowSpacing: Theme.spacingM

                            Text { text: qsTr("纸张方向"); color: Theme.text; font.pixelSize: Theme.fontBody }
                            AppComboBox {
                                id: orientCombo
                                Layout.fillWidth: true
                                model: [qsTr("横向"), qsTr("纵向")]
                                onActivated: root.persistOptions()
                            }
                            Text { text: qsTr("每页表数"); color: Theme.text; font.pixelSize: Theme.fontBody }
                            AppComboBox {
                                id: perPageCombo
                                Layout.fillWidth: true
                                model: ["1", "2"]
                                onActivated: root.persistOptions()
                            }
                            Text { text: qsTr("字号"); color: Theme.text; font.pixelSize: Theme.fontBody }
                            AppStepper {
                                id: fontStepper
                                from: 7; to: 18; value: 10
                                onValueModified: root.persistOptions()
                            }
                            Text { text: qsTr("含老师名"); color: Theme.text; font.pixelSize: Theme.fontBody }
                            AppSwitch {
                                id: showTeachersSwitch
                                checked: true
                                onToggled: { showTeachersSwitch.checked = checked; root.persistOptions() }
                            }
                            Text { text: qsTr("午休分隔行"); color: Theme.text; font.pixelSize: Theme.fontBody }
                            AppSwitch {
                                id: lunchSwitch
                                checked: true
                                onToggled: { lunchSwitch.checked = checked; root.persistOptions() }
                            }
                            Text { text: qsTr("预排锁定标记"); color: Theme.text; font.pixelSize: Theme.fontBody }
                            AppSwitch {
                                id: lockSwitch
                                checked: true
                                onToggled: { lockSwitch.checked = checked; root.persistOptions() }
                            }
                        }

                        Text { text: qsTr("表头说明文字"); color: Theme.text; font.pixelSize: Theme.fontBody }
                        AppTextField {
                            id: headerNoteField
                            Layout.fillWidth: true
                            placeholder: qsTr("例如：2025 春季学期")
                            showClear: false
                            onAccepted: root.persistOptions()
                            onTextChanged: root.persistOptions()
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                        AppButton {
                            Layout.fillWidth: true
                            text: qsTr("🖨️ 打印…")
                            variant: "primary"
                            enabled: app.hasSchedule
                            onClicked: root.doExport("print")
                        }
                        AppButton {
                            Layout.fillWidth: true
                            text: qsTr("📄 导出 PDF…")
                            enabled: app.hasSchedule
                            onClicked: root.doExport("pdf")
                        }
                        AppButton {
                            Layout.fillWidth: true
                            text: qsTr("🖼️ 导出 PNG…")
                            enabled: app.hasSchedule
                            onClicked: root.doExport("png")
                        }
                        AppButton {
                            Layout.fillWidth: true
                            text: qsTr("📊 导出 Excel…")
                            enabled: app.hasSchedule
                            onClicked: root.doExport("excel")
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: !app.hasSchedule
                            text: qsTr("尚未排课：请先前往「自动排课」页生成课表")
                            color: Theme.warning
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            // 右：预览
            AppCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingM
                    spacing: Theme.spacingS
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: qsTr("预览（第一页）")
                            font.pixelSize: Theme.fontBody
                            font.bold: true
                            color: Theme.text
                        }
                        Item { Layout.fillWidth: true }
                        AppButton {
                            text: qsTr("刷新预览")
                            enabled: app.hasSchedule
                            onClicked: {
                                const url = printer.renderPreview(root.scopeKeys[root.scopeIndex],
                                                                  root.selectedIds(), root.optionsMap())
                                if (url.length > 0)
                                    previewImage.source = url
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: Theme.radiusM
                        color: Theme.surfaceAlt
                        clip: true
                        Image {
                            id: previewImage
                            anchors.centerIn: parent
                            width: Math.min(parent.width - 8,
                                            (parent.height - 8) * sourceSize.width / Math.max(1, sourceSize.height))
                            height: Math.min(parent.height - 8,
                                             (parent.width - 8) * sourceSize.height / Math.max(1, sourceSize.width))
                            fillMode: Image.PreserveAspectFit
                            cache: false
                            smooth: true
                        }
                        AppEmptyState {
                            anchors.centerIn: parent
                            visible: previewImage.source == ""
                            icon: "🖨️"
                            title: qsTr("点击「刷新预览」查看效果")
                        }
                    }
                }
            }
        }
    }
}
