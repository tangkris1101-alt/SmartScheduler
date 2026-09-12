import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 新手引导轮播：欢迎 → 四步流程 → 深色模式提示
Popup {
    id: root
    property int page: 0

    modal: true
    anchors.centerIn: parent
    width: 560
    height: 420
    padding: 0
    closePolicy: Popup.NoAutoClose

    background: Rectangle { radius: Theme.radiusL; color: Theme.surface; border.color: Theme.border }
    Overlay.modal: Rectangle { color: Theme.dark ? Qt.rgba(0,0,0,0.6) : Qt.rgba(15,23,42,0.32) }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 160 }
        NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: 160; easing.type: Easing.OutCubic }
    }

    readonly property var pages: [
        {
            "icon": "🗓️",
            "title": qsTr("欢迎使用智排课系统"),
            "body": qsTr("现代化的中小学自动排课工具。\n约束保证可行、质量评分可度量、结果可复现。")
        },
        {
            "icon": "📋",
            "title": qsTr("第一步：录名单"),
            "body": qsTr("在「名单管理」录入班级 / 课程 / 老师 / 教室；\n支持 Excel 模板批量导入（推荐）。")
        },
        {
            "icon": "📅",
            "title": qsTr("第二步：录计划"),
            "body": qsTr("在「教学计划」按班或按老师录入周课时，\n支持连课、合班、单双周；再到「限制条件」设置不排时间与预排。")
        },
        {
            "icon": "▶️",
            "title": qsTr("第三步：自动排课"),
            "body": qsTr("设置运行轮数与策略，一键生成课表，\n查看质量报告后「应用到课表」。")
        },
        {
            "icon": "✨",
            "title": qsTr("最后：调整与输出"),
            "body": qsTr("「课表查看」页可拖拽调课（Ctrl+Z 撤销）、临时调课；\n「打印输出」页支持打印 / PDF / PNG / Excel。\n右上角 🌙 可切换深色模式。")
        }
    ]

    contentItem: ColumnLayout {
        anchors.fill: parent
        spacing: 0

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.page

            Repeater {
                model: root.pages.length
                delegate: Item {
                    required property int index
                    opacity: StackLayout.isCurrentItem ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 160 } }

                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: Theme.spacingM
                        width: parent.width - Theme.spacingXL * 2

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: root.pages[parent.index].icon
                            font.pixelSize: 52
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: root.pages[parent.index].title
                            font.pixelSize: Theme.fontTitle
                            font.bold: true
                            color: Theme.text
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: root.pages[parent.index].body
                            font.pixelSize: Theme.fontBody
                            color: Theme.textSecondary
                            wrapMode: Text.WordWrap
                            lineHeight: 1.5
                        }
                    }
                }
            }
        }

        // 指示点
        Row {
            Layout.alignment: Qt.AlignHCenter
            spacing: 8
            Repeater {
                model: root.pages.length
                delegate: Rectangle {
                    required property int index
                    width: 8; height: 8; radius: 4
                    color: root.page === index ? Theme.primary : Theme.overlayPress
                    Behavior on color { ColorAnimation { duration: 120 } }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingL
            spacing: Theme.spacingS

            RowLayout {
                spacing: Theme.spacingS
                AppSwitch {
                    id: noMoreSwitch
                    checked: true
                }
                Text {
                    text: qsTr("不再显示")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontBody
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: noMoreSwitch.toggled(!noMoreSwitch.checked)
                    }
                }
            }
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("上一步")
                visible: root.page > 0
                onClicked: root.page--
            }
            AppButton {
                text: root.page === root.pages.length - 1 ? qsTr("开始使用") : qsTr("下一步")
                variant: "primary"
                onClicked: {
                    if (root.page === root.pages.length - 1) {
                        app.setOnboardingShown(noMoreSwitch.checked)
                        root.close()
                    } else {
                        root.page++
                    }
                }
            }
        }
    }
}
