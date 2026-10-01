import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 错误反馈：一句话描述 + 可选附带最近日志，直接发到中继的 /v1/feedback
Popup {
    id: root

    property int logKb: 0

    modal: true
    anchors.centerIn: parent
    width: 560
    padding: 0
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        radius: Theme.radiusL
        color: Theme.surface
        border.color: Theme.border
    }
    Overlay.modal: Rectangle {
        color: Theme.dark ? Qt.rgba(0, 0, 0, 0.55) : Qt.rgba(15, 23, 42, 0.28)
    }

    onOpened: {
        root.logKb = Math.round(app.recentLogText().length / 1024)
        descField.forceActiveFocus()
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingM

        Text {
            Layout.topMargin: Theme.spacingL
            Layout.leftMargin: Theme.spacingXL
            text: qsTr("反馈问题")
            font.pixelSize: Theme.fontSubtitle
            font.bold: true
            color: Theme.text
        }
        Text {
            Layout.leftMargin: Theme.spacingXL
            Layout.rightMargin: Theme.spacingXL
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            text: qsTr("描述发生了什么、点了什么；勾选附带日志后，开发者能直接看到出错前后的记录。")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontCaption
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.leftMargin: Theme.spacingXL
            Layout.rightMargin: Theme.spacingXL
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            radius: Theme.radiusM
            color: Theme.surfaceAlt
            border.width: descField.activeFocus ? 2 : 1
            border.color: descField.activeFocus ? Theme.primary : Theme.border

            TextEdit {
                id: descField
                anchors.fill: parent
                anchors.margins: Theme.spacingS
                wrapMode: TextEdit.Wrap
                clip: true
                color: Theme.text
                font.pixelSize: Theme.fontBody
                selectByMouse: true
                Text {
                    anchors.fill: parent
                    visible: descField.text.length === 0
                    text: qsTr("例：点「自动排课」跑了很久最后提示失败，日志见附件")
                    color: Theme.textDisabled
                    font.pixelSize: Theme.fontBody
                    wrapMode: Text.WordWrap
                }
            }
        }

        RowLayout {
            Layout.leftMargin: Theme.spacingXL
            Layout.rightMargin: Theme.spacingXL
            Layout.fillWidth: true
            spacing: Theme.spacingS
            AppSwitch {
                id: withLogSwitch
                checked: true
                onToggled: function (checked) { withLogSwitch.checked = checked }
            }
            Text {
                Layout.fillWidth: true
                Layout.preferredWidth: 0
                text: qsTr("附带最近日志（约 %1 KB，含设备号与版本号）").arg(root.logKb)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.WordWrap
            }
        }

        Text {
            Layout.leftMargin: Theme.spacingXL
            Layout.rightMargin: Theme.spacingXL
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            visible: app.feedbackMessage !== ""
            text: app.feedbackMessage
            color: app.feedbackOk ? Theme.success : Theme.danger
            font.pixelSize: Theme.fontCaption
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.leftMargin: Theme.spacingXL
            Layout.rightMargin: Theme.spacingXL
            Layout.bottomMargin: Theme.spacingL
            Layout.fillWidth: true
            spacing: Theme.spacingS
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("关闭")
                variant: "subtle"
                onClicked: root.close()
            }
            AppButton {
                text: app.feedbackBusy ? qsTr("发送中…") : qsTr("发送反馈")
                variant: "primary"
                enabled: !app.feedbackBusy
                onClicked: app.sendFeedback(descField.text, withLogSwitch.checked)
            }
        }
    }
}
