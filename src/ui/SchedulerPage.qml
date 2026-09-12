import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 自动排课页：参数设置 + 前置检查 + 进度对话框 + 质量报告
Item {
    id: root

    property int runs: 8
    property int maxIterations: 20000
    property bool useRandomSeed: true
    property int strategy: 0 // 0 只要可排就排（placeAll） 1 影响质量的课不排

    function startRun() {
        app.runScheduler({
            "runs": root.runs,
            "maxIterations": root.maxIterations,
            "seed": seedField.text.length > 0 ? Number(seedField.text) : 0,
            "randomSeed": root.useRandomSeed || seedField.text.length === 0,
            "placeAll": root.strategy === 0
        })
        progressDialog.open()
    }

    function reportMap() {
        return app.schedulerReport
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingXL
        spacing: Theme.spacingM

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingL

            // 左列：参数 + 前置检查
            Flickable {
                Layout.preferredWidth: 400
                Layout.fillHeight: true
                contentHeight: leftColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: leftColumn
                    width: 400
                    spacing: Theme.spacingL

                    // 参数卡
                    AppCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: paramsColumn.implicitHeight + Theme.spacingL * 2
                        ColumnLayout {
                            id: paramsColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: Theme.spacingL
                            spacing: Theme.spacingM

                            Text {
                                text: qsTr("排课参数")
                                font.pixelSize: Theme.fontSubtitle
                                font.bold: true
                                color: Theme.text
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: Theme.spacingL
                                rowSpacing: Theme.spacingM

                                Text { text: qsTr("运行次数（取最优）"); color: Theme.text; font.pixelSize: Theme.fontBody }
                                AppStepper {
                                    from: 1; to: 32; value: root.runs; suffix: qsTr(" 轮")
                                    onValueModified: root.runs = value
                                }

                                Text { text: qsTr("每轮迭代上限"); color: Theme.text; font.pixelSize: Theme.fontBody }
                                AppComboBox {
                                    Layout.fillWidth: true
                                    model: ["5000", "10000", "20000", "50000", "100000"]
                                    currentIndex: 2
                                    onActivated: root.maxIterations = Number(model[index])
                                }

                                Text { text: qsTr("随机种子"); color: Theme.text; font.pixelSize: Theme.fontBody }
                                AppTextField {
                                    id: seedField
                                    Layout.fillWidth: true
                                    placeholder: qsTr("留空 = 每次随机")
                                }

                                Text { text: qsTr("排课策略"); color: Theme.text; font.pixelSize: Theme.fontBody }
                                AppComboBox {
                                    Layout.fillWidth: true
                                    model: [qsTr("只要可排就排"), qsTr("影响质量的课不排")]
                                    currentIndex: root.strategy
                                    onActivated: root.strategy = index
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("「影响质量的课不排」：实在放不下的课节将留作未排，由手工调整。")
                                color: Theme.textDisabled
                                font.pixelSize: Theme.fontCaption
                                wrapMode: Text.WordWrap
                            }

                            AppButton {
                                Layout.fillWidth: true
                                text: app.schedulerRunning ? qsTr("正在排课…") : qsTr("▶ 开始自动排课")
                                variant: "primary"
                                enabled: app.hasProject && !app.schedulerRunning
                                onClicked: root.startRun()
                            }
                        }
                    }

                    // 前置检查卡
                    AppCard {
                        Layout.fillWidth: true
                        Layout.preferredHeight: checkColumn.implicitHeight + Theme.spacingL * 2
                        ColumnLayout {
                            id: checkColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: Theme.spacingL
                            spacing: Theme.spacingS

                            Text {
                                text: qsTr("开始前检查")
                                font.pixelSize: Theme.fontSubtitle
                                font.bold: true
                                color: Theme.text
                            }
                            Repeater {
                                model: {
                                    root.revPrecheck
                                    return app.hasProject ? app.precheck() : []
                                }
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: Theme.spacingS
                                    Text {
                                        text: modelData.level === "err" ? "⛔"
                                            : modelData.level === "warn" ? "⚠️" : "✅"
                                        font.pixelSize: Theme.fontBody
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.text
                                        color: modelData.level === "err" ? Theme.danger
                                             : modelData.level === "warn" ? Theme.warning
                                             : Theme.textSecondary
                                        font.pixelSize: Theme.fontCaption
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 右列：结果区
            AppCard {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // 待应用的质量报告
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingXL
                    spacing: Theme.spacingM
                    visible: app.resultPending

                    Text {
                        text: qsTr("排课完成 · 质量报告")
                        font.pixelSize: Theme.fontSubtitle
                        font.bold: true
                        color: Theme.text
                    }

                    RowLayout {
                        spacing: Theme.spacingXL
                        Column {
                            spacing: 2
                            Text { text: qsTr("已排会话"); color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                            Text {
                                text: "%1 / %2".arg(reportMap().placedSessions ?? 0).arg(reportMap().totalSessions ?? 0)
                                color: Theme.text; font.pixelSize: Theme.fontTitle; font.bold: true
                            }
                        }
                        Column {
                            spacing: 2
                            Text { text: qsTr("硬违例"); color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                            Text {
                                text: String(reportMap().hardViolations ?? 0)
                                color: (reportMap().hardViolations ?? 0) === 0 ? Theme.success : Theme.danger
                                font.pixelSize: Theme.fontTitle; font.bold: true
                            }
                        }
                        Column {
                            spacing: 2
                            Text { text: qsTr("软罚分（越低越好）"); color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                            Text {
                                text: Number(reportMap().softScore ?? 0).toFixed(1)
                                color: Theme.text; font.pixelSize: Theme.fontTitle; font.bold: true
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    // 软约束分项条形图
                    Text {
                        text: qsTr("软约束分项")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontCaption
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingS
                        Repeater {
                            model: {
                                const bd = reportMap().breakdown ?? {}
                                const names = {
                                    "progressAlign": qsTr("进度一致"),
                                    "evenSpread": qsTr("平均分布"),
                                    "amPmPolicy": qsTr("上下午分配"),
                                    "majorMorning": qsTr("主课上午"),
                                    "teacherDaily": qsTr("老师日课时"),
                                    "classCourseSpread": qsTr("同课分布"),
                                    "chainAmPmCross": qsTr("连课跨界")
                                }
                                let arr = []
                                let maxV = 1
                                for (const k of Object.keys(bd)) maxV = Math.max(maxV, bd[k])
                                for (const k of Object.keys(names))
                                    arr.push({ key: k, label: names[k], value: bd[k] ?? 0, max: maxV })
                                return arr
                            }
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: Theme.spacingS
                                Text {
                                    Layout.preferredWidth: 90
                                    text: modelData.label
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontCaption
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 14
                                    radius: 7
                                    color: Theme.surfaceAlt
                                    Rectangle {
                                        height: parent.height
                                        radius: 7
                                        width: Math.max(2, parent.width * modelData.value / modelData.max)
                                        color: modelData.value > 0 ? Theme.primary : Theme.success
                                        Behavior on width { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
                                    }
                                }
                                Text {
                                    Layout.preferredWidth: 60
                                    horizontalAlignment: Text.AlignRight
                                    text: modelData.value.toFixed(1)
                                    color: Theme.text
                                    font.pixelSize: Theme.fontCaption
                                }
                            }
                        }
                    }

                    // 未排明细
                    Text {
                        visible: (reportMap().unplaced ?? []).length > 0
                        text: qsTr("未排出（%1 节次）").arg((reportMap().unplaced ?? []).length)
                        color: Theme.danger
                        font.pixelSize: Theme.fontCaption
                    }
                    Flow {
                        Layout.fillWidth: true
                        visible: (reportMap().unplaced ?? []).length > 0
                        spacing: Theme.spacingS
                        Repeater {
                            model: reportMap().unplaced ?? []
                            delegate: Rectangle {
                                required property string modelData
                                width: unplacedLabel.implicitWidth + 16
                                height: 26
                                radius: Theme.radiusS
                                color: Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.10)
                                Text {
                                    id: unplacedLabel
                                    anchors.centerIn: parent
                                    text: modelData
                                    color: Theme.danger
                                    font.pixelSize: Theme.fontCaption
                                }
                            }
                        }
                    }

                    Item { Layout.fillHeight: true }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingS
                        Item { Layout.fillWidth: true }
                        AppButton {
                            text: qsTr("放弃结果")
                            onClicked: app.discardResult()
                        }
                        AppButton {
                            text: qsTr("✔ 应用到课表")
                            variant: "primary"
                            onClicked: {
                                app.applyResult()
                                appliedHintTimer.restart()
                                appliedHint.visible = true
                            }
                        }
                    }
                }

                // 无待应用结果：当前课表概况 / 空状态
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Theme.spacingS
                    visible: !app.resultPending

                    AppEmptyState {
                        visible: !app.hasSchedule
                        icon: "🧩"
                        title: qsTr("还没有排课结果")
                        hint: qsTr("检查左侧参数与数据健康，然后点击「开始自动排课」")
                    }
                    AppEmptyState {
                        visible: app.hasSchedule
                        icon: "📊"
                        title: qsTr("当前课表：%1 条已排节次").arg(app.assignmentsCount)
                        hint: qsTr("前往「课表查看」页浏览；重新排课后需手动应用才会覆盖")
                    }
                    AppButton {
                        Layout.alignment: Qt.AlignHCenter
                        visible: app.hasSchedule
                        text: qsTr("查看课表 →")
                        variant: "subtle"
                        onClicked: {
                            const w = ApplicationWindow.window
                            if (w) w.currentPage = 7
                        }
                    }
                }

                // 应用成功提示
                Rectangle {
                    id: appliedHint
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.spacingL
                    visible: false
                    width: appliedText.implicitWidth + 32
                    height: 34
                    radius: Theme.radiusM
                    color: Theme.success
                    Text {
                        id: appliedText
                        anchors.centerIn: parent
                        text: qsTr("✔ 已应用到课表")
                        color: Theme.onPrimary
                        font.pixelSize: Theme.fontBody
                    }
                    Timer {
                        id: appliedHintTimer
                        interval: 2200
                        onTriggered: appliedHint.visible = false
                    }
                }
            }
        }
    }

    property int revPrecheck: 0
    Connections {
        target: app
        function onProjectChanged() { root.revPrecheck++ }
        function onStatsChanged() { root.revPrecheck++ }
    }

    // 进度对话框
    Popup {
        id: progressDialog
        parent: Overlay.overlay
        modal: true
        closePolicy: Popup.NoAutoClose
        anchors.centerIn: parent
        width: 440
        padding: 0
        background: Rectangle { radius: Theme.radiusL; color: Theme.surface; border.color: Theme.border }
        Overlay.modal: Rectangle { color: Theme.dark ? Qt.rgba(0,0,0,0.55) : Qt.rgba(15,23,42,0.28) }
        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 }
            NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: 140; easing.type: Easing.OutCubic }
        }

        Connections {
            target: app
            function onSchedulerFinished() { progressDialog.close() }
        }

        contentItem: ColumnLayout {
            spacing: Theme.spacingM
            Text {
                Layout.topMargin: Theme.spacingL; Layout.leftMargin: Theme.spacingXL
                text: qsTr("正在自动排课")
                font.pixelSize: Theme.fontSubtitle; font.bold: true; color: Theme.text
            }
            Text {
                Layout.leftMargin: Theme.spacingXL
                text: app.schedulerRunInfo
                color: Theme.textSecondary; font.pixelSize: Theme.fontCaption
            }
            Item {
                Layout.leftMargin: Theme.spacingXL; Layout.rightMargin: Theme.spacingXL
                Layout.fillWidth: true
                height: 10
                Rectangle {
                    anchors.fill: parent; radius: 5; color: Theme.surfaceAlt
                }
                Rectangle {
                    height: parent.height; radius: 5
                    width: parent.width * app.schedulerProgress / 100
                    color: Theme.primary
                    Behavior on width { NumberAnimation { duration: 150 } }
                }
            }
            RowLayout {
                Layout.leftMargin: Theme.spacingXL; Layout.rightMargin: Theme.spacingXL
                spacing: Theme.spacingL
                Text {
                    text: qsTr("已排 %1/%2").arg(app.schedulerReport.placedSessions ?? 0)
                          .arg(app.schedulerReport.totalSessions ?? 0)
                    color: Theme.text; font.pixelSize: Theme.fontCaption
                }
                Text {
                    text: qsTr("硬违例 %1").arg(app.schedulerReport.hardViolations ?? 0)
                    color: (app.schedulerReport.hardViolations ?? 0) === 0 ? Theme.success : Theme.danger
                    font.pixelSize: Theme.fontCaption
                }
                Text {
                    text: qsTr("软分 %1").arg(Number(app.schedulerReport.softScore ?? 0).toFixed(1))
                    color: Theme.text; font.pixelSize: Theme.fontCaption
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: app.schedulerProgress + "%"
                    color: Theme.primary; font.pixelSize: Theme.fontCaption; font.bold: true
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingS; Layout.bottomMargin: Theme.spacingL
                Layout.leftMargin: Theme.spacingXL; Layout.rightMargin: Theme.spacingXL
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("取消（保留当前最优）")
                    onClicked: app.cancelScheduler()
                }
            }
        }
    }
}
