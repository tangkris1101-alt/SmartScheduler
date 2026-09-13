import QtQuick

// 主题化按钮：variant = primary | default | subtle | danger
Rectangle {
    id: root
    property string text: ""
    property string variant: "default"
    property bool enabled: true
    signal clicked()

    implicitWidth: label.implicitWidth + 32
    implicitHeight: 34
    radius: Theme.radiusM
    opacity: root.enabled ? 1.0 : 0.45
    border.width: root.variant === "default" ? 1 : 0
    border.color: Theme.border

    color: {
        if (!root.enabled)
            return Theme.surfaceAlt;
        if (root.variant === "primary")
            return mouse.pressed ? Theme.primaryPressed
                                 : (mouse.containsMouse ? Theme.primaryHover : Theme.primary);
        if (root.variant === "danger")
            return mouse.pressed ? Qt.darker(Theme.danger, 1.15)
                                 : (mouse.containsMouse ? Qt.lighter(Theme.danger, 1.08) : Theme.danger);
        if (root.variant === "subtle")
            return mouse.pressed ? Theme.overlayPress
                                 : (mouse.containsMouse ? Theme.overlayHover : "transparent");
        return mouse.pressed ? Theme.overlayPress
                             : (mouse.containsMouse ? Theme.overlayHover : "transparent");
    }
    Behavior on color { ColorAnimation { duration: 110 } }
    scale: mouse.pressed ? 0.96 : 1.0
    Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        font.pixelSize: Theme.fontBody
        // 禁用态统一用 textDisabled：否则 primary/danger 的白字在浅色 surfaceAlt 底上不可见
        color: !root.enabled
               ? Theme.textDisabled
               : ((root.variant === "primary" || root.variant === "danger")
                  ? Theme.onPrimary
                  : (root.variant === "subtle" ? Theme.primary : Theme.text))
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
