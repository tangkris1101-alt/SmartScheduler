import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 确认对话框（最多三个按钮）：缩放弹出动画 + 模态遮罩
Popup {
    id: root
    property string title: qsTr("确认")
    property string message: ""
    property string okText: qsTr("确定")
    property string thirdText: ""      // 可选第三按钮（如"不保存"）
    property string cancelText: qsTr("取消")
    property bool danger: false        // 危险操作（主按钮红色）
    signal accepted()
    signal thirdAction()
    signal rejected()

    modal: true
    anchors.centerIn: parent
    width: 400
    padding: 0

    background: Rectangle {
        radius: Theme.radiusL
        color: Theme.surface
        border.color: Theme.border
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 }
        NumberAnimation { property: "scale"; from: 0.92; to: 1; duration: 140; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 110 }
    }

    Overlay.modal: Rectangle {
        color: Theme.dark ? Qt.rgba(0, 0, 0, 0.55) : Qt.rgba(15, 23, 42, 0.28)
    }

    onOpened: { opacity = 1; scale = 1 }

    contentItem: ColumnLayout {
        spacing: 0

        Text {
            Layout.topMargin: Theme.spacingL
            Layout.leftMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingL
            text: root.title
            font.pixelSize: Theme.fontSubtitle
            font.bold: true
            color: Theme.text
        }
        Text {
            Layout.topMargin: Theme.spacingS
            Layout.leftMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingL
            Layout.fillWidth: true
            text: root.message
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font.pixelSize: Theme.fontBody
            lineHeight: 1.4
        }
        RowLayout {
            Layout.topMargin: Theme.spacingL
            Layout.bottomMargin: Theme.spacingL
            Layout.leftMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingL
            Layout.fillWidth: true
            spacing: Theme.spacingS

            Item { Layout.fillWidth: true }
            AppButton {
                objectName: "dlgCancelBtn"
                text: root.cancelText
                variant: "default"
                onClicked: { root.close(); root.rejected() }
            }
            AppButton {
                objectName: "dlgThirdBtn"
                visible: root.thirdText.length > 0
                text: root.thirdText
                variant: "default"
                onClicked: { root.close(); root.thirdAction() }
            }
            AppButton {
                objectName: "dlgOkBtn"
                text: root.okText
                variant: root.danger ? "danger" : "primary"
                onClicked: { root.close(); root.accepted() }
            }
        }
    }
}
