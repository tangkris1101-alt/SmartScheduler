#include "AiController.h"

#include "AppController.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrl>

namespace {
constexpr int kMaxRounds = 4;        // 一次提问最多几轮工具调用
constexpr int kMaxPending = 20;      // 单轮最多挂起多少条变更（与设计约定一致）
const char *kDefaultRelay = "https://api.class.feimsg.cn";
// 应用令牌（应用级，可轮换；服务端 .env 中为 APP_TOKEN）
const char *kDefaultToken =
    "2627d9f4b6ec2d20892bb682276b650ca046d6b802dd6616a154af0d3903a92c";
} // namespace

AiController::AiController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_nam(new QNetworkAccessManager(this))
{
    QSettings s;
    m_relayUrl = s.value(QStringLiteral("ai/relay"), QString::fromLatin1(kDefaultRelay)).toString();
    m_appToken = s.value(QStringLiteral("ai/token"), QString::fromLatin1(kDefaultToken)).toString();
}

void AiController::setRelayUrl(const QString &url)
{
    const QString trimmed = url.trimmed();
    if (trimmed == m_relayUrl || trimmed.isEmpty())
        return;
    m_relayUrl = trimmed;
    QSettings().setValue(QStringLiteral("ai/relay"), m_relayUrl);
    Q_EMIT configChanged();
}

void AiController::setAppToken(const QString &token)
{
    const QString trimmed = token.trimmed();
    if (trimmed == m_appToken)
        return;
    m_appToken = trimmed;
    QSettings().setValue(QStringLiteral("ai/token"), m_appToken);
    Q_EMIT configChanged();
}

bool AiController::canUndo() const
{
    return m_app && m_app->canUndoBatch();
}

QString AiController::buildSystemPrompt() const
{
    const sched::School *s = m_app->school();
    QStringList classes;
    for (const auto &c : s->classes)
        classes << c.name;
    QStringList courses;
    for (const auto &c : s->courses)
        courses << c.name;
    return QStringLiteral(
               "你是「智排课系统」的排课助手。你只能通过工具读写数据，不要凭空猜测班级或课程名。\n"
               "当前项目：%1；共 %2 个班、%3 位老师；每周 %4 天、每天 %5 节（上午 %6 节）。\n"
               "班名：%7\n课程：%8\n"
               "规则：\n"
               "1. 不确定的班名/课程名先用 list_* / query_* 工具查清楚；\n"
               "2. 批量需求优先用 batch_* 工具（例如「全年级每天第 8 节自习」= batch_preassign）；\n"
               "3. 一次最多提出 %9 条变更，超了就分批并告诉用户；\n"
               "4. 你的变更不会立刻生效：系统会把它们挂起给用户确认，你只需说明你做了什么、影响多少；\n"
               "5. 用简洁中文回答，不要说「已修改完成」，而要说「已登记 N 项待确认」。")
        .arg(s->name)
        .arg(s->classes.size())
        .arg(s->teachers.size())
        .arg(s->timeConfig.daysPerWeek)
        .arg(s->timeConfig.periodsPerDay)
        .arg(s->timeConfig.morningPeriods)
        .arg(classes.isEmpty() ? QStringLiteral("（无）") : classes.join(QStringLiteral("、")),
             courses.isEmpty() ? QStringLiteral("（无）") : courses.join(QStringLiteral("、")))
        .arg(kMaxPending);
}

void AiController::appendTranscript(const QString &role, const QString &text)
{
    QVariantMap m{{QStringLiteral("role"), role}, {QStringLiteral("text"), text}};
    m_transcript.append(m);
    Q_EMIT transcriptChanged();
}

void AiController::addMessage(const QString &role, const QString &content,
                              const QVariantList &toolCalls)
{
    QVariantMap m{{QStringLiteral("role"), role}};
    if (!content.isEmpty() || toolCalls.isEmpty())
        m.insert(QStringLiteral("content"), content);
    if (!toolCalls.isEmpty()) {
        QVariantList tcs;
        for (const QVariant &v : toolCalls)
            tcs.append(v);
        m.insert(QStringLiteral("tool_calls"), tcs);
    }
    m_messages.append(m);
}

