import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// 质量报告页：十项指标（硬/软）+ 老师五项，可查看口径说明并导出自带打印样式的 HTML
Item {
    id: root

    property int rev: 0
    property int scopeIndex: 0   // 0 全校，1..n 年级组
    property string exportMsg: ""

    function report() {
        root.rev
        return app.qualityReport()
    }
    function scope() {
        const r = root.report()
        if (root.scopeIndex === 0)
            return { "indicators": r.indicators, "averages": r.averages, "teachers": r.teachers }
        const g = r.grades[root.scopeIndex - 1]
        return { "indicators": g ? g.indicators : [], "averages": g ? g.averages : ({}),
                 "teachers": root.scopeIndex === 0 ? r.teachers : [] }
    }
    function scopeNames() {
        const r = root.report()
        let names = [qsTr("全校")]
        for (const g of r.grades)
            names.push(qsTr("%1（%2 个班）").arg(g.name).arg(g.classCount))
        return names
    }
    function fmt(v, digits) {
        if (v === undefined || v === null) return "-"
        return Number(v).toFixed(digits === undefined ? 2 : digits)
    }
    function indicatorText(m) {
        if (m.isRatio && m.required > 0)
            return qsTr("%1 / %2").arg(fmt(m.value, 0)).arg(fmt(m.required, 0))
        return fmt(m.value, 0)
    }
    function indicatorColor(m) {
        if (m.hard)
            return m.isRatio ? (m.value >= m.required ? Theme.success : Theme.danger)
                             : (m.value <= 0 ? Theme.success : Theme.danger)
        return Theme.text
    }
    function indicatorHint(key) {
        switch (key) {
        case "placedPeriods": return qsTr("没排满：检查教学计划的周课时是否超过课表容量，或到限制条件页放开不必要的禁排。")
        case "teacherClash": return qsTr("老师撞课：多为跨班冲突。检查教学计划里的任课老师是否填对；两个班确实同节上课时应登记为合班。")
        case "courseForbiddenSlot": return qsTr("课程排进了不排时段：到「限制条件 → 不排时间」选中该课程调整，或确认这些节次确实要排它。")
        case "teacherForbiddenSlot": return qsTr("老师排进了不排时段：到「限制条件 → 不排时间」选中该老师调整禁排格子。")
        case "chainedPlaced": return qsTr("连堂没排上：检查该课程的连堂设置（教学计划里的「连课次数 / 每次节数」）与可用时段是否冲突。")
        case "chainGoodSlot": return qsTr("连堂位置不理想：可在「课表查看」里拖动调课，把连堂挪到第 1-2 或 4-5 节等习惯位置。")
        case "majorMorning": return qsTr("主课排在下午：可在「课程设置」给语数英勾选「上午」，或接受现状（软指标）。")
        case "sameCoursePerDay": return qsTr("同科一天 3 节以上：检查该课程的连堂次数是否过多，或减少单日课时。")
        case "teacherDailyLoad": return qsTr("老师一天 5 节以上：可在「名单管理 → 老师」设置该老师的「日最多课」。")
        case "teacherRunLength": return qsTr("老师连上 3 节以上：可调整教学计划里的连堂设置与任课分配。")
        }
        return ""
    }

    onVisibleChanged: if (visible) root.rev++
    Connections {
        target: app
        function onScheduleChanged() { root.rev++ }
        function onStatsChanged() { root.rev++ }
        function onProjectChanged() { root.rev++; root.scopeIndex = 0 }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingXL
        spacing: Theme.spacingM

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingM
            ColumnLayout {
                spacing: 2
                Text {
                    text: qsTr("排课质量报告")
                    font.pixelSize: Theme.fontBig
                    font.bold: true
                    color: Theme.text
                }
                Text {
                    text: qsTr("十项指标衡量合规与舒适度，老师五项衡量老师课表是否顺；口径见下方说明")
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textSecondary
                }
            }
            Item { Layout.fillWidth: true }
            AppComboBox {
                Layout.preferredWidth: 200
                model: { root.rev; return root.scopeNames() }
                currentIndex: root.scopeIndex
                onActivated: { root.scopeIndex = index; root.rev++ }
            }
            AppButton {
                text: qsTr("导出 HTML 报告…")
                variant: "primary"
                onClicked: exportDialog.open()
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.exportMsg !== ""
            text: root.exportMsg
            color: Theme.success
            font.pixelSize: Theme.fontCaption
            wrapMode: Text.WordWrap
        }

        // 读法引导：硬指标看问题、软指标看舒适度、老师五项看老师顺不顺
        Rectangle {
            Layout.fillWidth: true
            visible: app.hasSchedule
            height: guideCol.implicitHeight + Theme.spacingM * 2
            radius: Theme.radiusM
            color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.08)
            border.color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.35)

            ColumnLayout {
                id: guideCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingM
                spacing: Theme.spacingXS

                Text {
                    text: qsTr("这份报告怎么看")
                    color: Theme.text
                    font.pixelSize: Theme.fontBody
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("① 先看「十项指标」里的硬项：出现红字说明有违反或没排满，回到「教学计划 / 限制条件」检查；"
                               + "② 软项是舒适度（如连堂位置、主课是否在上午），用来在几套排法里挑更好的；"
                               + "③ 「老师五项」看老师课表顺不顺：数字越低越好，综合 = 开窗 + 周内均衡 + 先后公平。")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    spacing: Theme.spacingS
                    AppButton {
                        text: qsTr("去限制条件页")
                        variant: "subtle"
                        onClicked: {
                            const w = ApplicationWindow.window
                            if (w)
                                w.currentPage = 5
                        }
                    }
                    AppButton {
                        text: qsTr("去教学计划页")
                        variant: "subtle"
                        onClicked: {
                            const w = ApplicationWindow.window
                            if (w)
                                w.currentPage = 4
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }

        AppEmptyState {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !app.hasSchedule
            icon: "📈"
            title: qsTr("还没有排课结果")
            hint: qsTr("先到「自动排课」跑一次，或应用一份排课结果，再回来看质量报告")
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: app.hasSchedule
            contentWidth: width
            contentHeight: body.implicitHeight + Theme.spacingL
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: body
                width: parent.width
                spacing: Theme.spacingM

                // 十项指标
                AppCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: hardTable.implicitHeight + Theme.spacingL * 2

                    ColumnLayout {
                        id: hardTable
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.spacingL
                        spacing: Theme.spacingS

                        Text {
                            text: qsTr("十项指标")
                            font.pixelSize: Theme.fontSubtitle
                            font.bold: true
                            color: Theme.text
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingS
                            Text { Layout.preferredWidth: 220; text: qsTr("这一项"); color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                            Text { Layout.preferredWidth: 44; text: qsTr("类别"); color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                            Text { Layout.preferredWidth: 80; text: qsTr("方向"); color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                            Text { Layout.fillWidth: true; text: qsTr("实测"); color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                            Text { Layout.preferredWidth: 60; text: qsTr("单位"); color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                        }
                        Repeater {
                            model: root.scope().indicators
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: Theme.spacingS
                                Rectangle {
                                    Layout.preferredWidth: 220
                                    Layout.preferredHeight: 26
                                    color: "transparent"
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width
                                        text: modelData.name
                                        color: Theme.text
                                        font.pixelSize: Theme.fontBody
                                        elide: Text.ElideRight
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.WhatsThisCursor
                                        ToolTip.visible: containsMouse
                                        ToolTip.text: (modelData.hard
                                                     ? qsTr("硬指标：不许破，有值即为违例或缺口")
                                                     : qsTr("软指标：舒适度，越低越好，可手动微调"))
                                                    + "\n" + root.indicatorHint(modelData.key)
                                    }
                                }
                                Rectangle {
                                    Layout.preferredWidth: 44
                                    Layout.preferredHeight: 20
                                    radius: 10
                                    color: modelData.hard
                                           ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.14)
                                           : Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.16)
                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData.hard ? qsTr("硬") : qsTr("软")
                                        color: modelData.hard ? Theme.danger : Theme.warning
                                        font.pixelSize: 10
                                    }
                                }
                                Text {
                                    Layout.preferredWidth: 80
                                    text: modelData.higherBetter ? qsTr("↑ 越高越好") : qsTr("↓ 越低越好")
                                    color: Theme.textDisabled
                                    font.pixelSize: 10
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: root.indicatorText(modelData)
                                    color: root.indicatorColor(modelData)
                                    font.pixelSize: Theme.fontBody
                                    font.bold: true
                                }
                                Text {
                                    Layout.preferredWidth: 60
                                    text: modelData.unit
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontCaption
                                }
                            }
                        }
                    }
                }

                // 老师五项平均 + 明细
                AppCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: teacherCol.implicitHeight + Theme.spacingL * 2

                    ColumnLayout {
                        id: teacherCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.spacingL
                        spacing: Theme.spacingS

                        Text {
                            text: qsTr("老师顺不顺：五项（%1）")
                                  .arg(root.scopeIndex === 0 ? qsTr("全校平均") : qsTr("本年级平均"))
                            font.pixelSize: Theme.fontSubtitle
                            font.bold: true
                            color: Theme.text
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingL
                            Repeater {
                                model: [
                                    { k: qsTr("教案连续性 ↑1"), key: "continuity", d: 3 },
                                    { k: qsTr("教师开窗 ↓"), key: "window", d: 2 },
                                    { k: qsTr("周内均衡 ↓"), key: "balance", d: 2 },
                                    { k: qsTr("平行班先后公平 ↓"), key: "fairness", d: 2 },
                                    { k: qsTr("综合 ↓"), key: "composite", d: 2 }
                                ]
                                delegate: ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        text: modelData.k
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontCaption
                                    }
                                    Text {
                                        text: root.fmt(root.scope().averages[modelData.key], modelData.d)
                                        color: Theme.text
                                        font.pixelSize: Theme.fontTitle
                                        font.bold: true
                                    }
                                }
                            }
                        }
                    }
                }

                AppCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: teacherDetail.implicitHeight + Theme.spacingL * 2
                    visible: root.scopeIndex === 0 && root.scope().teachers.length > 0

                    ColumnLayout {
                        id: teacherDetail
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.spacingL
                        spacing: Theme.spacingS

                        Text {
                            text: qsTr("老师明细（%1 位，按周节数排序）").arg(root.scope().teachers.length)
                            font.pixelSize: Theme.fontSubtitle
                            font.bold: true
                            color: Theme.text
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingS
                            Text { Layout.preferredWidth: 110; text: qsTr("老师"); color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                            Text { Layout.preferredWidth: 60; text: qsTr("周节数"); color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                            Text { Layout.fillWidth: true; text: qsTr("教案连续性"); color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                            Text { Layout.fillWidth: true; text: qsTr("开窗"); color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                            Text { Layout.fillWidth: true; text: qsTr("周内均衡"); color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                            Text { Layout.fillWidth: true; text: qsTr("先后公平"); color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                            Text { Layout.fillWidth: true; text: qsTr("综合"); color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                        }
                        Repeater {
                            model: root.scope().teachers
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: Theme.spacingS
                                Text { Layout.preferredWidth: 110; text: modelData.name; color: Theme.text; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                                Text { Layout.preferredWidth: 60; text: modelData.periods; color: Theme.textSecondary; font.pixelSize: Theme.fontBody }
                                Text { Layout.fillWidth: true; text: root.fmt(modelData.continuity, 3); color: Theme.text; font.pixelSize: Theme.fontBody }
                                Text { Layout.fillWidth: true; text: root.fmt(modelData.window, 2); color: Theme.text; font.pixelSize: Theme.fontBody }
                                Text { Layout.fillWidth: true; text: root.fmt(modelData.balance, 2); color: Theme.text; font.pixelSize: Theme.fontBody }
                                Text { Layout.fillWidth: true; text: root.fmt(modelData.fairness, 2); color: Theme.text; font.pixelSize: Theme.fontBody }
                                Text { Layout.fillWidth: true; text: root.fmt(modelData.composite, 2); color: Theme.text; font.pixelSize: Theme.fontBody }
                            }
                        }
                    }
                }

                // 口径说明
                AppCard {
                    Layout.fillWidth: true
                    Layout.preferredHeight: formulaCol.implicitHeight + Theme.spacingL * 2

                    ColumnLayout {
                        id: formulaCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.spacingL
                        spacing: Theme.spacingXS

                        Text {
                            text: qsTr("指标怎么算")
                            font.pixelSize: Theme.fontSubtitle
                            font.bold: true
                            color: Theme.text
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("教案连续性 C = 1 / (1 + D̄)：同一位老师、同一门课、同一天教的几个平行班，「这周第几次课」的最大差 D，所有组取平均 D̄；几个班齐头并进 C 就是 1。")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("教师开窗 F = G + q(p_max) − q(p_min)：G 是课与课之间断开的次数，后半截是从第一节到最后一节的跨度（午休算一格）；课中间空着、从早拖到晚都会变大。")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("周内均衡 E = Σ|x_d − x̄| − D_min：x_d 是某位老师星期 d 的节数，减掉「怎么排都躲不掉」的不均 D_min = 2r(N−r)/N。体育不算。")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("平行班先后公平 B = ½ Σ|v − n/M|：同一组平行班里，每个班当「试讲班」的次数与理想次数的偏差；综合 S = F + E + B。")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("说明：硬指标（课时排满、老师不撞课、不排时段、连堂）有值即为问题；软指标用于横向比较与微调参考。导出的 HTML 报告已带打印样式，可直接浏览器打印或另存 PDF。")
                            color: Theme.textDisabled
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: exportDialog
        fileMode: FileDialog.SaveFile
        defaultSuffix: "html"
        nameFilters: [qsTr("网页报告 (*.html)")]
        onAccepted: {
            const ok = app.exportQualityReportHtml(selectedFile.toString())
            root.exportMsg = ok ? qsTr("已生成并打开：%1").arg(selectedFile.toString())
                                : qsTr("导出失败：%1").arg(app.lastError())
        }
    }
}
