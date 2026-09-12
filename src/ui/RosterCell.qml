import QtQuick

// 表格行内编辑单元格：点击进入编辑态，Enter 提交，Esc 取消，失焦提交
Item {
    id: root
    property string text: ""
    signal commit(string value)

    implicitHeight: 42
    clip: true

    Text {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.spacingM
        anchors.rightMargin: Theme.spacingS
        visible: !root.editing
        text: root.text
        color: Theme.text
        font.pixelSize: Theme.fontBody
        elide: Text.ElideRight
    }
    property bool editing: false

    Rectangle {
        anchors.fill: parent
        anchors.margins: 4
        visible: root.editing
        radius: Theme.radiusS
        color: Theme.surface
        border.width: 1.5
        border.color: Theme.primary

        TextInput {
            id: input
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingS
            anchors.rightMargin: Theme.spacingS
            verticalAlignment: Text.AlignVCenter
            color: Theme.text
            font.pixelSize: Theme.fontBody
            selectByMouse: true
            clip: true
            onAccepted: {
                root.commit(text)
                root.editing = false
            }
            Keys.onEscapePressed: root.editing = false
            onActiveFocusChanged: {
                if (!activeFocus && root.editing) {
                    root.commit(text)
                    root.editing = false
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        visible: !root.editing
        onClicked: {
            root.editing = true
            input.text = root.text
            input.forceActiveFocus()
            input.selectAll()
        }
    }
}
