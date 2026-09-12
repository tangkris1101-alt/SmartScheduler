import QtQuick

// 卡片容器：圆角 + 边框 + 轻量投影（偏移色块模拟 elevation）
Item {
    id: root
    default property alias content: container.data
    property int elevation: 1 // 0=无边框投影，1=标准卡片
    property int padding: Theme.spacingL

    Rectangle {
        visible: root.elevation > 0
        anchors.fill: container
        anchors.topMargin: 2
        anchors.leftMargin: 1
        anchors.rightMargin: 1
        radius: container.radius
        color: Theme.shadow
    }

    Rectangle {
        id: container
        anchors.fill: parent
        radius: Theme.radiusL
        color: Theme.surface
        border.width: root.elevation > 0 ? 1 : 0
        border.color: Theme.border
        Behavior on color { ColorAnimation { duration: 150 } }
    }

    // 内容区域留白由调用方通过 anchors.margins 控制；padding 供引用
}
