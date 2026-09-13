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
        case 2: return qsTr("✓ 当前已是最新版本（v%1）").arg(app.appVersion)
        case 3: return qsTr("发现新版本 %1，建议更新。").arg(app.latestVersion)
        case 4: return app.updateMessage
        default: return qsTr("尚未检查更新")
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
                            text: app.updateStatus === 1 ? qsTr("检查中…") : qsTr("检查更新")
                            variant: "primary"
                            enabled: app.updateStatus !== 1
                            onClicked: app.checkUpdate()
                        }
                        AppButton {
                            visible: app.updateStatus === 3
                            text: qsTr("前往下载")
                            onClicked: app.openReleasesPage()
                        }
                        AppButton {
                            visible: app.updateStatus === 4
                            text: qsTr("打开发布页")
                            onClicked: app.openReleasesPage()
                        }
                        Item { Layout.fillWidth: true }
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
}
