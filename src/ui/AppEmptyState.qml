import QtQuick
import QtQuick.Layouts

// 空状态提示：内容始终在所给区域内水平+垂直居中，
// 无论用 anchors.centerIn 还是 Layout.fillWidth/fillHeight 放置都居中
Item {
    id: root
    property string icon: "📭"
    property string title: ""
    property string hint: ""

    implicitWidth: col.implicitWidth
    implicitHeight: col.implicitHeight

    ColumnLayout {
        id: col
        anchors.centerIn: parent
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
}
