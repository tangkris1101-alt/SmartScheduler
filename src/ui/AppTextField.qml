import QtQuick

// 主题化输入框：占位提示、聚焦描边、可清除按钮
Rectangle {
    id: root
    property alias text: input.text
    property string placeholder: ""
    property bool showClear: true
    signal accepted()
    function forceFocus() { input.forceActiveFocus() }

    implicitHeight: 34
    implicitWidth: 200
    radius: Theme.radiusM
    color: Theme.surface
    border.width: input.activeFocus ? 2 : 1
    border.color: input.activeFocus ? Theme.primary : Theme.border
    Behavior on border.color { ColorAnimation { duration: 120 } }

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingM
        anchors.rightMargin: (clearBtn.visible ? clearBtn.width : 0) + Theme.spacingS
        verticalAlignment: Text.AlignVCenter
        color: Theme.text
        font.pixelSize: Theme.fontBody
        selectByMouse: true
        clip: true
        onAccepted: root.accepted()

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: !input.text && !input.activeFocus
            text: root.placeholder
            color: Theme.textDisabled
            font.pixelSize: Theme.fontBody
        }
    }

    Text {
        id: clearBtn
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingS
        anchors.verticalCenter: parent.verticalCenter
        visible: root.showClear && input.text.length > 0
        text: "✕"
        color: clearHover.containsMouse ? Theme.text : Theme.textDisabled
        font.pixelSize: Theme.fontCaption
        MouseArea {
            id: clearHover
            anchors.fill: parent
            anchors.margins: -6
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: { input.text = ""; root.accepted() }
        }
    }
}
