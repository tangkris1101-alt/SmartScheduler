import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 新建项目向导对话框
Popup {
    id: root
    signal created()

    modal: true
    anchors.centerIn: parent
    width: 460
    padding: 0

    background: Rectangle {
        radius: Theme.radiusL
        color: Theme.surface
        border.color: Theme.border
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 150 }
        NumberAnimation { property: "scale"; from: 0.92; to: 1; duration: 150; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 110 }
    }

    Overlay.modal: Rectangle {
        color: Theme.dark ? Qt.rgba(0, 0, 0, 0.55) : Qt.rgba(15, 23, 42, 0.28)
    }

    onOpened: nameField.forceFocus()

    function create() {
        app.newProject(nameField.text, daysStepper.value, periodsStepper.value,
                       morningStepper.value, weeksStepper.value)
        app.applyTemplate(templateCombo.currentIndex)
        root.close()
        root.created()
    }

    contentItem: ColumnLayout {
        spacing: 0

        Text {
            Layout.topMargin: Theme.spacingL
            Layout.leftMargin: Theme.spacingXL
            text: qsTr("新建排课项目")
            font.pixelSize: Theme.fontTitle
            font.bold: true
            color: Theme.text
        }
        Text {
            Layout.topMargin: Theme.spacingXS
            Layout.leftMargin: Theme.spacingXL
            Layout.bottomMargin: Theme.spacingM
            text: qsTr("先设置学校名称与作息结构，之后仍可修改")
            font.pixelSize: Theme.fontCaption
            color: Theme.textSecondary
        }

        GridLayout {
            Layout.leftMargin: Theme.spacingXL
            Layout.rightMargin: Theme.spacingXL
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacingL
            rowSpacing: Theme.spacingM

            Text { text: qsTr("学校名称"); color: Theme.text; font.pixelSize: Theme.fontBody }
            AppTextField {
                id: nameField
                Layout.fillWidth: true
                placeholder: qsTr("例如：第一中学")
                showClear: false
                onAccepted: if (text.length > 0) root.create()
            }

            Text { text: qsTr("起始模板"); color: Theme.text; font.pixelSize: Theme.fontBody }
            AppComboBox {
                id: templateCombo
                Layout.fillWidth: true
                model: [qsTr("空白项目"), qsTr("小学模板"), qsTr("初中模板"), qsTr("高中模板")]
                currentIndex: 0
                // 模板联动推荐作息（仍可手动调整）
                onActivated: function (index) {
                    if (index === 1) { periodsStepper.value = 7; morningStepper.value = 4 }
                    else if (index === 2) { periodsStepper.value = 8; morningStepper.value = 4 }
                    else if (index === 3) { periodsStepper.value = 9; morningStepper.value = 5 }
                }
            }

            Text { text: qsTr("每周上课天数"); color: Theme.text; font.pixelSize: Theme.fontBody }
            AppStepper { id: daysStepper; from: 1; to: 7; value: 5; suffix: qsTr(" 天") }

            Text { text: qsTr("每天节数"); color: Theme.text; font.pixelSize: Theme.fontBody }
            AppStepper {
                id: periodsStepper
                from: 1; to: 12; value: 7; suffix: qsTr(" 节")
                onValueModified: if (morningStepper.value > value) morningStepper.value = value
            }

            Text { text: qsTr("其中上午节数"); color: Theme.text; font.pixelSize: Theme.fontBody }
            AppStepper {
                id: morningStepper
                from: 0; to: periodsStepper.value; value: 4; suffix: qsTr(" 节")
            }

            Text { text: qsTr("学期周数"); color: Theme.text; font.pixelSize: Theme.fontBody }
            AppStepper { id: weeksStepper; from: 1; to: 30; value: 20; suffix: qsTr(" 周") }
        }

        RowLayout {
            Layout.topMargin: Theme.spacingL
            Layout.bottomMargin: Theme.spacingL
            Layout.leftMargin: Theme.spacingXL
            Layout.rightMargin: Theme.spacingXL
            Layout.fillWidth: true
            spacing: Theme.spacingS

            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("取消")
                onClicked: root.close()
            }
            AppButton {
                text: qsTr("创建项目")
                variant: "primary"
                enabled: nameField.text.length > 0
                onClicked: root.create()
            }
        }
    }
}
