#pragma once

#include "EngineTypes.h"
#include "ProjectStore.h"
#include "model/School.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace sched {
class SchedulerEngine;
}

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
    Q_PROPERTY(bool hasSchedule READ hasSchedule NOTIFY scheduleChanged)
    Q_PROPERTY(int assignmentsCount READ assignmentsCount NOTIFY scheduleChanged)
    Q_PROPERTY(bool schedulerRunning READ schedulerRunning NOTIFY schedulerRunningChanged)
    Q_PROPERTY(int schedulerProgress READ schedulerProgress NOTIFY schedulerProgressed)
    Q_PROPERTY(QString schedulerRunInfo READ schedulerRunInfo NOTIFY schedulerProgressed)
    Q_PROPERTY(QVariantMap schedulerReport READ schedulerReport NOTIFY schedulerProgressed)
    Q_PROPERTY(bool resultPending READ resultPending NOTIFY resultPendingChanged)
    Q_PROPERTY(QVariantMap printSettings READ printSettings WRITE setPrintSettings NOTIFY printSettingsChanged)
    Q_PROPERTY(bool importPending READ importPending NOTIFY importPendingChanged)

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
    void notifyScheduleChanged() { Q_EMIT scheduleChanged(); } // 供调课命令回写后通知

    // ---- 限制条件（不排时间）：entityType 0=班级 1=课程 2=老师 3=教室 ----
    Q_INVOKABLE QVariantList noScheduleSlots(int entityType, qint64 entityId) const;
    Q_INVOKABLE void setNoScheduleSlot(int entityType, qint64 entityId, int slot, bool forbidden);
    Q_INVOKABLE void clearNoSchedule(int entityType, qint64 entityId);

    // ---- 固定预排 ----
    Q_INVOKABLE QVariantList preAssignments(qint64 classId) const; // [{courseId, courseName, slotIndex}]
    Q_INVOKABLE void togglePreAssignment(qint64 classId, qint64 courseId, int slot);
    Q_INVOKABLE QVariantList planCourses(qint64 classId) const;    // 该班计划内课程 [{id, name}]
    Q_INVOKABLE QVariantList courseTeachers(qint64 classId, qint64 courseId) const; // 任课老师 id 列表

    // ---- 老师活动时间 ----
    Q_INVOKABLE QVariantList teacherActivities(qint64 teacherId) const; // [{slotIndex, courseId, courseName}]
    Q_INVOKABLE void toggleTeacherActivity(qint64 teacherId, int slot, qint64 courseId);

    // ---- 自动排课 ----
    bool schedulerRunning() const { return m_schedulerRunning; }
    int schedulerProgress() const { return m_schedulerProgress; }
    QString schedulerRunInfo() const { return m_schedulerRunInfo; }
    QVariantMap schedulerReport() const { return m_schedulerReport; }
    bool resultPending() const { return m_resultPending; }
    bool hasSchedule() const { return !m_school.schedule.assignments.isEmpty(); }
    int assignmentsCount() const { return m_school.schedule.assignments.size(); }

    // options: runs / maxIterations / seed / randomSeed / placeAll
    Q_INVOKABLE void runScheduler(const QVariantMap &options);
    Q_INVOKABLE void cancelScheduler();
    Q_INVOKABLE void applyResult();   // 引擎结果写入 School（替换旧课表，置脏）
    Q_INVOKABLE void discardResult();
    Q_INVOKABLE QVariantList precheck() const; // 数据健康检查 [{level: ok|warn|err, text}]

    // ---- 课表视图数据（只读）----
    Q_INVOKABLE QVariantList classRow(qint64 classId) const;   // 35 格 {text,sub,courseId,half,major,pre}
    Q_INVOKABLE QVariantList teacherRow(qint64 teacherId) const; // 35 格 [{text,sub,classId,courseId,half}]
    Q_INVOKABLE QVariantList roomRow(qint64 roomId) const;       // 35 格 [{text,sub}]
    Q_INVOKABLE QVariantList masterGrid() const; // [{classId, className, cells:[35 × {text,courseId,major}]}]

    // ---- Excel 批量导入 ----
    Q_INVOKABLE bool writeImportTemplate(const QString &pathOrUrl);
    // 预览：导入到暂存副本，返回 {created, updated, skipped, errors[]}
    Q_INVOKABLE QVariantMap excelImportPreview(const QString &pathOrUrl, bool overwrite);
    Q_INVOKABLE bool excelImportConfirm(); // 用暂存副本替换当前数据（置脏）
    Q_INVOKABLE void excelImportCancel();
    bool importPending() const { return m_importPending; }

    // ---- 示例学校与新手引导 ----
    // 一键生成示例中学（6 班/11 课/14 师，含合班/连课/单双周/不排/预排/教室容量），
    // autoSchedule=true 时自动跑 2 轮引擎并直接应用结果
    Q_INVOKABLE void createSampleSchool(bool autoSchedule = true);
    Q_INVOKABLE bool onboardingShown() const;
    Q_INVOKABLE void setOnboardingShown(bool shown);

    // ---- 打印设置（随项目持久化）----
    QVariantMap printSettings() const { return m_printSettings; }
    void setPrintSettings(const QVariantMap &settings);

Q_SIGNALS:
    void projectChanged();
    void projectDirtyChanged();
    void statsChanged();
    void recentFilesChanged();
    void constraintsChanged(); // 不排时间/预排/老师活动变更
    void scheduleChanged();    // 课表内容变更（应用排课结果后）
    void schedulerRunningChanged();
    void schedulerProgressed();
    void schedulerFinished();
    void resultPendingChanged();
    void importPendingChanged();
    void printSettingsChanged();

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

    // Excel 导入暂存
    sched::School m_importStaging;
    QVariantMap m_importReport;
    bool m_importPending = false;
    QVariantMap m_printSettings;
    bool m_sampleAutoApply = false; // 示例学校生成后自动应用排课结果

    // 自动排课状态
    sched::SchedulerEngine *m_engine = nullptr;
    bool m_schedulerRunning = false;
    int m_schedulerProgress = 0;
    QString m_schedulerRunInfo;
    QVariantMap m_schedulerReport;
    bool m_resultPending = false;
    sched::Schedule m_pendingResult;
    sched::QualityReport m_pendingReport;
};
