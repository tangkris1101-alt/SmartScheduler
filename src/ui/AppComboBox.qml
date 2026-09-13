import QtQuick
import QtQuick.Controls

// 下拉选择：model 为字符串数组
Rectangle {
    id: root
    property var model: []
    property int currentIndex: 0
    signal activated(int index)

    implicitWidth: 160
    implicitHeight: 34
    radius: Theme.radiusM
    color: mouse.containsMouse ? Theme.overlayHover : Theme.surface
    border.color: popup.visible ? Theme.primary : Theme.border
    Behavior on color { ColorAnimation { duration: 110 } }

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
