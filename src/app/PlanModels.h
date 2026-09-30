#pragma once

#include "model/School.h"

#include <QAbstractListModel>

class AppController;

// 教学计划条目模型：按 currentClassId 过滤某班的计划行
// roles 覆盖 PlanEntry 全字段，另提供课程名/老师名/教室名解析与校验提示角色
class PlanListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(qint64 currentClassId READ currentClassId WRITE setCurrentClassId NOTIFY currentClassIdChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(double totalWeeklyPeriods READ totalWeeklyPeriods NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        ClassIdRole,
        CourseIdRole,
        CourseNameRole,
        TeacherIdsRole,
        TeacherNamesRole,
        WeeklyPeriodsRole,
        ChainedCountRole,
        ChainLengthRole,
        MorningPeriodsRole,
        EarlyPeriodsRole,
        IsCombinedRole,
        ClassroomIdRole,
        ClassroomNameRole,
        StartWeekRole,
        EndWeekRole,
        SyncCourseIdRole,
        SyncCourseNameRole,
        ProgressGroupRole,
        DuplicateRole,   // 同班同课重复（红色警告）
        TeacherWarnRole  // 老师不在课程教研组（黄色提示，不强制）
    };

    explicit PlanListModel(AppController *app, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    qint64 currentClassId() const { return m_classId; }
    void setCurrentClassId(qint64 id);
    int count() const { return m_view.size(); }
    double totalWeeklyPeriods() const;

    Q_INVOKABLE qint64 addEntry(qint64 courseId); // 新课程行（默认 2 节），返回条目 id
    Q_INVOKABLE bool removeRow(int row);
    Q_INVOKABLE QVariant get(int row, const QString &roleName) const;
    Q_INVOKABLE bool set(int row, const QString &roleName, const QVariant &value);
    Q_INVOKABLE void toggleTeacher(int row, qint64 teacherId);
    // 把当前班全部计划复制到目标班；overwrite=true 时同课条目覆盖，否则跳过。返回复制条数
    Q_INVOKABLE int copyToClass(qint64 targetClassId, bool overwrite);

Q_SIGNALS:
    void currentClassIdChanged();
    void countChanged();

private:
    void fullReset();
    sched::PlanEntry *entryAtRow(int row) const;
    const sched::PlanEntry *entryAt(int sourceRow) const;
    int roleForName(const QString &name) const;

    AppController *m_app;
    qint64 m_classId = -1;
    QVector<int> m_view; // 可见行 → planEntries 源行
};

// 老师任课视图模型：按 currentTeacherId 列出其任教条目（班级×课程×节数×进度组）
class DutyModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(qint64 currentTeacherId READ currentTeacherId WRITE setCurrentTeacherId NOTIFY currentTeacherIdChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        EntryIdRole = Qt::UserRole + 1,
        ClassIdRole,
        ClassNameRole,
        CourseIdRole,
        CourseNameRole,
        WeeklyPeriodsRole,
        ProgressGroupRole
    };

    explicit DutyModel(AppController *app, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    qint64 currentTeacherId() const { return m_teacherId; }
    void setCurrentTeacherId(qint64 id);
    int count() const { return m_view.size(); }

    // 批量设置任教：对选中各班生成/更新 (班,课) 条目并加入当前老师；返回影响条数
    Q_INVOKABLE int setDuty(qint64 courseId, const QList<qint64> &classIds, double weeklyPeriods);
    Q_INVOKABLE bool removeRow(int row); // 解除任教（从条目中摘掉当前老师）
    Q_INVOKABLE QVariant get(int row, const QString &roleName) const;
    Q_INVOKABLE bool set(int row, const QString &roleName, const QVariant &value);

Q_SIGNALS:
    void currentTeacherIdChanged();
    void countChanged();

private:
    void fullReset();

    AppController *m_app;
    qint64 m_teacherId = -1;
    QVector<int> m_view;
};
