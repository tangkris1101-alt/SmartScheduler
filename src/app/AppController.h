#pragma once

#include "ProjectStore.h"
#include "model/School.h"

#include <QObject>
#include <QString>
#include <QStringList>

// 应用控制器：QML 与领域层之间的桥。持有内存中的 School 聚合根与 ProjectStore，
// 负责项目的新建/打开/保存与脏标记；名单模型通过 school() 直接读写聚合根。
class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasProject READ hasProject NOTIFY projectChanged)
    Q_PROPERTY(QString currentProjectPath READ currentProjectPath NOTIFY projectChanged)
    Q_PROPERTY(bool projectDirty READ projectDirty NOTIFY projectDirtyChanged)
    Q_PROPERTY(QString schoolName READ schoolName NOTIFY projectChanged)
    Q_PROPERTY(QStringList recentFiles READ recentFiles NOTIFY recentFilesChanged)
    Q_PROPERTY(int classCount READ classCount NOTIFY statsChanged)
    Q_PROPERTY(int courseCount READ courseCount NOTIFY statsChanged)
    Q_PROPERTY(int teacherCount READ teacherCount NOTIFY statsChanged)
    Q_PROPERTY(int classroomCount READ classroomCount NOTIFY statsChanged)
    Q_PROPERTY(int planEntryCount READ planEntryCount NOTIFY statsChanged)
    Q_PROPERTY(int daysPerWeek READ daysPerWeek NOTIFY projectChanged)
    Q_PROPERTY(int periodsPerDay READ periodsPerDay NOTIFY projectChanged)
    Q_PROPERTY(int morningPeriods READ morningPeriods NOTIFY projectChanged)
    Q_PROPERTY(int weeksPerTerm READ weeksPerTerm NOTIFY projectChanged)

public:
    explicit AppController(QObject *parent = nullptr);

    bool hasProject() const { return m_hasProject; }
    QString currentProjectPath() const { return m_currentPath; }
    bool projectDirty() const { return m_dirty; }
    QString schoolName() const { return m_school.name; }
    QStringList recentFiles() const;

    int classCount() const { return m_school.classes.size(); }
    int courseCount() const { return m_school.courses.size(); }
    int teacherCount() const { return m_school.teachers.size(); }
    int classroomCount() const { return m_school.classrooms.size(); }
    int planEntryCount() const { return m_school.planEntries.size(); }
    int daysPerWeek() const { return m_school.timeConfig.daysPerWeek; }
    int periodsPerDay() const { return m_school.timeConfig.periodsPerDay; }
    int morningPeriods() const { return m_school.timeConfig.morningPeriods; }
    int weeksPerTerm() const { return m_school.timeConfig.weeksPerTerm; }

    Q_INVOKABLE void newProject(const QString &name, int daysPerWeek, int periodsPerDay,
                                int morningPeriods, int weeksPerTerm);
    Q_INVOKABLE bool openProject(const QString &pathOrUrl);
    Q_INVOKABLE bool saveProject();    // 无路径时返回 false（QML 弹出另存对话框）
    Q_INVOKABLE bool saveProjectAs(const QString &pathOrUrl);
    Q_INVOKABLE void closeProject();
    Q_INVOKABLE QString lastError() const { return m_lastError; }

    sched::School *school() { return &m_school; }
    const sched::School *school() const { return &m_school; }
    void markDirty(); // 编辑回写后调用：置脏并刷新统计

    // ---- 限制条件（不排时间）：entityType 0=班级 1=课程 2=老师 3=教室 ----
    Q_INVOKABLE QVariantList noScheduleSlots(int entityType, qint64 entityId) const;
    Q_INVOKABLE void setNoScheduleSlot(int entityType, qint64 entityId, int slot, bool forbidden);
    Q_INVOKABLE void clearNoSchedule(int entityType, qint64 entityId);

    // ---- 固定预排 ----
    Q_INVOKABLE QVariantList preAssignments(qint64 classId) const; // [{courseId, courseName, slotIndex}]
    Q_INVOKABLE void togglePreAssignment(qint64 classId, qint64 courseId, int slot);
    Q_INVOKABLE QVariantList planCourses(qint64 classId) const;    // 该班计划内课程 [{id, name}]

    // ---- 老师活动时间 ----
    Q_INVOKABLE QVariantList teacherActivities(qint64 teacherId) const; // [{slotIndex, courseId, courseName}]
    Q_INVOKABLE void toggleTeacherActivity(qint64 teacherId, int slot, qint64 courseId);

Q_SIGNALS:
    void projectChanged();
    void projectDirtyChanged();
    void statsChanged();
    void recentFilesChanged();
    void constraintsChanged(); // 不排时间/预排/老师活动变更

private:
    static QString toLocalPath(const QString &pathOrUrl);
    bool saveToPath(const QString &path);
    void addRecentFile(const QString &path);

    sched::School m_school;
    sched::ProjectStore m_store;
    QString m_currentPath;
    QString m_lastError;
    bool m_hasProject = false;
    bool m_dirty = false;
};
