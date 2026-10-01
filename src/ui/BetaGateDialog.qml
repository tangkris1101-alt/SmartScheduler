import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 内测门禁：首次使用要求输入邀请码，服务端登记过本设备后不再出现。
// 不设关闭策略——没通过就进不去主界面（只有「退出」或「重试」）。
Popup {
    id: root

    modal: true
    anchors.centerIn: parent
    width: 460
    padding: 0
    closePolicy: Popup.NoAutoClose

    background: Rectangle {
        radius: Theme.radiusL
        color: Theme.surface
        border.color: Theme.border
    }
    Overlay.modal: Rectangle {
        color: Theme.dark ? Qt.rgba(0, 0, 0, 0.62) : Qt.rgba(15, 23, 42, 0.38)
    }

    function submit() {
        app.submitInviteCode(codeField.text)
    }

    onOpened: codeField.forceFocus()

    contentItem: ColumnLayout {
        spacing: Theme.spacingM

        Text {
            Layout.topMargin: Theme.spacingL
            Layout.leftMargin: Theme.spacingXL
            Layout.rightMargin: Theme.spacingXL
            text: qsTr("内测邀请码")
            font.pixelSize: Theme.fontSubtitle
            font.bold: true
            color: Theme.text
        }
        Text {
            Layout.leftMargin: Theme.spacingXL
            Layout.rightMargin: Theme.spacingXL
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            text: qsTr("当前是内测版本，需要邀请码才能使用。通过后本机会被登记，以后启动不再需要输入。")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontCaption
            wrapMode: Text.WordWrap
        }

        AppTextField {
            id: codeField
            Layout.leftMargin: Theme.spacingXL
            Layout.rightMargin: Theme.spacingXL
            Layout.fillWidth: true
            placeholder: qsTr("XXXX-XXXX-XXXX-XXXX")
            onAccepted: root.submit()
            Keys.onReturnPressed: root.submit()
            Keys.onEnterPressed: root.submit()
        }

        Text {
            Layout.leftMargin: Theme.spacingXL
            Layout.rightMargin: Theme.spacingXL
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            visible: app.betaMessage !== ""
            text: app.betaMessage
            color: app.betaMessage.indexOf("放行") >= 0 ? Theme.textSecondary : Theme.danger
            font.pixelSize: Theme.fontCaption
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.leftMargin: Theme.spacingXL
            Layout.rightMargin: Theme.spacingXL
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            text: qsTr("设备号 %1（反馈问题时一并提供，便于核对）").arg(app.deviceId)
            color: Theme.textDisabled
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
        }

        RowLayout {
            Layout.leftMargin: Theme.spacingXL
            Layout.rightMargin: Theme.spacingXL
            Layout.bottomMargin: Theme.spacingL
            Layout.fillWidth: true
            spacing: Theme.spacingS

            AppButton {
                text: qsTr("重试连接")
                variant: "subtle"
                enabled: !app.betaBusy
                onClicked: app.checkBetaGate()
            }
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("退出")
                variant: "subtle"
                onClicked: app.quitApp()
            }
            AppButton {
                text: app.betaBusy ? qsTr("校验中…") : qsTr("进入")
                variant: "primary"
                enabled: !app.betaBusy
                onClicked: root.submit()
            }
        }
    }
}
