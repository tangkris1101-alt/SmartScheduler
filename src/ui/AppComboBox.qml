import QtQuick
import QtQuick.Controls

// 下拉选择：model 为字符串数组
// 键盘可达：Tab 聚焦（焦点描边）、Space/Enter 展开、↑/↓ 直接切换选项；对读屏暴露为 ComboBox
Rectangle {
    id: root
    property var model: []
    property int currentIndex: 0
    signal activated(int index)

    implicitWidth: 160
    implicitHeight: 34
    radius: Theme.radiusM
    activeFocusOnTab: true
    color: mouse.containsMouse ? Theme.overlayHover : Theme.surface
    border.width: 1
    border.color: (popup.visible || root.activeFocus) ? Theme.primary : Theme.border
    Behavior on color { ColorAnimation { duration: 110 } }

    Accessible.role: Accessible.ComboBox
    Accessible.name: (root.currentIndex >= 0 && root.currentIndex < root.model.length)
                     ? String(root.model[root.currentIndex] ?? "") : ""
    Accessible.onPressAction: popup.visible ? popup.close() : popup.open()

    Keys.onSpacePressed: popup.visible ? popup.close() : popup.open()
    Keys.onReturnPressed: popup.visible ? popup.close() : popup.open()
    Keys.onEnterPressed: popup.visible ? popup.close() : popup.open()
    Keys.onDownPressed: {
        if (root.currentIndex < root.model.length - 1) {
            root.currentIndex++
            root.activated(root.currentIndex)
        }
    }
    Keys.onUpPressed: {
        if (root.currentIndex > 0) {
            root.currentIndex--
            root.activated(root.currentIndex)
        }
    }

    Text {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingM
        text: (root.currentIndex >= 0 && root.currentIndex < root.model.length)
              ? String(root.model[root.currentIndex] ?? "") : ""
        color: Theme.text
        font.pixelSize: Theme.fontBody
    }
    Text {
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingM
        text: "▾"
        color: Theme.textSecondary
        rotation: popup.visible ? 180 : 0
        Behavior on rotation { NumberAnimation { duration: 140 } }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: popup.open()
    }

    Popup {
        id: popup
        y: root.height + 4
        width: root.width
        height: Math.min(list.contentHeight + 8, 240)
        padding: 4

        background: Rectangle {
            radius: Theme.radiusM
            color: Theme.surface
            border.color: Theme.border
        }

        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 }
            NumberAnimation { property: "scale"; from: 0.95; to: 1; duration: 120; easing.type: Easing.OutCubic }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 }
        }

        contentItem: ListView {
            id: list
            clip: true
            model: root.model
            delegate: Rectangle {
                id: opt
                objectName: "comboOption"
                required property int index
                required property string modelData
                width: list.width
                height: 32
                radius: Theme.radiusS
                color: optHover.containsMouse ? Theme.overlayHover
                     : (opt.index === root.currentIndex ? Theme.selectedBg : "transparent")
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingM
                    text: opt.modelData
                    color: opt.index === root.currentIndex ? Theme.primary : Theme.text
                    font.pixelSize: Theme.fontBody
                }
                MouseArea {
                    id: optHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        // 与原生 ComboBox 一致：点击选项即更新 currentIndex 再发信号
                        root.currentIndex = opt.index
                        root.activated(opt.index)
                        popup.close()
                    }
                }
            }
        }
    }
}
