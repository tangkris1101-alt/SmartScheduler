import QtQuick

// 开关
// 键盘可达：Tab 聚焦（焦点环）、Space 切换；对读屏暴露为可勾选框
Item {
    id: root
    property bool checked: false
    property string accessibleText: ""
    signal toggled(bool checked)

    implicitWidth: 40
    implicitHeight: 22
    activeFocusOnTab: true

    Accessible.role: Accessible.CheckBox
    Accessible.name: root.accessibleText
    Accessible.checkable: true
    Accessible.checked: root.checked
    Accessible.onPressAction: root.toggled(!root.checked)

    Keys.onSpacePressed: root.toggled(!root.checked)
    Keys.onReturnPressed: root.toggled(!root.checked)
    Keys.onEnterPressed: root.toggled(!root.checked)

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: root.checked ? Theme.primary : Theme.overlayPress
        border.width: root.activeFocus ? 2 : 0
        border.color: Theme.primary
        Behavior on color { ColorAnimation { duration: 140 } }

        Rectangle {
            width: 16
            height: 16
            radius: 8
            y: 3
            x: root.checked ? parent.width - width - 3 : 3
            color: "#ffffff"
            Behavior on x { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled(!root.checked)
    }
}
