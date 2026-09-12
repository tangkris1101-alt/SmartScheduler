import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 1280
    height: 800
    visible: true
    title: qsTr("智排课系统 SmartScheduler")

    // 主题占位：后续迁移到独立 Theme 单例（Light/Dark）
    palette.window: "#f5f6f7"
    color: palette.window

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 12

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("智排课系统")
            font.pixelSize: 32
            font.bold: true
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("项目骨架已就绪 —— 基于 Qt 6 / QML 重建")
            font.pixelSize: 16
            color: "#666666"
        }
    }
}
