import QtQuick

// 分段标签切换：滑动高亮动画。stretch=true 时标签均分可用宽度（文字超长省略），
// 用于打印输出页等宽度受限场景；默认按内容自适应，行为不变。
// 键盘可达：Tab 聚焦（焦点描边）、←/→ 切换标签；对读屏暴露为 PageTabList
Item {
    id: root
    property var model: []
    property int currentIndex: 0
    property bool stretch: false
    signal tabClicked(int index)

    implicitWidth: stretch ? 160 : row.x * 2 + row.implicitWidth
    implicitHeight: 36
    activeFocusOnTab: true

    Accessible.role: Accessible.PageTabList
    Accessible.name: (root.currentIndex >= 0 && root.currentIndex < root.model.length)
                     ? String(root.model[root.currentIndex] ?? "") : ""

    Keys.onLeftPressed: if (root.currentIndex > 0) root.tabClicked(root.currentIndex - 1)
    Keys.onRightPressed: if (root.currentIndex < root.model.length - 1) root.tabClicked(root.currentIndex + 1)

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusM
        color: Theme.surfaceAlt
        border.width: root.activeFocus ? 2 : 0
        border.color: Theme.primary
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
