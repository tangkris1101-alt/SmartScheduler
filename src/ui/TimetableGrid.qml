import QtQuick
import QtQuick.Layouts

// 可复用课表网格：行=节次、列=星期
// cellState(day, period) → 0 正常 / 1 禁排(红叉) / 2 预排(锁) / 3 活动(橙)
// cellText(day, period) → 单元格文字；revision 递增触发全部单元格重算
Item {
    id: grid
    property int days: app.daysPerWeek
    property int periods: app.periodsPerDay
    property int morningPeriods: app.morningPeriods
    property var cellState: function (day, period) { return 0 }
    property var cellText: function (day, period) { return "" }
    property var cellSubText: function (day, period) { return "" }    // 第二行小字（如老师名）
    property var cellAccent: function (day, period) { return false }  // 主课色条
    property var cellHighlight: function (day, period) { return false } // 分布高亮
    property int revision: 0
    property bool headerActions: false // 行列头点击整列/整行快捷设置

    signal cellClicked(int day, int period)
    signal dayHeaderClicked(int day)
    signal periodHeaderClicked(int period)

    readonly property var dayNames: ["周一", "周二", "周三", "周四", "周五", "周六", "周日"]

    function slotOf(day, period) { return day * grid.periods + period }

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        // 星期表头
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            spacing: 4
            Item { Layout.preferredWidth: 52 }
            Repeater {
                model: grid.days
                delegate: Rectangle {
                    required property int index
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radiusS
                    color: dayHover.containsMouse && grid.headerActions
                           ? Theme.overlayHover : Theme.surfaceAlt
                    Text {
                        anchors.centerIn: parent
                        text: grid.dayNames[index]
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontCaption
                        font.bold: true
                    }
                    MouseArea {
                        id: dayHover
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: grid.headerActions
                        cursorShape: grid.headerActions ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: grid.dayHeaderClicked(index)
                    }
                }
            }
        }

        // 节次行
        Repeater {
            model: grid.periods
            delegate: ColumnLayout {
                id: periodRow
                required property int index
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // 上午/下午分隔线
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 8
                    visible: periodRow.index === grid.morningPeriods
                    color: "transparent"
                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width
                        height: 1
                        color: Theme.border
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 4

                    // 节次表头
                    Rectangle {
                        Layout.preferredWidth: 52
                        Layout.fillHeight: true
                        radius: Theme.radiusS
                        color: periodHover.containsMouse && grid.headerActions
                               ? Theme.overlayHover : Theme.surfaceAlt
                        Column {
                            anchors.centerIn: parent
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: qsTr("第%1节").arg(periodRow.index + 1)
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontCaption
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: periodRow.index < grid.morningPeriods ? qsTr("上午") : qsTr("下午")
                                color: Theme.textDisabled
                                font.pixelSize: 10
                            }
                        }
                        MouseArea {
                            id: periodHover
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: grid.headerActions
                            cursorShape: grid.headerActions ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: grid.periodHeaderClicked(periodRow.index)
                        }
                    }

                    // 单元格
                    Repeater {
                        model: grid.days
                        delegate: Rectangle {
                            id: cell
                            required property int index
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 34
                            radius: Theme.radiusS

                            property int state: {
                                grid.revision // 依赖：revision 变化时重算
                                return grid.cellState(index, periodRow.index)
                            }
                            property string label: {
                                grid.revision
                                return grid.cellText(index, periodRow.index)
                            }
                            property string subLabel: {
                                grid.revision
                                return grid.cellSubText(index, periodRow.index)
                            }
                            property bool accent: {
                                grid.revision
                                return grid.cellAccent(index, periodRow.index)
                            }
                            property bool highlighted: {
                                grid.revision
                                return grid.cellHighlight(index, periodRow.index)
                            }

                            color: state === 1 ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.14)
                                 : state === 2 ? Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.14)
                                 : state === 3 ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.16)
                                 : highlighted ? Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.10)
                                 : (cellHover.containsMouse ? Theme.overlayHover : "transparent")
                            border.color: highlighted ? Theme.primary
                                        : state === 1 ? Theme.danger
                                        : state === 2 ? Theme.primary
                                        : state === 3 ? Theme.warning
                                        : Theme.border
                            border.width: (state === 0 && !highlighted) ? 1 : 1.5
                            Behavior on color { ColorAnimation { duration: 100 } }

                            // 主课色条
                            Rectangle {
                                visible: cell.accent
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.margins: 3
                                width: 3
                                radius: 1.5
                                color: Theme.primary
                            }

                            Column {
                                anchors.centerIn: parent
                                width: parent.width - 8
                                spacing: 0
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: parent.width
                                    horizontalAlignment: Text.AlignHCenter
                                    text: cell.state === 1 ? "✕"
                                        : cell.state === 2 ? ("🔒 " + cell.label)
                                        : cell.state === 3 ? ("◆ " + cell.label)
                                        : cell.label
                                    color: cell.state === 1 ? Theme.danger
                                         : cell.state === 2 ? Theme.primary
                                         : cell.state === 3 ? Theme.warning
                                         : Theme.text
                                    font.pixelSize: Theme.fontCaption
                                    elide: Text.ElideRight
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: parent.width
                                    horizontalAlignment: Text.AlignHCenter
                                    visible: text.length > 0
                                    text: cell.subLabel
                                    color: Theme.textSecondary
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }
                            MouseArea {
                                id: cellHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: grid.cellClicked(index, periodRow.index)
                            }
                        }
                    }
                }
            }
        }
    }
}
