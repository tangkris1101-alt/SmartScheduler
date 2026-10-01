import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// AI 助手页：一句话需求 → 中继调用大模型 → 工具调用在本地执行；
// 变更不直接落盘，而是挂成「待办卡」，由用户勾选后一次性应用（整体可撤销）。
Item {
    id: root

    readonly property var examples: [
        qsTr("把全年级每天第 8 节排成自习"),
        qsTr("高一各班班会固定在周一第 8 节"),
        qsTr("把数学周课时改成 6 节"),
        qsTr("看看现在的排课质量，有撞课吗"),
        qsTr("把高一1班周三第 3 节的课挪到周四第 3 节")
    ]

    function send(text) {
        if (text.trim().length === 0)
            return
        ai.sendPrompt(text)
        promptField.text = ""
        stream.followTail = true // 自己发的话一定要跟到最新
    }
    function visualIndex(displayIndex) {
        // 待办卡列表里显示的序号 → ai.pending 的真实序号（中间可能有被移除的）
        return displayIndex
    }

    // 对话流视图：系统消息（工具调用）在请求结束后合并成一行，避免几十条「系统：查询 …」刷屏；
    // 请求进行中仍逐条显示，用户能看到 AI 正在查什么。
    readonly property var viewItems: {
        const src = ai.transcript
        const collapse = !ai.busy
        const out = []
        let group = null
        for (let i = 0; i < src.length; ++i) {
            const e = src[i]
            if (e.role !== "tool" || !collapse) {
                group = null
                out.push({ kind: e.role === "tool" ? "tool" : "msg", role: e.role, text: e.text, key: i })
                continue
            }
            if (group === null) {
                group = { kind: "tools", key: i, texts: [] }
                out.push(group)
            }
            group.texts.push(e.text)
        }
        return out
    }
    // "查询 list_classes" ×N → "list_classes、query_plan ×6"
    function toolSummary(texts) {
        const counts = {}
        const order = []
        for (let i = 0; i < texts.length; ++i) {
            const m = /(\S+)\s*$/.exec(texts[i])
            const name = m ? m[1] : texts[i]
            if (counts[name] === undefined) {
                counts[name] = 0
                order.push(name)
            }
            counts[name]++
        }
        return order.map(function (n) { return counts[n] > 1 ? n + " ×" + counts[n] : n }).join("、")
    }
    property var expandedGroups: ({}) // 点开过的折叠组，key = 组内第一条的序号
    function toggleGroup(key) {
        const next = Object.assign({}, expandedGroups)
        next[key] = !next[key]
        expandedGroups = next
    }

    // 中英文混排的显示宽度：全角字符按 2 列算，用于表格列对齐
    function displayWidth(s) {
        let n = 0
        for (let i = 0; i < s.length; ++i) {
            const c = s.charCodeAt(i)
            const wide = (c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF)
                       || (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0xF900 && c <= 0xFAFF)
                       || (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60)
                       || (c >= 0xFFE0 && c <= 0xFFE6)
            n += wide ? 2 : 1
        }
        return n
    }

    // Qt 的 Markdown 不支持表格：直接渲染会把整张表糊成一整段。
    // 这里把连续的 | a | b | 行转成等宽代码块（去掉 |---| 分隔行、按列宽补空格），仍能对齐阅读。
    function prettifyMarkdown(src) {
        if (!src || src.indexOf("|") < 0)
            return src
        const isRow = function (l) { return /^\s*\|.*\|\s*$/.test(l) }
        const isSep = function (l) { return /^\s*\|[\s:|-]+\|\s*$/.test(l) }
        const lines = src.split("\n")
        const out = []
        let i = 0
        while (i < lines.length) {
            if (!isRow(lines[i])) {
                out.push(lines[i])
                i++
                continue
            }
            const rows = []
            while (i < lines.length && isRow(lines[i])) {
                if (!isSep(lines[i]))
                    rows.push(lines[i].trim().replace(/^\||\|$/g, "").split("|")
                                     .map(function (c) {
                                         // 代码块里 ** 与 ` 不会渲染成格式，直接去掉更干净
                                         return c.trim().replace(/\*\*/g, "").replace(/`/g, "")
                                     }))
                i++
            }
            let cols = 0
            for (const r of rows)
                cols = Math.max(cols, r.length)
            const widths = []
            for (let c = 0; c < cols; ++c) {
                widths[c] = 0
                for (const r of rows)
                    widths[c] = Math.max(widths[c], displayWidth(r[c] || ""))
            }
            out.push("```")
            for (const r of rows) {
                const cells = []
                for (let c = 0; c < cols; ++c) {
                    const t = r[c] || ""
                    cells.push(t + " ".repeat(Math.max(0, widths[c] - displayWidth(t))))
                }
                out.push(cells.join("  ").replace(/\s+$/, ""))
            }
            out.push("```")
        }
        return out.join("\n")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingXL
        spacing: Theme.spacingM

        // 标题行
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingM
            ColumnLayout {
                spacing: 2
                Text {
                    text: qsTr("AI 助手")
                    font.pixelSize: Theme.fontBig
                    font.bold: true
                    color: Theme.text
                }
                Text {
                    text: qsTr("说一句话，AI 会调用系统里的批量接口把改动挂成待办，你勾选确认后才真正生效")
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textSecondary
                }
            }
            Item { Layout.fillWidth: true }
            AppButton {
                variant: "subtle"
                text: qsTr("⚙ 连接设置")
                onClicked: settingsDialog.open()
            }
            AppButton {
                variant: "subtle"
                text: qsTr("清空对话")
                onClicked: ai.clearTranscript()
            }
        }

        // 对话 + 待办
        AppCard {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Flickable {
                id: stream
                anchors.fill: parent
                anchors.margins: Theme.spacingM
                contentWidth: width
                contentHeight: streamColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                // 新内容（流式输出/新消息/工具行）到达时贴底；用户主动往上翻则不再打扰，
                // 翻回底部附近又自动恢复跟随
                property bool followTail: true
                readonly property real tailY: Math.max(0, contentHeight - height)
                onContentHeightChanged: if (followTail) Qt.callLater(function () { contentY = tailY })
                onMovementEnded: followTail = contentY >= tailY - 48
                onFlickEnded: followTail = contentY >= tailY - 48

                ColumnLayout {
                    id: streamColumn
                    width: stream.width
                    spacing: Theme.spacingS

                    // 空状态与示例
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: ai.transcript.length === 0 && ai.pendingCount === 0 && !ai.busy
                        spacing: Theme.spacingS
                        Text {
                            Layout.topMargin: Theme.spacingL
                            text: qsTr("试试这样说：")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.spacingS
                            Repeater {
                                model: root.examples
                                delegate: AppButton {
                                    required property string modelData
                                    variant: "subtle"
                                    text: modelData
                                    onClicked: root.send(modelData)
                                }
                            }
                        }
                        Text {
                            Layout.topMargin: Theme.spacingM
                            Layout.fillWidth: true
                            text: qsTr("AI 只会读写当前项目的数据；所有变更都要你确认后才生效。")
                            color: Theme.textDisabled
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.WordWrap
                        }
                    }

                    Repeater {
                        model: root.viewItems
                        delegate: Rectangle {
                            required property var modelData
                            // kind：msg（对话）/ tool（进行中的单条系统消息）/ tools（折叠后的系统消息组）
                            readonly property bool isAi: modelData.kind === "msg"
                                                         && modelData.role !== "user"
                                                         && modelData.role !== "error"
                            readonly property bool isToolGroup: modelData.kind === "tools"
                            readonly property bool expanded: isToolGroup
                                                             && root.expandedGroups[modelData.key] === true
                            Layout.fillWidth: true
                            implicitHeight: bubbleText.implicitHeight + Theme.spacingM * 1.5
                            radius: Theme.radiusM
                            color: modelData.role === "user" ? Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.10)
                                 : modelData.role === "error" ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.12)
                                 : Theme.surfaceAlt
                            Text {
                                id: bubbleText
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.margins: Theme.spacingM
                                // AI 回复按 Markdown 渲染（粗体/列表/标题/行内代码/链接）；
                                // 前缀与正文之间留一个空行：Markdown 里单换行不成段，
                                // 否则首行若是「- 列表」会被前缀粘成普通文字
                                textFormat: parent.isAi ? Text.MarkdownText : Text.PlainText
                                text: {
                                    if (parent.isToolGroup) {
                                        const head = qsTr("系统：%1 次调用 · %2")
                                                     .arg(modelData.texts.length)
                                                     .arg(root.toolSummary(modelData.texts))
                                        const hint = parent.expanded ? qsTr("　▾ 收起")
                                                                     : qsTr("　▸ 展开")
                                        if (!parent.expanded)
                                            return head + hint
                                        return head + hint + "\n\n"
                                               + modelData.texts.map(function (t) { return "· " + t })
                                                     .join("\n")
                                    }
                                    const prefix = modelData.role === "user" ? qsTr("你：")
                                                 : modelData.role === "tool" ? qsTr("系统：")
                                                 : modelData.role === "error" ? qsTr("⚠ ") : qsTr("AI：")
                                    return parent.isAi
                                            ? prefix + "\n\n" + root.prettifyMarkdown(modelData.text)
                                            : prefix + modelData.text
                                }
                                color: modelData.role === "error" ? Theme.danger : Theme.text
                                font.pixelSize: Theme.fontBody
                                wrapMode: Text.WordWrap
                                onLinkActivated: (link) => Qt.openUrlExternally(link)
                            }
                            HoverHandler {
                                cursorShape: parent.isToolGroup ? Qt.PointingHandCursor : Qt.ArrowCursor
                            }
                            TapHandler {
                                enabled: parent.isToolGroup
                                onTapped: root.toggleGroup(modelData.key)
                            }
                        }
                    }

                    // 流式输出
                    Rectangle {
                        Layout.fillWidth: true
                        visible: ai.streamText !== ""
                        implicitHeight: liveText.implicitHeight + Theme.spacingM * 1.5
                        radius: Theme.radiusM
                        color: Theme.surfaceAlt
                        Text {
                            id: liveText
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: Theme.spacingM
                            // 流式输出同样按 Markdown 渲染（半截的 ** / ` 会短暂显示为原文，属正常）
                            textFormat: Text.MarkdownText
                            text: qsTr("AI：") + "\n\n" + root.prettifyMarkdown(ai.streamText)
                            color: Theme.text
                            font.pixelSize: Theme.fontBody
                            wrapMode: Text.WordWrap
                            onLinkActivated: (link) => Qt.openUrlExternally(link)
                        }
                    }

                    // 待办卡
                    Text {
                        Layout.topMargin: Theme.spacingS
                        visible: ai.pendingCount > 0
                        text: qsTr("待确认的变更（%1 项，勾选后点「应用选中」）").arg(ai.pendingCount)
                        color: Theme.text
                        font.pixelSize: Theme.fontSubtitle
                        font.bold: true
                    }
                    Repeater {
                        model: ai.pending
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            implicitHeight: cardColumn.implicitHeight + Theme.spacingM * 1.5
                            radius: Theme.radiusM
                            color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.10)
                            border.color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.45)

                            ColumnLayout {
                                id: cardColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.margins: Theme.spacingM
                                spacing: Theme.spacingXS

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spacingS
                                    AppSwitch {
                                        checked: modelData.selected === true
                                        onToggled: function (isChecked) {
                                            ai.setPendingSelected(index, isChecked)
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: (modelData.title ?? "") + " · " + (modelData.summary ?? "")
                                        color: Theme.text
                                        font.pixelSize: Theme.fontBody
                                        font.bold: true
                                        wrapMode: Text.WordWrap
                                    }
                                    AppButton {
                                        variant: "subtle"
                                        text: qsTr("只看这条")
                                        onClicked: {
                                            const r = ai.applyPending([index])
                                            root.reportApplied(r)
                                        }
                                    }
                                    AppButton {
                                        variant: "subtle"
                                        text: qsTr("移除")
                                        onClicked: ai.removePending([index])
                                    }
                                }
                                Repeater {
                                    model: modelData.details ?? []
                                    delegate: Text {
                                        required property string modelData
                                        Layout.fillWidth: true
                                        text: "· " + modelData
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontCaption
                                        wrapMode: Text.WordWrap
                                    }
                                }
                                Repeater {
                                    model: modelData.warnings ?? []
                                    delegate: Text {
                                        required property string modelData
                                        Layout.fillWidth: true
                                        text: "⚠ " + modelData
                                        color: Theme.warning
                                        font.pixelSize: Theme.fontCaption
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }
                        }
                    }

                    Item { Layout.preferredHeight: Theme.spacingL }
                }
            }
        }

        // 底部：输入 + 动作
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingS
            AppTextField {
                id: promptField
                Layout.fillWidth: true
                placeholder: ai.busy ? qsTr("AI 正在思考…") : qsTr("说一句你要怎么改课表，例如：全年级每天第 8 节自习")
                enabled: !ai.busy
                onAccepted: root.send(text)
            }
            AppButton {
                variant: "primary"
                text: ai.busy ? qsTr("停止") : qsTr("发送")
                onClicked: ai.busy ? ai.cancel() : root.send(promptField.text)
            }
            AppButton {
                text: qsTr("应用选中（%1）").arg(ai.pendingCount)
                enabled: ai.pendingCount > 0
                onClicked: root.reportApplied(ai.applyPending([]))
            }
            AppButton {
                variant: "danger"
                text: qsTr("全部拒绝")
                enabled: ai.pendingCount > 0
                onClicked: ai.removePending([])
            }
            AppButton {
                variant: "subtle"
                text: qsTr("撤销本次 AI 变更")
                enabled: ai.canUndo
                onClicked: ai.undo()
            }
        }

        Text {
            Layout.fillWidth: true
            visible: ai.lastError !== ""
            text: ai.lastError
            color: Theme.danger
            font.pixelSize: Theme.fontCaption
            wrapMode: Text.WordWrap
        }
    }

    function reportApplied(result) {
        if (!result || result.applied === undefined)
            return
    }

    // 连接设置
    Popup {
        id: settingsDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 520
        padding: 0
        background: Rectangle {
            radius: Theme.radiusL
            color: Theme.surface
            border.color: Theme.border
        }
        Overlay.modal: Rectangle {
            color: Theme.dark ? Qt.rgba(0, 0, 0, 0.55) : Qt.rgba(15, 23, 42, 0.28)
        }
        contentItem: ColumnLayout {
            spacing: Theme.spacingS
            Text {
                Layout.topMargin: Theme.spacingL
                Layout.leftMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                text: qsTr("AI 连接设置")
                font.pixelSize: Theme.fontSubtitle
                font.bold: true
                color: Theme.text
            }
            Text {
                Layout.leftMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                Layout.fillWidth: true
                text: qsTr("默认走「智排课」官方中继（大模型密钥只保存在服务端）。企业内网可改成自建中继地址。")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.WordWrap
            }
            GridLayout {
                Layout.leftMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                Layout.fillWidth: true
                columns: 2
                rowSpacing: Theme.spacingS
                columnSpacing: Theme.spacingM
                Text { text: qsTr("中继地址"); color: Theme.text; font.pixelSize: Theme.fontBody }
                AppTextField {
                    id: relayField
                    Layout.fillWidth: true
                    text: ai.relayUrl
                    showClear: false
                }
                Text { text: qsTr("应用令牌"); color: Theme.text; font.pixelSize: Theme.fontBody }
                AppTextField {
                    id: tokenField
                    Layout.fillWidth: true
                    text: ai.appToken
                    showClear: false
                }
            }
            RowLayout {
                Layout.leftMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                Layout.bottomMargin: Theme.spacingL
                Layout.topMargin: Theme.spacingS
                Layout.fillWidth: true
                spacing: Theme.spacingS
                Item { Layout.fillWidth: true }
                AppButton { text: qsTr("取消"); onClicked: settingsDialog.close() }
                AppButton {
                    variant: "primary"
                    text: qsTr("保存")
                    onClicked: {
                        ai.relayUrl = relayField.text
                        ai.appToken = tokenField.text
                        settingsDialog.close()
                    }
                }
            }
        }
    }
}