void AiController::setError(const QString &msg)
{
    m_lastError = msg;
    if (!msg.isEmpty())
        appendTranscript(QStringLiteral("error"), msg);
    Q_EMIT stateChanged();
}

void AiController::sendPrompt(const QString &text)
{
    const QString prompt = text.trimmed();
    if (prompt.isEmpty() || m_busy)
        return;
    if (!m_app->hasProject()) {
        setError(tr("请先新建或打开一个项目，再让 AI 帮你排课"));
        return;
    }
    if (m_messages.isEmpty())
        addMessage(QStringLiteral("system"), buildSystemPrompt());
    addMessage(QStringLiteral("user"), prompt);
    appendTranscript(QStringLiteral("user"), prompt);
    m_lastError.clear();
    m_round = 0;
    m_pending.clear();
    Q_EMIT pendingChanged();
    postToRelay();
}

void AiController::cancel()
{
    if (m_reply) {
        m_reply->abort();
        m_reply = nullptr;
    }
    m_busy = false;
    Q_EMIT stateChanged();
}

void AiController::clearTranscript()
{
    m_transcript.clear();
    m_messages.clear();
    Q_EMIT transcriptChanged();
}

void AiController::postToRelay()
{
    if (m_round++ >= kMaxRounds) {
        m_busy = false;
        appendTranscript(QStringLiteral("assistant"),
                         tr("（已达单次对话的工具调用上限，先按上面的待办确认吧）"));
        Q_EMIT stateChanged();
        return;
    }
    // 工具清单随请求下发：客户端加工具不需要改服务端
    QVariantList tools;
    for (const QVariant &v : m_app->aiTools()) {
        tools.append(QVariantMap{{QStringLiteral("type"), QStringLiteral("function")},
                                 {QStringLiteral("function"), v}});
    }
    QVariantMap body{{QStringLiteral("messages"), QVariantList(m_messages.begin(), m_messages.end())},
                     {QStringLiteral("tools"), tools},
                     {QStringLiteral("tool_choice"), QStringLiteral("auto")},
                     {QStringLiteral("stream"), true}};

    QNetworkRequest req{QUrl(m_relayUrl + QStringLiteral("/v1/ai/orchestrate"))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("X-App-Token", m_appToken.toUtf8());
    req.setRawHeader("X-Client-Version", QByteArrayLiteral("1.0.0"));
    req.setTransferTimeout(120000);

    m_streamText.clear();
    m_pendingAssistant.clear();
    m_toolBuf.clear();
    m_sseBuffer.clear();
    m_busy = true;
    Q_EMIT stateChanged();
    Q_EMIT streamTextChanged();

    QNetworkReply *reply = m_nam->post(req, QJsonDocument(QJsonObject::fromVariantMap(body))
                                                .toJson(QJsonDocument::Compact));
    m_reply = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        consumeSse(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const auto err = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (m_reply == reply)
            m_reply = nullptr;
        if (err != QNetworkReply::NoError && err != QNetworkReply::OperationCanceledError) {
            const QString detail = QString::fromUtf8(reply->readAll()).left(300);
            QJsonParseError pe{};
            const QJsonDocument d = QJsonDocument::fromJson(detail.toUtf8(), &pe);
            const QString msg = d.isObject() && d.object().contains(QStringLiteral("error"))
                                    ? d.object().value(QStringLiteral("error")).toString()
                                    : reply->errorString();
            m_busy = false;
            setError(tr("AI 服务不可用（%1 %2）：%3").arg(status).arg(reply->errorString(), msg));
            return;
        }
        finishRound();
    });
}

