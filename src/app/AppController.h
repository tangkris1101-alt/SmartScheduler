#pragma once

#include "EngineTypes.h"
#include "ProjectStore.h"
#include "model/School.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

class QNetworkAccessManager;

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
    // 内测门禁与反馈：QML 里要用属性绑定（visible/enabled/text），必须是 Q_PROPERTY
    Q_PROPERTY(bool betaLocked READ betaLocked NOTIFY betaStateChanged)
    Q_PROPERTY(bool betaBusy READ betaBusy NOTIFY betaStateChanged)
    Q_PROPERTY(QString betaMessage READ betaMessage NOTIFY betaStateChanged)
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY betaStateChanged)
    Q_PROPERTY(bool feedbackBusy READ feedbackBusy NOTIFY feedbackStateChanged)
    Q_PROPERTY(bool feedbackOk READ feedbackOk NOTIFY feedbackStateChanged)
    Q_PROPERTY(QString feedbackMessage READ feedbackMessage NOTIFY feedbackStateChanged)
    Q_PROPERTY(int classCount READ classCount NOTIFY statsChanged)
    Q_PROPERTY(int courseCount READ courseCount NOTIFY statsChanged)
    Q_PROPERTY(int teacherCount READ teacherCount NOTIFY statsChanged)
    Q_PROPERTY(int classroomCount READ classroomCount NOTIFY statsChanged)
    Q_PROPERTY(int planEntryCount READ planEntryCount NOTIFY statsChanged)
    Q_PROPERTY(int constraintCount READ constraintCount NOTIFY constraintsChanged) // 不排+预排+老师活动
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
    // ---- 设置页：版本 / 检查更新 / 日志 ----
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
    Q_PROPERTY(int updateStatus READ updateStatus NOTIFY updateStatusChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY updateStatusChanged)
    Q_PROPERTY(QString updateMessage READ updateMessage NOTIFY updateStatusChanged)
    // ---- 应用内自动更新：镜像链下载 + 校验 + 静默安装 ----
    Q_PROPERTY(int updateDlState READ updateDlState NOTIFY updateDlStateChanged)
    Q_PROPERTY(bool updateReady READ updateReady NOTIFY updateDlStateChanged)
    Q_PROPERTY(int updateDlPercent READ updateDlPercent NOTIFY updateDlStateChanged)
    Q_PROPERTY(QString updateDlMessage READ updateDlMessage NOTIFY updateDlStateChanged)
    Q_PROPERTY(QString logFilePath READ logFilePath CONSTANT)

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
    int constraintCount() const
    {
        return m_school.noScheduleConstraints.size() + m_school.preAssignments.size()
               + m_school.teacherActivities.size();
    }
    int daysPerWeek() const { return m_school.timeConfig.daysPerWeek; }
    int periodsPerDay() const { return m_school.timeConfig.periodsPerDay; }
    int morningPeriods() const { return m_school.timeConfig.morningPeriods; }
    int weeksPerTerm() const { return m_school.timeConfig.weeksPerTerm; }

    Q_INVOKABLE void newProject(const QString &name, int daysPerWeek, int periodsPerDay,
                                int morningPeriods, int weeksPerTerm);
    // 修改项目基础信息；每周天数/每天节数变化会使既有课表、不排时间、预排、老师活动全部失效并清空
    Q_INVOKABLE void updateProjectInfo(const QString &name, int daysPerWeek, int periodsPerDay,
                                       int morningPeriods, int weeksPerTerm);
    // 起始模板：0=空白 1=小学 2=初中 3=高中；须在 newProject 之后调用，填充课程/班级/年级组/教室
    Q_INVOKABLE void applyTemplate(int tpl);
    // 最近打开：移除单项 / 清空列表（只改本地记录，不动磁盘文件）
    Q_INVOKABLE void removeRecentFile(const QString &path);
    Q_INVOKABLE void clearRecentFiles();

    // 批量建班并按名称前缀自动建/并入年级组（"高一1班"→"高一年级"）
    // 返回 {created, groups:[{name, memberCount}]}；名称不含数字时不建组
    Q_INVOKABLE QVariantMap addClassesWithAutoGroup(const QStringList &names);

    // ---- 计划/老师批量编辑（界面与 AI 工具共用）----
    Q_INVOKABLE QVariantMap batchSetPlanChained(qint64 courseId, const QVariantList &classIds,
                                                int times, int length);
    Q_INVOKABLE QVariantMap batchSetPlanEarlyPeriods(qint64 courseId, const QVariantList &classIds,
                                                     int earlyPeriods);
    Q_INVOKABLE QVariantMap batchSetPlanTeachers(qint64 courseId, const QVariantList &classIds,
                                                 const QVariantList &teacherIds, bool rotate);
    Q_INVOKABLE QVariantMap batchSetTeacherDailyMax(const QVariantList &teacherIds, int maxPerDay);
    Q_INVOKABLE QVariantMap batchSetNoScheduleByGroup(qint64 groupId, const QVariantList &slots,
                                                      bool forbidden);
    // ---- 同时段组（走班）----
    Q_INVOKABLE QVariantList timeSyncGroups() const;
    Q_INVOKABLE qint64 createTimeSyncGroup(const QString &name, const QVariantList &entries);
    Q_INVOKABLE bool removeTimeSyncGroup(qint64 id);
    qint64 createTimeSyncGroupByIds(const QString &name, const QVariantList &idPairs);
    // ---- 合班组（同一位老师、同一门课的多个班一起上）----
    Q_INVOKABLE QVariantList combinedGroups() const;
    Q_INVOKABLE qint64 createCombinedGroup(const QString &name, const QVariantList &entries);
    Q_INVOKABLE bool removeCombinedGroup(qint64 id);
    qint64 createCombinedGroupByIds(const QString &name, const QVariantList &idPairs);

    // ---- 批量操作（一次设置/清除，支持整体撤销；对象集合由界面传入）----
    Q_INVOKABLE QVariantList groupMemberIds(qint64 groupId) const; // 组成员 id（按教研组批量勾选用）
    // 批量固定预排：conflictMode 0=跳过已有 1=覆盖。返回 {set, skipped, occupied, affected}
    Q_INVOKABLE QVariantMap batchSetPreassignments(qint64 courseId, const QVariantList &classIds,
                                                   const QVariantList &slots, int conflictMode,
                                                   int half = 0);
    // 批量清除预排：courseId < 0 表示不限课程。返回 {cleared, affected}
    Q_INVOKABLE QVariantMap batchClearPreassignments(qint64 courseId, const QVariantList &classIds,
                                                     const QVariantList &slots);
    // 批量不排时间：forbidden=true 禁排 / false 恢复。返回 {set, cleared, affected}
    Q_INVOKABLE QVariantMap batchSetNoSchedule(int entityType, const QVariantList &ids,
                                               const QVariantList &slots, bool forbidden);
    // 批量改周节数（只改该课程已存在的计划条目）。返回 {updated, missing, affected}
    Q_INVOKABLE QVariantMap batchSetWeeklyPeriods(qint64 courseId, const QVariantList &classIds,
                                                  double weeklyPeriods);
    Q_INVOKABLE bool canUndoBatch() const { return m_hasBatchBackup; }
    Q_INVOKABLE QString lastBatchLabel() const { return m_batchBackup.label; }
    Q_INVOKABLE bool undoLastBatch(); // 撤销上一次批量操作（单级）

    // ---- AI 编排：工具层（工具定义由客户端下发，中继只透传）----
    Q_INVOKABLE QVariantList aiTools() const;          // 工具清单 [{name, description, parameters}]
    Q_INVOKABLE QVariantMap aiRunReadTool(const QString &tool, const QVariantMap &args) const;
    // 干跑：不落盘地算出「影响摘要 + 明细 + 警告」，供待办卡展示
    Q_INVOKABLE QVariantMap aiDescribe(const QString &tool, const QVariantMap &args) const;
    // 按序应用多条 AI 操作（整体一个快照，可被 undoLastBatch 一次性撤销）
    Q_INVOKABLE QVariantMap applyAiOperations(const QVariantList &ops);
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

    // ---- 年级组作息遮罩：把「每周天数 / 每天节数」范围外的槽位批量设为不排 ----
    Q_INVOKABLE QVariantList gradeGroups() const; // 年级组 [{id, name, memberCount}]
    // forbidden=true 禁排越界槽位，false 恢复；activeDays/activePeriods 为组内作息范围
    Q_INVOKABLE void applyGradeMask(qint64 groupId, int activeDays, int activePeriods, bool forbidden);

    // ---- 固定预排 ----
    Q_INVOKABLE QVariantList preAssignments(qint64 classId) const; // [{courseId, courseName, slotIndex}]
    // half：0 整节 / 1 单周 / 2 双周（两门课各占半周 = 共用同一格）
    Q_INVOKABLE void togglePreAssignment(qint64 classId, qint64 courseId, int slot, int half = 0);
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

    // ---- 质量报告（十项指标 + 老师五项）----
    // {indicators:[...], teachers:[...], avgContinuity/avgWindow/avgBalance/avgFairness/avgComposite,
    //  grades:[{id,name,indicators,avg...}], hasSchedule}
    Q_INVOKABLE QVariantMap qualityReport() const;
    Q_INVOKABLE bool exportQualityReportHtml(const QString &pathOrUrl); // 生成自带打印样式的报告并打开
    // 报告 HTML（独立成静态函数便于测试）
    static QString buildQualityReportHtml(const sched::School &school);

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

    // ---- 设置页：版本 / 检查更新 / 日志 ----
    QString appVersion() const { return QStringLiteral(APP_VERSION); }
    QString qtVersion() const { return QString::fromLatin1(qVersion()); }
    int updateStatus() const { return m_updateStatus; } // 0未检查 1检查中 2已最新 3有新版本 4失败
    QString latestVersion() const { return m_latestVersion; }
    QString updateMessage() const { return m_updateMessage; }
    int updateDlState() const { return m_updateDlState; } // 0未下载 1下载中 2已就绪(可安装) 3失败
    bool updateReady() const { return m_updateDlState == 2; }
    int updateDlPercent() const { return m_updateDlPercent; }
    QString updateDlMessage() const { return m_updateDlMessage; }
    // 已弹过系统提醒的版本（自检用：确认「下载就绪 → 弹提醒」这条链路真的走过）
    QString notifiedVersion() const { return m_notifiedVersion; }
    QString logFilePath() const;

    // ---- 内测门禁：首次启动输邀请码，之后靠服务端「设备已通过」跳过 ----
    QString deviceId() const;                    // 设备唯一标识（首次生成后写 QSettings）
    bool betaLocked() const { return m_betaLocked; }
    bool betaBusy() const { return m_betaBusy; }
    QString betaMessage() const { return m_betaMessage; }
    Q_INVOKABLE void checkBetaGate();            // 启动 / 点「重试」时调用
    Q_INVOKABLE void submitInviteCode(const QString &code);
    Q_INVOKABLE void quitApp();
    void initBetaGate();                         // 启动时：先按本地缓存决定是否先锁上，再问服务端
    void setBetaBypass(bool on);                 // 冒烟自检用：跳过门禁弹窗

    // ---- 错误日志反馈 ----
    bool feedbackBusy() const { return m_feedbackBusy; }
    QString feedbackMessage() const { return m_feedbackMessage; }
    bool feedbackOk() const { return m_feedbackOk; }
    Q_INVOKABLE void sendFeedback(const QString &description, bool includeLog);
    Q_INVOKABLE QString recentLogText() const;   // 弹窗里展示「将附带多少日志」

    // 版本号比较与「本次探测是否可信」的判定，抽成静态纯函数便于单测
    static int compareVersions(const QString &a, const QString &b);
    static bool updateProbeConclusive(const QString &tag, const QString &current, bool freshChannel);

    // 系统提醒：更新下载就绪时弹 Windows 原生通知（WinRT toast）。
    // toastScript() 单独暴露便于单测；wait=true 时同步等待（自检用）
    static QString toastScript();
    static bool showWindowsToast(const QString &title, const QString &body, bool wait = false);
    Q_INVOKABLE void sendTestNotification(); // 设置页「发送测试提醒」：验证系统通知是否被允许

    Q_INVOKABLE void checkUpdate();
    void checkUpdateSilently(); // 定时/启动静默检查：不显示「检查中」，发现新版本自动下载
    Q_INVOKABLE void openReleasesPage();
    Q_INVOKABLE void openMirrorDownload(); // 国内镜像加速下载最新安装包
    Q_INVOKABLE void downloadUpdate();     // 镜像链下载安装包到临时目录并校验
    Q_INVOKABLE void installUpdate();      // 启动安装程序（静默覆盖）并退出本程序
    Q_INVOKABLE void openLogDir();
    Q_INVOKABLE bool exportLog(const QString &pathOrUrl);

    // 私有实现：interactive=true 时显示「检查中」状态（手动点击），false 为静默检查
    void checkUpdate(bool interactive);

    // 生成更新用批处理内容：延迟等本程序退出后静默拉起安装包（独立成静态函数便于测试）
    static QString updateBatchContent(const QString &setupPath);

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
    void updateStatusChanged();
    void updateDlStateChanged();
    void betaStateChanged();
    void feedbackStateChanged();

