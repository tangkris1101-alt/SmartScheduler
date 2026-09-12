import QtQuick
import QtQuick.Layouts

// 数字步进器
RowLayout {
    id: root
    property int value: 0
    property int from: 0
    property int to: 99
    property string suffix: ""
    signal valueModified(int value)

    spacing: 0

    Rectangle {
        Layout.preferredWidth: 28
        Layout.preferredHeight: 28
        radius: Theme.radiusS
        color: minusHover.containsMouse ? Theme.overlayHover : "transparent"
        border.color: Theme.border
        enabled: root.value > root.from
        opacity: enabled ? 1 : 0.4
        Text {
            anchors.centerIn: parent
            text: "−"
            color: Theme.text
        }
        MouseArea {
            id: minusHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                root.value = Math.max(root.from, root.value - 1)
                root.valueModified(root.value)
            }
        }
    }

    Text {
        Layout.preferredWidth: valueText.implicitWidth + 20
        horizontalAlignment: Text.AlignHCenter
        text: root.value + root.suffix
        color: Theme.text
        font.pixelSize: Theme.fontBody
        Text { id: valueText; visible: false; text: root.to + root.suffix; font.pixelSize: Theme.fontBody }
    }

    Rectangle {
        Layout.preferredWidth: 28
        Layout.preferredHeight: 28
        radius: Theme.radiusS
        color: plusHover.containsMouse ? Theme.overlayHover : "transparent"
        border.color: Theme.border
        enabled: root.value < root.to
        opacity: enabled ? 1 : 0.4
        Text {
            anchors.centerIn: parent
            text: "+"
            color: Theme.text
        }
        MouseArea {
            id: plusHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                root.value = Math.min(root.to, root.value + 1)
                root.valueModified(root.value)
            }
        }
    }
}
