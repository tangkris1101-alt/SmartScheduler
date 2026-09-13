import QtQuick

// 分段标签切换：滑动高亮动画。stretch=true 时标签均分可用宽度（文字超长省略），
// 用于打印输出页等宽度受限场景；默认按内容自适应，行为不变。
Item {
    id: root
    property var model: []
    property int currentIndex: 0
    property bool stretch: false
    signal tabClicked(int index)

    implicitWidth: stretch ? 160 : row.x * 2 + row.implicitWidth
    implicitHeight: 36

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusM
        color: Theme.surfaceAlt
    }

    Rectangle {
        id: highlight
        height: 28
        y: 4
        radius: Theme.radiusM - 2
        color: Theme.surface
        border.color: Theme.border
        visible: row.children.length > root.currentIndex
        x: visible ? row.x + row.children[root.currentIndex].x : 0
        width: visible ? row.children[root.currentIndex].width : 0
        Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        Behavior on width { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
    }

    Row {
        id: row
        x: 4
        anchors.verticalCenter: parent.verticalCenter
        Repeater {
            model: root.model
            delegate: Item {
                id: tab
                required property int index
                required property string modelData
                width: root.stretch
                       ? Math.max(40, (root.width - row.x * 2) / Math.max(1, root.model.length))
                       : lbl.implicitWidth + 32
                height: 36
                Text {
                    id: lbl
                    anchors.centerIn: parent
                    width: root.stretch ? Math.min(implicitWidth, tab.width - 10) : implicitWidth
                    elide: root.stretch ? Text.ElideRight : Text.ElideNone
                    horizontalAlignment: Text.AlignHCenter
                    text: tab.modelData
                    font.pixelSize: Theme.fontBody
                    color: root.currentIndex === tab.index ? Theme.text : Theme.textSecondary
                    font.bold: root.currentIndex === tab.index
                    Behavior on color { ColorAnimation { duration: 120 } }
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.tabClicked(tab.index)
                }
            }
        }
    }
}
