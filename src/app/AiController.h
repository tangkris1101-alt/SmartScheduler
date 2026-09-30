#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class AppController;
class QNetworkAccessManager;
class QNetworkReply;

// AI 编排客户端：把一句话需求发给中继（api.class.feimsg.cn），由大模型产出工具调用，
// 只读工具立即执行并回灌结果；变更工具一律**挂起为待办卡**，等用户在界面上勾选后
// 由 AppController::applyAiOperations() 一次性应用（整体可撤销）。
class AiController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString streamText READ streamText NOTIFY streamTextChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QString relayUrl READ relayUrl WRITE setRelayUrl NOTIFY configChanged)
    Q_PROPERTY(QString appToken READ appToken WRITE setAppToken NOTIFY configChanged)
    Q_PROPERTY(QVariantList transcript READ transcript NOTIFY transcriptChanged)
    Q_PROPERTY(QVariantList pending READ pending NOTIFY pendingChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY pendingChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY pendingChanged)

public:
    explicit AiController(AppController *app, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QString streamText() const { return m_streamText; }
    QString lastError() const { return m_lastError; }
    QString relayUrl() const { return m_relayUrl; }
    QString appToken() const { return m_appToken; }
    QVariantList transcript() const { return m_transcript; }
    QVariantList pending() const { return m_pending; }
    int pendingCount() const { return m_pending.size(); }
    bool canUndo() const;

    void setRelayUrl(const QString &url);
    void setAppToken(const QString &token);

    Q_INVOKABLE void sendPrompt(const QString &text);
    Q_INVOKABLE void cancel();          // 中断当前请求
    Q_INVOKABLE void clearTranscript();
    // 应用选中的待办（indices 为待办序号；空表示全部勾选项）
    Q_INVOKABLE QVariantMap applyPending(const QVariantList &indices);
    Q_INVOKABLE void removePending(const QVariantList &indices); // 仅移除待办，不落盘
    Q_INVOKABLE void setPendingSelected(int index, bool selected);
    Q_INVOKABLE bool undo();            // 撤销上一次 AI 变更

Q_SIGNALS:
    void stateChanged();
    void streamTextChanged();
    void transcriptChanged();
    void pendingChanged();
    void configChanged();

private:
    void postToRelay();
    void consumeSse(const QByteArray &chunk);
    void finishRound();
    void addMessage(const QString &role, const QString &content, const QVariantList &toolCalls = {});
    void appendTranscript(const QString &role, const QString &text);
    QString buildSystemPrompt() const;
    void setError(const QString &msg);

    AppController *m_app = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QVector<QVariantMap> m_messages;       // OpenAI 格式的消息历史
    QVariantList m_transcript;             // 界面展示用（role + text）
    QVariantList m_pending;                // 待办卡
    QMap<int, QVariantMap> m_toolBuf;      // 流式 tool_call 累积（按 index）
    QString m_streamText;
    QString m_pendingAssistant;            // 本轮 assistant 的可见文本
    QString m_lastError;
    QString m_relayUrl;
    QString m_appToken;
    QByteArray m_sseBuffer;
    bool m_busy = false;
    int m_round = 0;
};