private:
    static QString toLocalPath(const QString &pathOrUrl);
    bool saveToPath(const QString &path);
    void addRecentFile(const QString &path);

    // AI 变更操作的规范化计划（干跑与应用共用同一套解析）
    struct AiOpPlan {
        bool valid = false;
        QString error;
        QString title;
        QString summary;
        QStringList details;
        QStringList warnings;
        int changes = 0;
        qint64 courseId = -1;
        QVariantList classIds;
        QVariantList slots;
        int half = 0;
        int conflictMode = 0;
        int entityType = -1;
        QVariantList entityIds;
        bool forbidden = true;
        double weeklyPeriods = 0;
        qint64 moveClassId = -1;
        int moveFromSlot = -1;
        int moveToSlot = -1;
        int chainedTimes = 0;
        int chainedLength = 2;
        int earlyPeriods = -1;
        QVariantList teacherIds;
        bool rotateTeachers = false;
        int dailyMax = -1;
        qint64 groupId = -1;
        QString groupName;
        QVariantList syncEntries; // [{classId, courseId}]
    };

    // 批量操作的实现（不含快照；包装函数与 AI 多步应用共用）
    // 工具扩展（实现在 AiToolsExtra.cpp）
    bool aiRunReadToolExtra(const QString &tool, const QVariantMap &args, QVariantMap *out) const;
    QVariantList aiToolSchemasExtra() const;
    bool planAiOperationExtra(const QString &tool, const QVariantMap &args, AiOpPlan *out) const;
    bool applyAiOperationExtra(const QString &tool, const AiOpPlan &plan, QVariantMap *out);
    QVariantMap batchSetPlanChainedImpl(qint64 courseId, const QVariantList &classIds, int times,
                                        int length);
    QVariantMap batchSetPlanEarlyPeriodsImpl(qint64 courseId, const QVariantList &classIds,
                                             int earlyPeriods);
    QVariantMap batchSetPlanTeachersImpl(qint64 courseId, const QVariantList &classIds,
                                         const QVariantList &teacherIds, bool rotate);
    QVariantMap batchSetTeacherDailyMaxImpl(const QVariantList &teacherIds, int maxPerDay);

    QVariantMap batchSetPreassignmentsImpl(qint64 courseId, const QVariantList &classIds,
                                           const QVariantList &slots, int conflictMode, int half);
    QVariantMap batchClearPreassignmentsImpl(qint64 courseId, const QVariantList &classIds,
                                             const QVariantList &slots);
    QVariantMap batchSetNoScheduleImpl(int entityType, const QVariantList &ids,
                                       const QVariantList &slots, bool forbidden);
    QVariantMap batchSetWeeklyPeriodsImpl(qint64 courseId, const QVariantList &classIds,
                                          double weeklyPeriods);
    // AI 变更操作的规范化计划（干跑与应用共用同一套解析）
    AiOpPlan planAiOperation(const QString &tool, const QVariantMap &args) const;

    // 批量操作前的单级快照（供「撤销本次」）
    struct BatchBackup {
        QVector<sched::PreAssignment> preAssignments;
        QVector<sched::NoScheduleConstraint> noSchedule;
        QVector<sched::PlanEntry> planEntries;
        sched::Schedule schedule; // AI 调课也要能一并撤销
        QVector<sched::TimeSyncGroup> timeSyncGroups;
        QVector<sched::CombinedGroup> combinedGroups;
        QString label;
    };
    void makeBatchBackup(const QString &label);
    void discardBatchBackup();
    BatchBackup m_batchBackup;
    bool m_hasBatchBackup = false;

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

    // 检查更新
    QNetworkAccessManager *m_nam = nullptr;
    int m_updateStatus = 0;
    QString m_latestVersion;
    QString m_updateMessage;
    QString m_probeTag;    // 探测中见过的不比当前新的最高版本（所有通道都没给出更新版本时用它兜底展示）
    QString m_probeSetup;  // 与之配套的安装包文件名 / 校验值（通道间版本可能不一致）
    QString m_probeSha256;
    qint64 m_probeSize = 0;
    QString m_latestSetup; // 最新版安装包文件名（镜像下载拼 URL 用）
    // 更新检查通道：fresh=true 表示结果实时可信（自建中继直连 GitHub API、GitHub API 自身），
    // 拿到「不比当前新」即可据此定论；第三方反代与 CDN 会缓存 version.json，只能当补充探测。
    struct UpdateChannel {
        QString url;
        bool fresh = false;
    };
    QVector<UpdateChannel> m_updateChannels; // 按国内可达性 + 实时性排序，逐个回退
    int m_updateAttempt = 0;
    bool m_freshChannelAnswered = false; // 本轮是否有实时通道给出了结论
    QString m_notifiedVersion;           // 已弹过系统提醒的版本，避免同一版本反复打扰
    void tryUpdateSource();
    void finishUpdateProbe(); // 通道全部问完的收尾（兜底「已是最新」或报失败）

    // 自动更新：下载 → 校验 → 静默安装
    int m_updateDlState = 0;
    int m_updateDlPercent = 0;
    QString m_updateDlMessage;
    QString m_latestSha256;     // version.json / GitHub API 提供的安装包 sha256（可空）
    qint64 m_latestSize = 0;    // version.json 提供的安装包字节数（无 Content-Length 时兜底进度）
    QString m_downloadedSetup;  // 已下载并校验通过的安装包路径
    QStringList m_downloadUrls; // 安装包下载通道（镜像优先，逐个回退）
    int m_dlAttempt = 0;
    void tryUpdateDownload();

    // 内测门禁
    bool m_betaLocked = false;      // true = 弹邀请码弹窗挡住主界面
    bool m_betaBusy = false;        // 正在问服务端/提交邀请码
    bool m_betaCached = false;      // 本地缓存的「已通过」（网络不通时据此放行）
    bool m_betaBypassed = false;    // 冒烟自检模式：不查门禁
    QString m_betaMessage;          // 弹窗里的提示/错误文案
    void applyBetaStatus(const QJsonObject &obj);
    void setBetaApproved(bool approved);

    // 错误日志反馈
    bool m_feedbackBusy = false;
    bool m_feedbackOk = false;
    QString m_feedbackMessage;
};