void AiController::consumeSse(const QByteArray &chunk)
{
    m_sseBuffer += chunk;
    for (;;) {
        const int idx = m_sseBuffer.indexOf('\n');
        if (idx < 0)
            break;
        const QByteArray line = m_sseBuffer.left(idx).trimmed();
        m_sseBuffer.remove(0, idx + 1);
        if (!line.startsWith("data:"))
            continue;
        const QByteArray json = line.mid(5).trimmed();
        if (json.isEmpty() || json == "[DONE]")
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(json);
        if (!doc.isObject())
            continue;
        const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty())
            continue;
        const QJsonObject delta = choices.first().toObject().value(QStringLiteral("delta")).toObject();
        const QString content = delta.value(QStringLiteral("content")).toString();
        if (!content.isEmpty()) {
            m_streamText += content;
            m_pendingAssistant += content;
            Q_EMIT streamTextChanged();
        }
        const QJsonArray tcs = delta.value(QStringLiteral("tool_calls")).toArray();
        for (const QJsonValue &tv : tcs) {
            const QJsonObject t = tv.toObject();
            const int index = t.value(QStringLiteral("index")).toInt(0);
            QVariantMap acc = m_toolBuf.value(index);
            const QString id = t.value(QStringLiteral("id")).toString();
            if (!id.isEmpty())
                acc.insert(QStringLiteral("id"), id);
            const QJsonObject fn = t.value(QStringLiteral("function")).toObject();
            if (!fn.value(QStringLiteral("name")).toString().isEmpty())
                acc.insert(QStringLiteral("name"), fn.value(QStringLiteral("name")).toString());
            const QString args = fn.value(QStringLiteral("arguments")).toString();
            if (!args.isEmpty())
                acc.insert(QStringLiteral("args"),
                           acc.value(QStringLiteral("args")).toString() + args);
            m_toolBuf.insert(index, acc);
        }
    }
}

void AiController::finishRound()
{
    // 本轮若无工具调用 → 对话结束
    if (m_toolBuf.isEmpty()) {
        if (!m_pendingAssistant.trimmed().isEmpty())
            appendTranscript(QStringLiteral("assistant"), m_pendingAssistant.trimmed());
        m_streamText.clear();
        m_busy = false;
        Q_EMIT streamTextChanged();
        Q_EMIT stateChanged();
        return;
    }

    QVariantList toolCallsForMessage;
    QVariantList toolResults;
    QSet<QString> readOnlyNames{QStringLiteral("list_classes"), QStringLiteral("list_courses"),
                                QStringLiteral("list_teachers"), QStringLiteral("list_groups"),
                                QStringLiteral("query_schedule"), QStringLiteral("query_plan"),
                                QStringLiteral("query_preassign"), QStringLiteral("query_no_schedule"),
                                QStringLiteral("quality_report")};
    for (auto it = m_toolBuf.begin(); it != m_toolBuf.end(); ++it) {
        const QVariantMap tc = it.value();
        const QString id = tc.value(QStringLiteral("id")).toString();
        const QString name = tc.value(QStringLiteral("name")).toString();
        const QString argsJson = tc.value(QStringLiteral("args")).toString();
        toolCallsForMessage.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("type"), QStringLiteral("function")},
            {QStringLiteral("function"),
             QVariantMap{{QStringLiteral("name"), name},
                         {QStringLiteral("arguments"), argsJson}}}});

        QVariantMap args;
        const QJsonDocument ad = QJsonDocument::fromJson(argsJson.toUtf8());
        if (ad.isObject())
            args = ad.object().toVariantMap();

        QVariantMap result;
        if (readOnlyNames.contains(name)) {
            result = m_app->aiRunReadTool(name, args);
            appendTranscript(QStringLiteral("tool"),
                             tr("查询 %1").arg(name));
        } else {
            const QVariantMap dry = m_app->aiDescribe(name, args);
            if (!dry.value(QStringLiteral("ok")).toBool()) {
                result = QVariantMap{{QStringLiteral("ok"), false},
                                     {QStringLiteral("error"),
                                      dry.value(QStringLiteral("error")).toString()}};
            } else if (m_pending.size() >= kMaxPending) {
                result = QVariantMap{{QStringLiteral("ok"), false},
                                     {QStringLiteral("error"),
                                      tr("单次最多挂起 %1 项变更，请让用户先确认这批")
                                          .arg(kMaxPending)}};
            } else {
                QVariantMap card = dry;
                card.insert(QStringLiteral("tool"), name);
                card.insert(QStringLiteral("args"), args);
                card.insert(QStringLiteral("selected"), true);
                m_pending.append(card);
                Q_EMIT pendingChanged();
                result = QVariantMap{{QStringLiteral("ok"), true},
                                     {QStringLiteral("status"),
                                      tr("已登记为待确认操作（用户尚未批准，不要声称已生效）")},
                                     {QStringLiteral("summary"),
                                      dry.value(QStringLiteral("summary")).toString()}};
            }
        }
        toolResults.append(QVariantMap{{QStringLiteral("role"), QStringLiteral("tool")},
                                       {QStringLiteral("tool_call_id"), id},
                                       {QStringLiteral("content"),
                                        QString::fromUtf8(QJsonDocument(
                                                QJsonObject::fromVariantMap(result))
                                                              .toJson(QJsonDocument::Compact))}});
    }

    if (!m_pendingAssistant.trimmed().isEmpty())
        appendTranscript(QStringLiteral("assistant"), m_pendingAssistant.trimmed());
    addMessage(QStringLiteral("assistant"), QString(), toolCallsForMessage);

    m_messages.append(toolResults.first().toMap());
    for (int i = 1; i < toolResults.size(); ++i)
        m_messages.append(toolResults.at(i).toMap());

    QTimer::singleShot(0, this, [this]() { postToRelay(); });
}

