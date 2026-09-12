import QtQuick
import QtQuick.Layouts

// 空状态提示
ColumnLayout {
    id: root
    property string icon: "📭"
    property string title: ""
    property string hint: ""
    spacing: Theme.spacingS

    Text {
        Layout.alignment: Qt.AlignHCenter
        text: root.icon
        font.pixelSize: 44
    }
    Text {
        Layout.alignment: Qt.AlignHCenter
        text: root.title
        font.pixelSize: Theme.fontSubtitle
        font.bold: true
        color: Theme.text
    }
    Text {
        Layout.alignment: Qt.AlignHCenter
        visible: root.hint.length > 0
        text: root.hint
        font.pixelSize: Theme.fontBody
        color: Theme.textSecondary
    }
}
