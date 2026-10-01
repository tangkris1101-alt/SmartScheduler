import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// 设置页：外观 / 检查更新 / 错误日志 / 关于
Item {
    id: root

    property string exportMsg: ""

    function updateStatusText() {
        switch (app.updateStatus) {
        case 1: return qsTr("正在检查更新…")
        case 2: return app.updateMessage !== ""
                       ? app.updateMessage
                       : qsTr("✓ 当前已是最新版本（v%1）").arg(app.appVersion)
        case 3: return app.updateMessage !== "" ? app.updateMessage
                                                : qsTr("发现新版本 %1，正在自动下载…").arg(app.latestVersion)
        case 4: return app.updateMessage
        default: return qsTr("启动时与每 5 分钟自动检查一次；发现新版本会自动下载，安装需你确认")
        }
    }
    function updateStatusColor() {
        switch (app.updateStatus) {
        case 2: return Theme.success
        case 3: return Theme.warning
        case 4: return Theme.danger
        default: return Theme.textSecondary
        }
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.y + column.implicitHeight + Theme.spacingXL
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: column
            x: Theme.spacingXL
            y: Theme.spacingXL
            width: parent.width - Theme.spacingXL * 2
            spacing: Theme.spacingL

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXS
                Text {
                    text: qsTr("设置")
                    font.pixelSize: Theme.fontBig
                    font.bold: true
                    color: Theme.text
                }
                Text {
                    text: qsTr("外观、更新与诊断")
                    font.pixelSize: Theme.fontBody
                    color: Theme.textSecondary
                }
            }

            // ---- 外观 ----
            AppCard {
                Layout.fillWidth: true
                Layout.preferredHeight: appearanceCol.implicitHeight + Theme.spacingL * 2

                ColumnLayout {
                    id: appearanceCol
                    anchors.fill: parent
                    anchors.margins: Theme.spacingL
                    spacing: Theme.spacingM

                    Text {
                        text: qsTr("外观")
                        font.pixelSize: Theme.fontSubtitle
                        font.bold: true
                        color: Theme.text
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingM
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: qsTr("深色模式")
                                font.pixelSize: Theme.fontBody
                                color: Theme.text
                            }
                            Text {
                                text: qsTr("切换后立即生效，并自动记住选择")
                                font.pixelSize: Theme.fontCaption
                                color: Theme.textSecondary
                            }
                        }
                        AppSwitch {
                            checked: Theme.dark
                            onToggled: function (checked) { Theme.dark = checked }
                        }
                    }
                }
            }

            // ---- 检查更新 ----
            AppCard {
                Layout.fillWidth: true
                Layout.preferredHeight: updateCol.implicitHeight + Theme.spacingL * 2

                ColumnLayout {
                    id: updateCol
                    anchors.fill: parent
                    anchors.margins: Theme.spacingL
                    spacing: Theme.spacingM

                    Text {
                        text: qsTr("检查更新")
                        font.pixelSize: Theme.fontSubtitle
                        font.bold: true
                        color: Theme.text
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.updateStatusText()
                        color: root.updateStatusColor()
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingM
                        AppButton {
                            text: app.updateStatus === 1 ? qsTr("检查中…") : qsTr("立即检查")
                            variant: app.updateStatus === 3 ? "default" : "primary"
                            enabled: app.updateStatus !== 1
                            onClicked: app.checkUpdate()
                        }
                        AppButton {
                            visible: app.updateDlState === 2
                            variant: "primary"
                            text: qsTr("✔ 安装更新")
                            onClicked: installConfirm.open()
                        }
                        AppButton {
                            visible: app.updateStatus === 3 && app.updateDlState !== 1 && app.updateDlState !== 2
                            text: qsTr("⬇ 重新下载")
                            onClicked: app.downloadUpdate()
                        }
                        AppButton {
                            visible: app.updateStatus === 3 && app.updateDlState !== 2
                            text: qsTr("浏览器下载")
                            onClicked: app.openMirrorDownload()
                        }
                        AppButton {
                            visible: app.updateStatus === 4
                            text: qsTr("打开发布页")
                            onClicked: app.openReleasesPage()
                        }
                        Item { Layout.fillWidth: true }
                    }

                    // 下载进度（镜像加速通道）
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXS
                        visible: app.updateDlState === 1
                        Rectangle {
                            Layout.fillWidth: true
                            height: 8
                            radius: 4
                            color: Theme.surfaceAlt
                            Rectangle {
                                height: parent.height
                                radius: 4
                                color: Theme.primary
                                width: parent.width * app.updateDlPercent / 100
                                Behavior on width { NumberAnimation { duration: 150 } }
                            }
                        }
                        Text {
                            text: qsTr("正在通过镜像加速下载安装包 %1% …").arg(app.updateDlPercent)
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: app.updateDlState === 2
                        text: qsTr("✓ 新版本 %1 已自动下载并校验通过。点击「安装更新」后将关闭本程序并自动完成安装。")
                              .arg(app.latestVersion)
                        color: Theme.success
                        font.pixelSize: Theme.fontCaption
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: app.updateDlState === 3
                        text: app.updateDlMessage
                        color: Theme.danger
                        font.pixelSize: Theme.fontCaption
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // ---- 错误日志 ----
            AppCard {
                Layout.fillWidth: true
                Layout.preferredHeight: logCol.implicitHeight + Theme.spacingL * 2

                ColumnLayout {
                    id: logCol
                    anchors.fill: parent
                    anchors.margins: Theme.spacingL
                    spacing: Theme.spacingM

                    Text {
                        text: qsTr("错误日志")
                        font.pixelSize: Theme.fontSubtitle
                        font.bold: true
                        color: Theme.text
                    }
                    Text {
                        Layout.fillWidth: true
                        text: app.logFilePath
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textSecondary
                        elide: Text.ElideMiddle
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingM
                        AppButton {
                            text: qsTr("打开日志目录")
                            onClicked: app.openLogDir()
                        }
                        AppButton {
                            text: qsTr("导出日志…")
                            onClicked: exportDialog.open()
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.exportMsg !== ""
                        text: root.exportMsg
                        font.pixelSize: Theme.fontCaption
                        color: Theme.success
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("遇到异常或排课结果疑问时，导出日志发送给开发者可加速定位。")
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // ---- 关于 ----
            AppCard {
                Layout.fillWidth: true
                Layout.preferredHeight: aboutRow.implicitHeight + Theme.spacingL * 2

                RowLayout {
                    id: aboutRow
                    anchors.fill: parent
                    anchors.margins: Theme.spacingL
                    spacing: Theme.spacingL

                    Image {
                        source: ":/qt/qml/SmartScheduler/icon.png"
                        sourceSize.width: 56
                        sourceSize.height: 56
                        Layout.alignment: Qt.AlignTop
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXS
                        Text {
                            text: qsTr("智排课系统 SmartScheduler")
                            font.pixelSize: Theme.fontSubtitle
                            font.bold: true
                            color: Theme.text
                        }
                        Text {
                            text: qsTr("版本 v%1 · Qt %2").arg(app.appVersion).arg(app.qtVersion)
                            font.pixelSize: Theme.fontBody
                            color: Theme.textSecondary
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("现代化自动排课：约束保证、质量可度量、结果可复现")
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: exportDialog
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("日志文件 (*.log)"), qsTr("文本文件 (*.txt)")]
        onAccepted: {
            const ok = app.exportLog(selectedFile.toString())
            root.exportMsg = ok ? qsTr("已导出：%1").arg(selectedFile.toString())
                                : qsTr("导出失败，请检查目标路径是否可写")
        }
    }

    // 安装更新确认：先保存未保存的项目，再启动静默安装并退出
    AppConfirmDialog {
        id: installConfirm
        parent: Overlay.overlay
        title: qsTr("安装更新")
        message: qsTr("将关闭智排课系统并自动安装 %1，完成后可从开始菜单或桌面快捷方式重新打开。")
                     .arg(app.latestVersion)
                 + (app.projectDirty ? qsTr("\n当前项目有未保存更改，将先为您保存。") : "")
        okText: qsTr("开始安装")
        onAccepted: {
            if (app.projectDirty && !app.saveProject()) {
                installSaveAs.open() // 从未保存过的项目需先选路径
            } else {
                app.installUpdate()
            }
        }
    }
    FileDialog {
        id: installSaveAs
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("SmartScheduler 项目 (*.spk)")]
        onAccepted: {
            if (app.saveProjectAs(selectedFile.toString()))
                app.installUpdate()
        }
    }
}
