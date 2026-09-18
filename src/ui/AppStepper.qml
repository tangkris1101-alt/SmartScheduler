import QtQuick
import QtQuick.Layouts

// 数字步进器（stepSize 支持小数，如 0.5 表示单双周）
// 键盘可达：Tab 聚焦（焦点描边）、←/→ 或 ↓/↑ 步进；对读屏暴露为 SpinBox
RowLayout {
    id: root
    property real value: 0
    property real from: 0
    property real to: 99
    property real stepSize: 1
    property string suffix: ""
    property int decimals: stepSize < 1 ? 1 : 0
    signal valueModified(real value)

    spacing: 0
    activeFocusOnTab: true

    Accessible.role: Accessible.SpinBox
    Accessible.name: root.value.toFixed(root.decimals) + root.suffix

    function stepBy(dir) {
        root.value = Math.max(root.from, Math.min(root.to, root.value + dir * root.stepSize))
        root.valueModified(root.value)
    }

    Keys.onLeftPressed: root.stepBy(-1)
    Keys.onDownPressed: root.stepBy(-1)
    Keys.onRightPressed: root.stepBy(1)
    Keys.onUpPressed: root.stepBy(1)

    Rectangle {
        anchors.fill: parent
        z: -1
        radius: Theme.radiusS
        color: "transparent"
        border.width: root.activeFocus ? 2 : 0
        border.color: Theme.primary
    }

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
            onClicked: root.stepBy(-1)
        }
    }

    Text {
        Layout.preferredWidth: valueText.implicitWidth + 20
        horizontalAlignment: Text.AlignHCenter
        text: root.value.toFixed(root.decimals) + root.suffix
        color: Theme.text
        font.pixelSize: Theme.fontBody
        Text { id: valueText; visible: false; text: root.to.toFixed(root.decimals) + root.suffix; font.pixelSize: Theme.fontBody }
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
            onClicked: root.stepBy(1)
        }
    }
}
