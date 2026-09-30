import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// AI 助手页：一句话需求 → 中继调用大模型 → 工具调用在本地执行；
// 变更不直接落盘，而是挂成「待办卡」，由用户勾选后一次性应用（整体可撤销）。
Item {
    id: root

    readonly property var examples: [
        qsTr("把全年级每天第 8 节排成自习"),
        qsTr("高一各班班会固定在周一第 8 节"),
        qsTr("把数学周课时改成 6 节"),
        qsTr("看看现在的排课质量，有撞课吗"),
        qsTr("把高一1班周三第 3 节的课挪到周四第 3 节")
    ]

    function send(text) {
        if (text.trim().length === 0)
            return
        ai.sendPrompt(text)
        promptField.text = ""
    }
    function visualIndex(displayIndex) {
        // 待办卡列表里显示的序号 → ai.pending 的真实序号（中间可能有被移除的）
        return displayIndex
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingXL
        spacing: Theme.spacingM

        // 标题行
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingM
            ColumnLayout {
                spacing: 2
                Text {
                    text: qsTr("AI 助手")
                    font.pixelSize: Theme.fontBig
                    font.bold: true
                    color: Theme.text
                }
                Text {
                    text: qsTr("说一句话，AI 会调用系统里的批量接口把改动挂成待办，你勾选确认后才真正生效")
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textSecondary
                }
            }
            Item { Layout.fillWidth: true }
            AppButton {
                variant: "subtle"
                text: qsTr("⚙ 连接设置")
                onClicked: settingsDialog.open()
            }
            AppButton {
                variant: "subtle"
                text: qsTr("清空对话")
                onClicked: ai.clearTranscript()
            }
        }

        // 对话 + 待办
        AppCard {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Flickable {
                id: stream
                anchors.fill: parent
                anchors.margins: Theme.spacingM
                contentWidth: width
                contentHeight: streamColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                ColumnLayout {
                    id: streamColumn
                    width: stream.width
                    spacing: Theme.spacingS

                    // 空状态与示例
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: ai.transcript.length === 0 && ai.pendingCount === 0 && !ai.busy
                        spacing: Theme.spacingS
                        Text {
                            Layout.topMargin: Theme.spacingL
                            text: qsTr("试试这样说：")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.spacingS
                            Repeater {
                                model: root.examples
                                delegate: AppButton {
                                    required property string modelData
                                    variant: "subtle"
                                    text: modelData
                                    onClicked: root.send(modelData)
                                }
                            }
                        }
                        Text {
                            Layout.topMargin: Theme.spacingM
                            Layout.fillWidth: true
                            text: qsTr("AI 只会读写当前项目的数据；所有变更都要你确认后才生效。")
                            color: Theme.textDisabled
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.WordWrap
                        }
                    }

                    Repeater {
                        model: ai.transcript
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: bubbleText.implicitHeight + Theme.spacingM * 1.5
                            radius: Theme.radiusM
                            color: modelData.role === "user" ? Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.10)
                                 : modelData.role === "error" ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.12)
                                 : Theme.surfaceAlt
                            Text {
                                id: bubbleText
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.margins: Theme.spacingM
                                text: (modelData.role === "user" ? qsTr("你：")
                                      : modelData.role === "tool" ? qsTr("系统：")
                                      : modelData.role === "error" ? qsTr("⚠ ") : qsTr("AI："))
                                      + modelData.text
                                color: modelData.role === "error" ? Theme.danger : Theme.text
                                font.pixelSize: Theme.fontBody
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    // 流式输出
                    Rectangle {
                        Layout.fillWidth: true
                        visible: ai.streamText !== ""
                        implicitHeight: liveText.implicitHeight + Theme.spacingM * 1.5
                        radius: Theme.radiusM
                        color: Theme.surfaceAlt
                        Text {
                            id: liveText
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: Theme.spacingM
                            text: qsTr("AI：") + ai.streamText
                            color: Theme.text
                            font.pixelSize: Theme.fontBody
                            wrapMode: Text.WordWrap
                        }
                    }

                    // 待办卡
                    Text {
                        Layout.topMargin: Theme.spacingS
                        visible: ai.pendingCount > 0
                        text: qsTr("待确认的变更（%1 项，勾选后点「应用选中」）").arg(ai.pendingCount)
                        color: Theme.text
                        font.pixelSize: Theme.fontSubtitle
                        font.bold: true
                    }
                    Repeater {
                        model: ai.pending
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            implicitHeight: cardColumn.implicitHeight + Theme.spacingM * 1.5
                            radius: Theme.radiusM
                            color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.10)
                            border.color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.45)

                            ColumnLayout {
                                id: cardColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.margins: Theme.spacingM
                                spacing: Theme.spacingXS

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spacingS
                                    AppSwitch {
                                        checked: modelData.selected === true
                                        onToggled: function (isChecked) {
                                            ai.setPendingSelected(index, isChecked)
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: (modelData.title ?? "") + " · " + (modelData.summary ?? "")
                                        color: Theme.text
                                        font.pixelSize: Theme.fontBody
                                        font.bold: true
                                        wrapMode: Text.WordWrap
                                    }
                                    AppButton {
                                        variant: "subtle"
                                        text: qsTr("只看这条")
                                        onClicked: {
                                            const r = ai.applyPending([index])
                                            root.reportApplied(r)
                                        }
                                    }
                                    AppButton {
                                        variant: "subtle"
                                        text: qsTr("移除")
                                        onClicked: ai.removePending([index])
                                    }
                                }
                                Repeater {
                                    model: modelData.details ?? []
                                    delegate: Text {
                                        required property string modelData
                                        Layout.fillWidth: true
                                        text: "· " + modelData
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontCaption
                                        wrapMode: Text.WordWrap
                                    }
                                }
                                Repeater {
                                    model: modelData.warnings ?? []
                                    delegate: Text {
                                        required property string modelData
                                        Layout.fillWidth: true
                                        text: "⚠ " + modelData
                                        color: Theme.warning
                                        font.pixelSize: Theme.fontCaption
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }
                    }

                    Item { Layout.preferredHeight: Theme.spacingL }
                }
            }
        }

        // 底部：输入 + 动作
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingS
            AppTextField {
                id: promptField
                Layout.fillWidth: true
                placeholder: ai.busy ? qsTr("AI 正在思考…") : qsTr("说一句你要怎么改课表，例如：全年级每天第 8 节自习")
                enabled: !ai.busy
                onAccepted: root.send(text)
            }
            AppButton {
                variant: "primary"
                text: ai.busy ? qsTr("停止") : qsTr("发送")
                onClicked: ai.busy ? ai.cancel() : root.send(promptField.text)
            }
            AppButton {
                text: qsTr("应用选中（%1）").arg(ai.pendingCount)
                enabled: ai.pendingCount > 0
                onClicked: root.reportApplied(ai.applyPending([]))
            }
            AppButton {
                variant: "danger"
                text: qsTr("全部拒绝")
                enabled: ai.pendingCount > 0
                onClicked: ai.removePending([])
            }
            AppButton {
                variant: "subtle"
                text: qsTr("撤销本次 AI 变更")
                enabled: ai.canUndo
                onClicked: ai.undo()
            }
        }

        Text {
            Layout.fillWidth: true
            visible: ai.lastError !== ""
            text: ai.lastError
            color: Theme.danger
            font.pixelSize: Theme.fontCaption
            wrapMode: Text.WordWrap
        }
    }

    function reportApplied(result) {
        if (!result || result.applied === undefined)
            return
    }

    // 连接设置
    Popup {
        id: settingsDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 520
        padding: 0
        background: Rectangle {
            radius: Theme.radiusL
            color: Theme.surface
            border.color: Theme.border
        }
        Overlay.modal: Rectangle {
            color: Theme.dark ? Qt.rgba(0, 0, 0, 0.55) : Qt.rgba(15, 23, 42, 0.28)
        }
        contentItem: ColumnLayout {
            spacing: Theme.spacingS
            Text {
                Layout.topMargin: Theme.spacingL
                Layout.leftMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                text: qsTr("AI 连接设置")
                font.pixelSize: Theme.fontSubtitle
                font.bold: true
                color: Theme.text
            }
            Text {
                Layout.leftMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                Layout.fillWidth: true
                text: qsTr("默认走「智排课」官方中继（大模型密钥只保存在服务端）。企业内网可改成自建中继地址。")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.WordWrap
            }
            GridLayout {
                Layout.leftMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                Layout.fillWidth: true
                columns: 2
                rowSpacing: Theme.spacingS
                columnSpacing: Theme.spacingM
                Text { text: qsTr("中继地址"); color: Theme.text; font.pixelSize: Theme.fontBody }
                AppTextField {
                    id: relayField
                    Layout.fillWidth: true
                    text: ai.relayUrl
                    showClear: false
                }
                Text { text: qsTr("应用令牌"); color: Theme.text; font.pixelSize: Theme.fontBody }
                AppTextField {
                    id: tokenField
                    Layout.fillWidth: true
                    text: ai.appToken
                    showClear: false
                }
            }
            RowLayout {
                Layout.leftMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                Layout.bottomMargin: Theme.spacingL
                Layout.topMargin: Theme.spacingS
                Layout.fillWidth: true
                spacing: Theme.spacingS
                Item { Layout.fillWidth: true }
                AppButton { text: qsTr("取消"); onClicked: settingsDialog.close() }
                AppButton {
                    variant: "primary"
                    text: qsTr("保存")
                    onClicked: {
                        ai.relayUrl = relayField.text
                        ai.appToken = tokenField.text
                        settingsDialog.close()
                    }
                }
            }
        }
    }
}
