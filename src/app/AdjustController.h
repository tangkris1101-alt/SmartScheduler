#pragma once

#include "model/School.h"

#include <QObject>
#include <QUndoStack>
#include <QVariantList>
#include <QVariantMap>

class AppController;

// 手工调课控制器：以班级课表为操作面
// - 实时冲突校验（老师/教室/不排时间/预排锁/合班联动/单双周匹配）
// - 全部改动封装为 QUndoCommand（整表快照式，数据量小，简单可靠）
// - 临时调课模式：进入时快照，退出即丢弃，不置脏不保存
class AdjustController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStackChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStackChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoStackChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY undoStackChanged)
    Q_PROPERTY(bool temporaryMode READ temporaryMode NOTIFY temporaryModeChanged)
    Q_PROPERTY(bool issueHintsEnabled READ issueHintsEnabled WRITE setIssueHintsEnabled NOTIFY issueHintsEnabledChanged)
    Q_PROPERTY(QVariantMap bufferContent READ bufferContent NOTIFY bufferChanged)

public:
    explicit AdjustController(AppController *app, QObject *parent = nullptr);

    bool canUndo() const { return m_undo.canUndo(); }
    bool canRedo() const { return m_undo.canRedo(); }
    QString undoText() const { return m_undo.undoText(); }
    QString redoText() const { return m_undo.redoText(); }
    bool temporaryMode() const { return m_temporary; }
    bool issueHintsEnabled() const { return m_issueHints; }
    void setIssueHintsEnabled(bool on);
    QVariantMap bufferContent() const { return m_buffer; }

    // 目标计算：移动 (classId, fromSlot, fromHalf) 的课，返回每个 slot 的状态
    // 0=禁止（灰） 1=可落子（绿，空格） 2=可对调（黄，有课）
    Q_INVOKABLE QVariantList targetsFor(qint64 classId, int fromSlot, int fromHalf) const;
    // 移动/对调；返回 {ok, reason}
    Q_INVOKABLE QVariantMap moveCell(qint64 classId, int fromSlot, int fromHalf, int toSlot, int toHalf);
    // 清除课程（变为未排）
    Q_INVOKABLE QVariantMap unscheduleCell(qint64 classId, int slot, int half);
    // 未排课程：[{courseId, name, missing}]
    Q_INVOKABLE QVariantList unplacedCourses(qint64 classId) const;
    // 安排未排课到指定节次（临时模式下不受计划节数限制）
    Q_INVOKABLE QVariantMap placeUnplaced(qint64 classId, qint64 courseId, int slot, int half);
    // 未排课落子目标：每个 slot 0=禁止 1=可落子
    Q_INVOKABLE QVariantList placeTargetsFor(qint64 classId, qint64 courseId, int half) const;
    // 缺陷提示：连课不连 / 课时不匀 / 落入不排节次
    Q_INVOKABLE QVariantList cellIssues(qint64 classId) const;
    // 某格课程的老师当周占用描述 ["张老师：周一第3节…"]
    Q_INVOKABLE QStringList teacherBusyText(qint64 classId, int slot) const;

    // 缓冲节次（暂存区）：一次存一门课（含 half）
    Q_INVOKABLE QVariantMap bufferStore(qint64 classId, int slot, int half);
    Q_INVOKABLE QVariantMap bufferPlace(int toSlot, int toHalf);
    Q_INVOKABLE void bufferClear();

    // 临时调课
    Q_INVOKABLE void enterTemporary();
    Q_INVOKABLE void exitTemporary(); // 丢弃全部临时改动
    Q_INVOKABLE QString tempNoticeText() const;                    // 调课通知文本
    Q_INVOKABLE bool exportTempNotice(const QString &pathOrUrl);   // .txt 或 .pdf
    Q_INVOKABLE QString lastExportError() const { return m_exportError; }

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

Q_SIGNALS:
    void undoStackChanged();
    void temporaryModeChanged();
    void issueHintsEnabledChanged();
    void bufferChanged();

private:
    struct CellContent {
        qint64 classId = -1;
        qint64 courseId = -1;
        int half = 0;
        bool found = false;
    };

    CellContent cellAt(qint64 classId, int slot, int half) const;
    QVector<QPair<qint64, qint64>> combinedMembers(qint64 classId, qint64 courseId) const;
    // 校验：在 work 表中把 (classId,courseId,half) 放到 slot 是否合法；合法返回空串
    QString placementError(const QVector<sched::ScheduleAssignment> &work, qint64 classId,
                           qint64 courseId, int half, int slot) const;
    QString moveLockError(qint64 classId, int fromSlot, qint64 courseId) const; // 预排锁定检查
    void pushCommand(const QString &text, const QVector<sched::ScheduleAssignment> &after);
    double placedPeriods(qint64 classId, qint64 courseId) const;
    static bool halfOverlap(int a, int b) { return a == 0 || b == 0 || a == b; }

    AppController *m_app;
    QUndoStack m_undo;
    bool m_temporary = false;
    bool m_issueHints = true;
    QVariantMap m_buffer; // {classId, courseId, half}
    QVector<sched::ScheduleAssignment> m_tempSnapshot;
    QString m_exportError;
};
