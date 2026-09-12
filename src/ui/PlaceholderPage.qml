import QtQuick
import QtQuick.Layouts

// "建设中"占位页
Item {
    id: root
    property string icon: "🚧"
    property string title: ""
    property string hint: ""

    AppCard {
        anchors.centerIn: parent
        width: 420
        height: 240

        AppEmptyState {
            anchors.centerIn: parent
            icon: root.icon
            title: root.title
            hint: root.hint
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.spacingL
            text: qsTr("该功能将在后续里程碑交付")
            color: Theme.textDisabled
            font.pixelSize: Theme.fontCaption
        }
    }
}