QVariantMap AiController::applyPending(const QVariantList &indices)
{
    QVariantList ops;
    QSet<int> chosen;
    if (indices.isEmpty()) {
        for (int i = 0; i < m_pending.size(); ++i) {
            if (m_pending.at(i).toMap().value(QStringLiteral("selected")).toBool())
                chosen.insert(i);
        }
    } else {
        for (const QVariant &v : indices)
            chosen.insert(v.toInt());
    }
    for (int i : chosen) {
        if (i < 0 || i >= m_pending.size())
            continue;
        const QVariantMap card = m_pending.at(i).toMap();
        ops.append(QVariantMap{{QStringLiteral("tool"), card.value(QStringLiteral("tool"))},
                               {QStringLiteral("args"), card.value(QStringLiteral("args"))}});
    }
    const QVariantMap result = m_app->applyAiOperations(ops);
    // 移除已应用的待办（保留未选中的）
    QVariantList rest;
    for (int i = 0; i < m_pending.size(); ++i) {
        if (!chosen.contains(i))
            rest.append(m_pending.at(i));
    }
    m_pending = rest;
    Q_EMIT pendingChanged();
    appendTranscript(QStringLiteral("assistant"),
                     tr("已应用 %1 项变更%2")
                         .arg(result.value(QStringLiteral("applied")).toInt())
                         .arg(result.value(QStringLiteral("changed")).toBool()
                                  ? tr("（可点「撤销本次 AI 变更」整体回退）")
                                  : tr("（没有实际变化）")));
    return result;
}

void AiController::removePending(const QVariantList &indices)
{
    QSet<int> drop;
    for (const QVariant &v : indices)
        drop.insert(v.toInt());
    QVariantList rest;
    for (int i = 0; i < m_pending.size(); ++i) {
        if (!drop.contains(i))
            rest.append(m_pending.at(i));
    }
    m_pending = rest;
    Q_EMIT pendingChanged();
}

void AiController::setPendingSelected(int index, bool selected)
{
    if (index < 0 || index >= m_pending.size())
        return;
    QVariantMap card = m_pending.at(index).toMap();
    card.insert(QStringLiteral("selected"), selected);
    m_pending[index] = card;
    Q_EMIT pendingChanged();
}

bool AiController::undo()
{
    const bool ok = m_app->undoLastBatch();
    if (ok)
        appendTranscript(QStringLiteral("assistant"), tr("已撤销上一次 AI 变更"));
    Q_EMIT pendingChanged();
    return ok;
}
