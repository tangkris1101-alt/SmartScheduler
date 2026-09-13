#include "AppController.h"

#include "ExcelIo.h"
#include "SchedulerEngine.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>

namespace {

constexpr int kMaxRecentFiles = 8;

} // namespace

AppController::AppController(QObject *parent)
    : QObject(parent)
{
}

QString AppController::toLocalPath(const QString &pathOrUrl)
{
    if (pathOrUrl.startsWith(QLatin1StringView("file:")))
        return QUrl(pathOrUrl).toLocalFile();
    return pathOrUrl;
}

QStringList AppController::recentFiles() const
{
    return QSettings().value(QStringLiteral("recentFiles")).toStringList();
}

void AppController::addRecentFile(const QString &path)
{
    QStringList files = recentFiles();
    files.removeAll(path);
    files.prepend(path);
    while (files.size() > kMaxRecentFiles)
        files.removeLast();
    QSettings().setValue(QStringLiteral("recentFiles"), files);
    Q_EMIT recentFilesChanged();
}

void AppController::newProject(const QString &name, int daysPerWeek, int periodsPerDay,
                               int morningPeriods, int weeksPerTerm)
{
    m_store.close();
    m_school = sched::School();
    m_school.name = name;
    m_school.timeConfig.daysPerWeek = daysPerWeek;
    m_school.timeConfig.periodsPerDay = periodsPerDay;
    m_school.timeConfig.morningPeriods = morningPeriods;
    m_school.timeConfig.weeksPerTerm = weeksPerTerm;
    m_currentPath.clear();
    m_hasProject = true;
    m_dirty = true; // 新项目尚未落盘
    m_resultPending = false;
    Q_EMIT projectChanged();
    Q_EMIT projectDirtyChanged();
    Q_EMIT statsChanged();
    Q_EMIT scheduleChanged();
    Q_EMIT resultPendingChanged();
}

bool AppController::openProject(const QString &pathOrUrl)
{
    const QString path = toLocalPath(pathOrUrl);
    if (!m_store.open(path)) {
        m_lastError = m_store.lastError();
        return false;
    }
    if (!m_store.load(&m_school)) {
        m_lastError = m_store.lastError();
        return false;
    }
    m_currentPath = path;
    m_hasProject = true;
    m_dirty = false;
    addRecentFile(path);
    // 打印设置随项目载入
    const QVariant saved = m_store.setting(QStringLiteral("print"));
    if (saved.isValid() && !saved.isNull())
        m_printSettings = saved.toMap();
    Q_EMIT printSettingsChanged();
    Q_EMIT projectChanged();
    Q_EMIT projectDirtyChanged();
    Q_EMIT statsChanged();
    Q_EMIT scheduleChanged();
    return true;
}

bool AppController::saveToPath(const QString &path)
{
    // 已有文件走 open 复用 schema；否则 createNew 建库
    const bool okOpen = QFile::exists(path) ? m_store.open(path) : m_store.createNew(path);
    if (!okOpen) {
        m_lastError = m_store.lastError();
        return false;
    }
    if (!m_store.save(m_school)) {
        m_lastError = m_store.lastError();
        return false;
    }
    // 打印设置随项目保存
    m_store.setSetting(QStringLiteral("print"), m_printSettings);
    m_currentPath = path;
    addRecentFile(path);
    if (m_dirty) {
        m_dirty = false;
        Q_EMIT projectDirtyChanged();
    }
    Q_EMIT projectChanged();
    return true;
}

bool AppController::saveProject()
{
    if (!m_hasProject) {
        m_lastError = QStringLiteral("没有打开的项目");
        return false;
    }
    if (m_currentPath.isEmpty()) {
        m_lastError = QStringLiteral("尚未选择保存位置");
        return false;
    }
    return saveToPath(m_currentPath);
}

bool AppController::saveProjectAs(const QString &pathOrUrl)
{
    if (!m_hasProject) {
        m_lastError = QStringLiteral("没有打开的项目");
        return false;
    }
    QString path = toLocalPath(pathOrUrl);
    if (!path.endsWith(QLatin1StringView(".spk"), Qt::CaseInsensitive))
        path += QStringLiteral(".spk");
    return saveToPath(path);
}

void AppController::closeProject()
{
    cancelScheduler();
    m_store.close();
    m_school = sched::School();
    m_currentPath.clear();
    m_hasProject = false;
    m_dirty = false;
    m_resultPending = false;
    Q_EMIT projectChanged();
    Q_EMIT projectDirtyChanged();
    Q_EMIT statsChanged();
    Q_EMIT scheduleChanged();
    Q_EMIT resultPendingChanged();
}

void AppController::markDirty()
{
    if (!m_dirty) {
        m_dirty = true;
        Q_EMIT projectDirtyChanged();
    }
    Q_EMIT statsChanged(); // 名单增删会改变统计数字
}

// ==================== 限制条件 / 预排 / 老师活动 ====================

namespace {

// 找到（或创建）某实体的唯一不排时间约束
template<typename S>
static auto findNoSchedule(S &school, int entityType, qint64 entityId)
{
    const auto type = static_cast<sched::NoScheduleConstraint::EntityType>(entityType);
    for (auto &c : school.noScheduleConstraints) {
        if (c.entityType == type && c.entityId == entityId)
            return &c;
    }
    return static_cast<decltype(&school.noScheduleConstraints.first())>(nullptr);
}

} // namespace

QVariantList AppController::noScheduleSlots(int entityType, qint64 entityId) const
{
    QVariantList out;
    const sched::NoScheduleConstraint *c = findNoSchedule(m_school, entityType, entityId);
    if (!c)
        return out;
    QList<int> sorted = c->slots.values();
    std::sort(sorted.begin(), sorted.end());
    for (int slot : sorted)
        out.append(slot);
    return out;
}

void AppController::setNoScheduleSlot(int entityType, qint64 entityId, int slot, bool forbidden)
{
    sched::NoScheduleConstraint *c = findNoSchedule(m_school, entityType, entityId);
    if (forbidden) {
        if (!c) {
            sched::NoScheduleConstraint nc;
            nc.entityType = static_cast<sched::NoScheduleConstraint::EntityType>(entityType);
            nc.entityId = entityId;
            m_school.addNoScheduleConstraint(nc);
            c = &m_school.noScheduleConstraints.last(); // add 追加在末尾
        }
        if (c && !c->slots.contains(slot)) {
            c->slots.insert(slot);
        } else {
            return;
        }
    } else {
        if (!c || !c->slots.contains(slot))
            return;
        c->slots.remove(slot);
        if (c->slots.isEmpty()) // 空约束删除，保持数据整洁
            m_school.removeNoScheduleConstraint(c->id);
    }
    markDirty();
    Q_EMIT constraintsChanged();
}

void AppController::clearNoSchedule(int entityType, qint64 entityId)
{
    sched::NoScheduleConstraint *c = findNoSchedule(m_school, entityType, entityId);
    if (!c)
        return;
    m_school.removeNoScheduleConstraint(c->id);
    markDirty();
    Q_EMIT constraintsChanged();
}

QVariantList AppController::preAssignments(qint64 classId) const
{
    QVariantList out;
    for (const auto &p : m_school.preAssignments) {
        if (p.classId != classId)
            continue;
        const sched::Course *course = m_school.findCourse(p.courseId);
        out.append(QVariantMap{{QStringLiteral("courseId"), p.courseId},
                               {QStringLiteral("courseName"),
                                course ? course->name : QStringLiteral("?")},
                               {QStringLiteral("slotIndex"), p.slotIndex}});
    }
    return out;
}

void AppController::togglePreAssignment(qint64 classId, qint64 courseId, int slot)
{
    // 同班同 slot 只允许一条预排：已存在则取消，否则写入
    for (const auto &p : m_school.preAssignments) {
        if (p.classId == classId && p.slotIndex == slot) {
            m_school.removePreAssignment(p.id);
            markDirty();
            Q_EMIT constraintsChanged();
            return;
        }
    }
    sched::PreAssignment p;
    p.classId = classId;
    p.courseId = courseId;
    p.slotIndex = slot;
    m_school.addPreAssignment(p);
    markDirty();
    Q_EMIT constraintsChanged();
}

QVariantList AppController::planCourses(qint64 classId) const
{
    QVariantList out;
    QVector<qint64> seen;
    for (const auto &e : m_school.planEntries) {
        if (e.classId != classId || seen.contains(e.courseId))
            continue;
        seen.append(e.courseId);
        const sched::Course *course = m_school.findCourse(e.courseId);
        out.append(QVariantMap{{QStringLiteral("id"), e.courseId},
                               {QStringLiteral("name"),
                                course ? course->name : QStringLiteral("?")}});
    }
    return out;
}

QVariantList AppController::courseTeachers(qint64 classId, qint64 courseId) const
{
    QVariantList out;
    for (const auto &e : m_school.planEntries) {
        if (e.classId == classId && e.courseId == courseId) {
            for (qint64 tid : e.teacherIds)
                out.append(tid);
            break;
        }
    }
    return out;
}

QVariantList AppController::teacherActivities(qint64 teacherId) const
{
    QVariantList out;
    for (const auto &a : m_school.teacherActivities) {
        if (a.teacherId != teacherId)
            continue;
        const sched::Course *course = m_school.findCourse(a.activityCourseId);
        out.append(QVariantMap{{QStringLiteral("slotIndex"), a.slotIndex},
                               {QStringLiteral("courseId"), a.activityCourseId},
                               {QStringLiteral("courseName"),
                                course ? course->name : QStringLiteral("?")}});
    }
    return out;
}

void AppController::toggleTeacherActivity(qint64 teacherId, int slot, qint64 courseId)
{
    for (const auto &a : m_school.teacherActivities) {
        if (a.teacherId == teacherId && a.slotIndex == slot) {
            m_school.removeTeacherActivity(a.id);
            markDirty();
            Q_EMIT constraintsChanged();
            return;
        }
    }
    sched::TeacherActivity a;
    a.teacherId = teacherId;
    a.slotIndex = slot;
    a.activityCourseId = courseId;
    m_school.addTeacherActivity(a);
    markDirty();
    Q_EMIT constraintsChanged();
}

// ==================== 自动排课 ====================

namespace {

QVariantMap reportToMap(const sched::QualityReport &r)
{
    QVariantMap breakdown;
    for (auto it = r.softBreakdown.begin(); it != r.softBreakdown.end(); ++it)
        breakdown.insert(it.key(), it.value());
    return {{QStringLiteral("totalSessions"), r.totalSessions},
            {QStringLiteral("placedSessions"), r.placedSessions},
            {QStringLiteral("hardViolations"), r.hardViolations},
            {QStringLiteral("softScore"), r.softScore},
            {QStringLiteral("breakdown"), breakdown},
            {QStringLiteral("unplaced"), r.unplaced}};
}

} // namespace

void AppController::runScheduler(const QVariantMap &options)
{
    if (m_schedulerRunning)
        return;
    sched::EngineOptions opt;
    opt.runs = options.value(QStringLiteral("runs"), 4).toInt();
    opt.maxIterationsPerRun = options.value(QStringLiteral("maxIterations"), 20000).toInt();
    opt.seed = options.value(QStringLiteral("seed"), 0).toUInt();
    opt.randomSeed = options.value(QStringLiteral("randomSeed"), true).toBool();
    opt.placeAll = options.value(QStringLiteral("placeAll"), true).toBool();

    if (m_engine)
        m_engine->deleteLater();
    m_engine = new sched::SchedulerEngine(m_school, opt, this);
    connect(m_engine, &sched::SchedulerEngine::progress, this,
            [this](int percent, int currentRun, int totalRuns, const sched::QualityReport &current) {
                m_schedulerProgress = percent;
                m_schedulerRunInfo = QStringLiteral("第 %1 / %2 轮").arg(currentRun).arg(totalRuns);
                m_schedulerReport = reportToMap(current);
                Q_EMIT schedulerProgressed();
            });
    connect(m_engine, &sched::SchedulerEngine::finished, this,
            [this](const sched::Schedule &best, const sched::QualityReport &report) {
                m_schedulerRunning = false;
                m_pendingResult = best;
                m_pendingReport = report;
                m_resultPending = true;
                m_schedulerReport = reportToMap(report);
                Q_EMIT schedulerRunningChanged();
                Q_EMIT resultPendingChanged();
                Q_EMIT schedulerFinished();
                if (m_sampleAutoApply) {
                    m_sampleAutoApply = false;
                    applyResult(); // 示例学校：直接应用，用户立即看到课表
                }
            });

    m_schedulerRunning = true;
    m_schedulerProgress = 0;
    m_schedulerRunInfo = {};
    m_resultPending = false;
    Q_EMIT schedulerRunningChanged();
    Q_EMIT resultPendingChanged();
    Q_EMIT schedulerProgressed();
    m_engine->start();
}

void AppController::cancelScheduler()
{
    if (m_engine && m_schedulerRunning)
        m_engine->cancel();
}

void AppController::applyResult()
{
    if (!m_resultPending)
        return;
    m_school.schedule = m_pendingResult;
    m_resultPending = false;
    markDirty();
    Q_EMIT resultPendingChanged();
    Q_EMIT scheduleChanged();
}

void AppController::discardResult()
{
    if (!m_resultPending)
        return;
    m_resultPending = false;
    m_pendingResult = sched::Schedule();
    Q_EMIT resultPendingChanged();
}

QVariantList AppController::precheck() const
{
    QVariantList out;
    auto add = [&out](const QString &level, const QString &text) {
        out.append(QVariantMap{{QStringLiteral("level"), level}, {QStringLiteral("text"), text}});
    };

    const int capacity = m_school.timeConfig.slotCount();
    if (m_school.classes.isEmpty())
        add(QStringLiteral("err"), QStringLiteral("还没有班级：请先在「名单管理」添加班级"));
    if (m_school.planEntries.isEmpty())
        add(QStringLiteral("err"), QStringLiteral("还没有教学计划：请先在「教学计划」页录入"));

    // 每班计划总节数 vs 每周节位容量
    for (const auto &c : m_school.classes) {
        double total = 0;
        for (const auto &e : m_school.planEntries) {
            if (e.classId == c.id)
                total += e.weeklyPeriods;
        }
        if (total > capacity)
            add(QStringLiteral("err"),
                QStringLiteral("%1 计划总节数 %2 超过每周容量 %3 节")
                    .arg(c.name)
                    .arg(total)
                    .arg(capacity));
    }

    int noTeacher = 0;
    for (const auto &e : m_school.planEntries) {
        if (e.teacherIds.isEmpty())
            noTeacher++;
    }
    if (noTeacher > 0)
        add(QStringLiteral("warn"),
            QStringLiteral("%1 条教学计划未设任课老师（将不参与老师冲突检查）").arg(noTeacher));

    int unlinkedCombined = 0;
    for (const auto &e : m_school.planEntries) {
        if (!e.isCombined)
            continue;
        bool linked = false;
        for (const auto &g : m_school.combinedGroups) {
            if (g.entries.contains({e.classId, e.courseId})) {
                linked = true;
                break;
            }
        }
        if (!linked)
            unlinkedCombined++;
    }
    if (unlinkedCombined > 0)
        add(QStringLiteral("warn"),
            QStringLiteral("%1 条合班课未加入合班组（将按普通课处理）").arg(unlinkedCombined));

    // 不排时间过多的实体
    for (const auto &c : m_school.noScheduleConstraints) {
        if (c.slots.size() > capacity / 2) {
            QString who;
            using ET = sched::NoScheduleConstraint::EntityType;
            switch (c.entityType) {
            case ET::Class:
                if (const auto *e = m_school.findClass(c.entityId)) who = e->name;
                break;
            case ET::Course:
                if (const auto *e = m_school.findCourse(c.entityId)) who = e->name;
                break;
            case ET::Teacher:
                if (const auto *e = m_school.findTeacher(c.entityId)) who = e->name;
                break;
            case ET::Classroom:
                if (const auto *e = m_school.findClassroom(c.entityId)) who = e->name;
                break;
            }
            add(QStringLiteral("warn"),
                QStringLiteral("「%1」的不排时间超过一半节位（%2 格），可能排不出课")
                    .arg(who)
                    .arg(c.slots.size()));
        }
    }

    bool hasErr = false;
    for (const auto &item : out) {
        if (item.toMap().value(QStringLiteral("level")).toString() == QLatin1StringView("err"))
            hasErr = true;
    }
    if (!hasErr && out.isEmpty())
        add(QStringLiteral("ok"), QStringLiteral("数据检查通过，可以开始排课"));
    else if (!hasErr)
        add(QStringLiteral("ok"), QStringLiteral("有警告但不强制阻止，可以继续排课"));
    return out;
}

// ==================== 课表视图数据 ====================

QVariantList AppController::classRow(qint64 classId) const
{
    QVariantList row;
    const int sc = m_school.timeConfig.slotCount();
    for (int slot = 0; slot < sc; ++slot) {
        QVariantMap cell;
        for (const auto &a : m_school.schedule.assignments) {
            if (a.classId != classId || a.slotIndex != slot)
                continue;
            const sched::Course *co = m_school.findCourse(a.courseId);
            QStringList teacherNames;
            for (const auto &e : m_school.planEntries) {
                if (e.classId == classId && e.courseId == a.courseId) {
                    for (qint64 tid : e.teacherIds) {
                        if (const sched::Teacher *t = m_school.findTeacher(tid))
                            teacherNames.append(t->name);
                    }
                    break;
                }
            }
            bool pre = false;
            for (const auto &p : m_school.preAssignments) {
                if (p.classId == classId && p.slotIndex == slot) {
                    pre = true;
                    break;
                }
            }
            cell = {{QStringLiteral("text"), co ? (co->shortName.isEmpty() ? co->name : co->shortName)
                                               : QStringLiteral("?")},
                    {QStringLiteral("sub"), teacherNames.join(QStringLiteral("、"))},
                    {QStringLiteral("courseId"), a.courseId},
                    {QStringLiteral("half"), a.half},
                    {QStringLiteral("major"), co && co->isMajor},
                    {QStringLiteral("pre"), pre}};
            break; // 整节优先；单双周同格只取第一条
        }
        // 单双周同格第二条
        QString secondText;
        for (const auto &a : m_school.schedule.assignments) {
            if (a.classId == classId && a.slotIndex == slot && a.half != 0) {
                const sched::Course *co = m_school.findCourse(a.courseId);
                const QString t = co ? co->name : QStringLiteral("?");
                if (cell.isEmpty()) {
                    cell = {{QStringLiteral("text"), t},
                            {QStringLiteral("sub"), QString()},
                            {QStringLiteral("courseId"), a.courseId},
                            {QStringLiteral("half"), a.half},
                            {QStringLiteral("major"), co && co->isMajor},
                            {QStringLiteral("pre"), false}};
                } else if (cell.value(QStringLiteral("half")).toInt() != 0
                           && cell.value(QStringLiteral("courseId")).toLongLong() != a.courseId) {
                    secondText = t + (a.half == 1 ? QStringLiteral("单") : QStringLiteral("双"));
                }
            }
        }
        if (!secondText.isEmpty() && !cell.isEmpty())
            cell.insert(QStringLiteral("sub2"), secondText);
        row.append(cell);
    }
    return row;
}

QVariantList AppController::teacherRow(qint64 teacherId) const
{
    QVariantList row;
    const int sc = m_school.timeConfig.slotCount();
    for (int slot = 0; slot < sc; ++slot) {
        QVariantList items;
        for (const auto &a : m_school.schedule.assignments) {
            if (a.slotIndex != slot)
                continue;
            for (const auto &e : m_school.planEntries) {
                if (e.classId != a.classId || e.courseId != a.courseId
                    || !e.teacherIds.contains(teacherId))
                    continue;
                const sched::ClassInfo *c = m_school.findClass(a.classId);
                const sched::Course *co = m_school.findCourse(a.courseId);
                items.append(QVariantMap{
                    {QStringLiteral("text"), c ? c->name : QStringLiteral("?")},
                    {QStringLiteral("sub"), co ? co->name : QString()},
                    {QStringLiteral("classId"), a.classId},
                    {QStringLiteral("courseId"), a.courseId},
                    {QStringLiteral("half"), a.half}});
                break;
            }
        }
        // 老师活动时间
        for (const auto &act : m_school.teacherActivities) {
            if (act.teacherId == teacherId && act.slotIndex == slot) {
                const sched::Course *co = m_school.findCourse(act.activityCourseId);
                items.append(QVariantMap{{QStringLiteral("text"), QStringLiteral("活动")},
                                         {QStringLiteral("sub"),
                                          co ? co->name : QString()},
                                         {QStringLiteral("classId"), -1},
                                         {QStringLiteral("courseId"), act.activityCourseId},
                                         {QStringLiteral("half"), 0},
                                         {QStringLiteral("activity"), true}});
            }
        }
        row.append(QVariant(items));
    }
    return row;
}

QVariantList AppController::roomRow(qint64 roomId) const
{
    QVariantList row;
    const int sc = m_school.timeConfig.slotCount();
    for (int slot = 0; slot < sc; ++slot) {
        QVariantList items;
        for (const auto &a : m_school.schedule.assignments) {
            if (a.slotIndex != slot)
                continue;
            for (const auto &e : m_school.planEntries) {
                if (e.classId != a.classId || e.courseId != a.courseId
                    || e.classroomId != roomId)
                    continue;
                const sched::ClassInfo *c = m_school.findClass(a.classId);
                const sched::Course *co = m_school.findCourse(a.courseId);
                items.append(QVariantMap{
                    {QStringLiteral("text"), c ? c->name : QStringLiteral("?")},
                    {QStringLiteral("sub"), co ? co->name : QString()},
                    {QStringLiteral("classId"), a.classId},
                    {QStringLiteral("courseId"), a.courseId}});
                break;
            }
        }
        row.append(QVariant(items));
    }
    return row;
}

QVariantList AppController::masterGrid() const
{
    QVariantList rows;
    const int sc = m_school.timeConfig.slotCount();
    for (const auto &c : m_school.classes) {
        QVariantList cells(sc);
        for (int slot = 0; slot < sc; ++slot) {
            QVariantMap cell;
            for (const auto &a : m_school.schedule.assignments) {
                if (a.classId != c.id || a.slotIndex != slot)
                    continue;
                const sched::Course *co = m_school.findCourse(a.courseId);
                cell = {{QStringLiteral("text"),
                         co ? (co->shortName.isEmpty() ? co->name : co->shortName)
                            : QStringLiteral("?")},
                        {QStringLiteral("courseId"), a.courseId},
                        {QStringLiteral("major"), co && co->isMajor},
                        {QStringLiteral("half"), a.half}};
                break;
            }
            cells[slot] = cell;
        }
        rows.append(QVariantMap{{QStringLiteral("classId"), c.id},
                                {QStringLiteral("className"), c.name},
                                {QStringLiteral("cells"), cells}});
    }
    return rows;
}

// ==================== Excel 批量导入 ====================

bool AppController::writeImportTemplate(const QString &pathOrUrl)
{
    QString path = toLocalPath(pathOrUrl);
    if (!path.endsWith(QLatin1StringView(".xlsx"), Qt::CaseInsensitive))
        path += QStringLiteral(".xlsx");
    QString err;
    if (!sched::ExcelImporter::writeImportTemplate(path, &err)) {
        m_lastError = err;
        return false;
    }
    return true;
}

QVariantMap AppController::excelImportPreview(const QString &pathOrUrl, bool overwrite)
{
    m_importStaging = m_school; // 在副本上预演，不污染当前数据
    const sched::ImportReport rep =
        sched::ExcelImporter::importFile(toLocalPath(pathOrUrl), &m_importStaging, overwrite);
    m_importReport = {{QStringLiteral("created"), rep.created},
                      {QStringLiteral("updated"), rep.updated},
                      {QStringLiteral("skipped"), rep.skipped},
                      {QStringLiteral("errors"), rep.errors},
                      {QStringLiteral("fatal"), rep.fatal}};
    m_importPending = !rep.fatal;
    Q_EMIT importPendingChanged();
    return m_importReport;
}

bool AppController::excelImportConfirm()
{
    if (!m_importPending)
        return false;
    m_school = m_importStaging;
    m_importStaging = sched::School();
    m_importPending = false;
    markDirty();
    Q_EMIT importPendingChanged();
    Q_EMIT projectChanged(); // 名单/计划大面积变更，模型全量重置
    Q_EMIT statsChanged();
    return true;
}

void AppController::excelImportCancel()
{
    m_importStaging = sched::School();
    m_importPending = false;
    Q_EMIT importPendingChanged();
}

// ==================== 打印设置 ====================

void AppController::setPrintSettings(const QVariantMap &settings)
{
    if (m_printSettings == settings)
        return;
    m_printSettings = settings;
    markDirty(); // 设置随项目保存，也算未保存改动
    Q_EMIT printSettingsChanged();
}

// ==================== 示例学校与新手引导 ====================

void AppController::createSampleSchool(bool autoSchedule)
{
    cancelScheduler();
    m_store.close();
    sched::School s;
    s.name = QStringLiteral("示例实验中学");
    s.timeConfig.daysPerWeek = 5;
    s.timeConfig.periodsPerDay = 7;
    s.timeConfig.morningPeriods = 4;
    s.timeConfig.weeksPerTerm = 20;

    // 年级组 + 6 个班
    sched::Group grade;
    grade.type = sched::Group::Type::Grade;
    grade.name = QStringLiteral("高一年级");
    const qint64 gradeId = s.addGroup(grade);
    QVector<qint64> classIds;
    for (int i = 1; i <= 6; ++i) {
        sched::ClassInfo c;
        c.name = QStringLiteral("高一%1班").arg(i);
        c.gradeId = gradeId;
        classIds.append(s.addClass(c));
        s.findGroup(gradeId)->memberIds.append(classIds.last());
    }

    // 课程（11 门，含主课/理科/平均分布/上下午）
    auto addCourse = [&](const QString &name, bool major, bool science,
                         sched::AmPmPolicy ampm = sched::AmPmPolicy::Auto, bool spread = false) {
        sched::Course c;
        c.name = name;
        c.isMajor = major;
        c.isScience = science;
        c.amPmPolicy = ampm;
        c.evenSpread = spread;
        return s.addCourse(c);
    };
    const qint64 yuwen = addCourse(QStringLiteral("语文"), true, false, sched::AmPmPolicy::Morning);
    const qint64 shuxue = addCourse(QStringLiteral("数学"), true, true, sched::AmPmPolicy::Morning);
    const qint64 yingyu = addCourse(QStringLiteral("英语"), true, false, sched::AmPmPolicy::Morning);
    const qint64 wuli = addCourse(QStringLiteral("物理"), true, true);
    const qint64 huaxue = addCourse(QStringLiteral("化学"), false, true);
    const qint64 shengwu = addCourse(QStringLiteral("生物"), false, true);
    const qint64 lishi = addCourse(QStringLiteral("历史"), false, false);
    const qint64 dili = addCourse(QStringLiteral("地理"), false, false);
    const qint64 zhengzhi = addCourse(QStringLiteral("政治"), false, false);
    const qint64 tiyu = addCourse(QStringLiteral("体育"), false, false, sched::AmPmPolicy::Afternoon);
    const qint64 xinxi = addCourse(QStringLiteral("信息"), false, true, sched::AmPmPolicy::Auto, true);
    const qint64 shengya = addCourse(QStringLiteral("生涯规划"), false, false);

    // 老师（14 位）
    auto addTeacher = [&](const QString &name, int maxPerDay = -1) {
        sched::Teacher t;
        t.name = name;
        t.maxPeriodsPerDay = maxPerDay;
        return s.addTeacher(t);
    };
    const qint64 tWangFang = addTeacher(QStringLiteral("王芳"));
    const qint64 tLiJing = addTeacher(QStringLiteral("李静"));
    const qint64 tZhangMin = addTeacher(QStringLiteral("张敏"));
    const qint64 tChenQiang = addTeacher(QStringLiteral("陈强"));
    const qint64 tLiuYang = addTeacher(QStringLiteral("刘洋"));
    const qint64 tYangLiu = addTeacher(QStringLiteral("杨柳"));
    const qint64 tZhaoLei = addTeacher(QStringLiteral("赵磊"));
    const qint64 tZhouJie = addTeacher(QStringLiteral("周杰"));
    const qint64 tWuXia = addTeacher(QStringLiteral("吴霞"));
    const qint64 tZhengShuang = addTeacher(QStringLiteral("郑爽"));
    const qint64 tFengGuo = addTeacher(QStringLiteral("冯国"));
    const qint64 tChuHong = addTeacher(QStringLiteral("褚红"));
    const qint64 tWeiMin = addTeacher(QStringLiteral("卫民"));
    const qint64 tHeBing = addTeacher(QStringLiteral("何冰"), 4);
    const qint64 tZhengTao = addTeacher(QStringLiteral("郑涛"));

    // 公共教室
    auto addRoom = [&](const QString &name, int cap) {
        sched::Classroom r;
        r.name = name;
        r.capacity = cap;
        return s.addClassroom(r);
    };
    const qint64 playground = addRoom(QStringLiteral("操场"), 6);
    const qint64 computerRoom = addRoom(QStringLiteral("计算机房"), 3);
    const qint64 physicsLab = addRoom(QStringLiteral("物理实验室"), 2);

    // 教研组
    sched::Group ywGroup;
    ywGroup.type = sched::Group::Type::CourseGroup;
    ywGroup.name = QStringLiteral("语文教研组");
    const qint64 ywGid = s.addGroup(ywGroup);
    s.findGroup(ywGid)->memberIds.append(yuwen);

    // 教学计划：每班一套
    auto plan = [&](qint64 cls, qint64 course, QVector<qint64> teachers, double periods,
                    int chained = 0, int chainLen = 2, qint64 room = -1, bool combined = false,
                    int pg = -1) {
        sched::PlanEntry e;
        e.classId = cls;
        e.courseId = course;
        e.teacherIds = teachers;
        e.weeklyPeriods = periods;
        e.chainedCount = chained;
        e.chainLength = chainLen;
        e.classroomId = room;
        e.isCombined = combined;
        e.progressGroup = pg;
        s.addPlanEntry(e);
    };
    const QVector<qint64> ywTeachers = {tWangFang, tWangFang, tLiJing, tLiJing, tZhangMin, tZhangMin};
    const QVector<qint64> sxTeachers = {tChenQiang, tChenQiang, tChenQiang, tLiuYang, tLiuYang, tLiuYang};
    const QVector<qint64> yyTeachers = {tYangLiu, tYangLiu, tYangLiu, tZhaoLei, tZhaoLei, tZhaoLei};
    for (int i = 0; i < 6; ++i) {
        const qint64 cls = classIds[i];
        plan(cls, yuwen, {ywTeachers[i]}, 5, 1, 2, -1, false, 1); // 语文进度组1
        plan(cls, shuxue, {sxTeachers[i]}, 5, 1, 2, -1, false, 1);
        plan(cls, yingyu, {yyTeachers[i]}, 4);
        plan(cls, wuli, {tZhouJie}, 3, 0, 2, physicsLab);
        plan(cls, huaxue, {tWuXia}, 3);
        plan(cls, shengwu, {tZhengShuang}, 2);
        plan(cls, lishi, {tFengGuo}, 2);
        plan(cls, dili, {tChuHong}, 2);
        plan(cls, zhengzhi, {tWeiMin}, 2);
        plan(cls, tiyu, {tHeBing}, 2, 0, 2, playground, true); // 体育合班
        plan(cls, xinxi, {tZhengTao}, 2, 0, 2, computerRoom);
        plan(cls, shengya, {tZhengTao}, 0.5); // 单双周
    }

    // 合班组：体育两两合班
    for (int i = 0; i < 6; i += 2) {
        sched::CombinedGroup cg;
        cg.name = QStringLiteral("体育合班 %1-%2").arg(i + 1).arg(i + 2);
        cg.entries = {{classIds[i], tiyu}, {classIds[i + 1], tiyu}};
        s.addCombinedGroup(cg);
    }

    // 不排时间：体育老师周五下午；信息课周一上午（机房维护）
    sched::NoScheduleConstraint ns1;
    ns1.entityType = sched::NoScheduleConstraint::EntityType::Teacher;
    ns1.entityId = tHeBing;
    ns1.slots = {4 * 7 + 5, 4 * 7 + 6}; // 周五第 6-7 节
    s.addNoScheduleConstraint(ns1);
    sched::NoScheduleConstraint ns2;
    ns2.entityType = sched::NoScheduleConstraint::EntityType::Course;
    ns2.entityId = xinxi;
    ns2.slots = {0, 1}; // 周一第 1-2 节
    s.addNoScheduleConstraint(ns2);

    // 固定预排：高一1班体育 周五第1节
    sched::PreAssignment pre;
    pre.classId = classIds[0];
    pre.courseId = tiyu;
    pre.slotIndex = 4 * 7; // 周五第1节
    s.addPreAssignment(pre);

    m_school = s;
    m_currentPath.clear();
    m_hasProject = true;
    m_dirty = true;
    m_resultPending = false;
    Q_EMIT projectChanged();
    Q_EMIT projectDirtyChanged();
    Q_EMIT statsChanged();
    Q_EMIT scheduleChanged();
    Q_EMIT resultPendingChanged();

    if (autoSchedule) {
        m_sampleAutoApply = true;
        runScheduler({{QStringLiteral("runs"), 2},
                      {QStringLiteral("maxIterations"), 20000},
                      {QStringLiteral("randomSeed"), true},
                      {QStringLiteral("placeAll"), true}});
    }
}

bool AppController::onboardingShown() const
{
    return QSettings().value(QStringLiteral("ui/onboardingShown"), false).toBool();
}

void AppController::setOnboardingShown(bool shown)
{
    QSettings().setValue(QStringLiteral("ui/onboardingShown"), shown);
}

// ---- 设置页：版本 / 检查更新 / 日志 ----

QString AppController::logFilePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/logs/smartscheduler.log");
}

int AppController::compareVersions(const QString &a, const QString &b)
{
    auto parts = [](QString v) {
        if (v.startsWith(QLatin1Char('v')) || v.startsWith(QLatin1Char('V')))
            v = v.mid(1);
        const QStringList segs = v.split(QLatin1Char('.'));
        QList<int> nums;
        nums.reserve(segs.size());
        for (const QString &s : segs)
            nums.append(s.toInt());
        return nums;
    };
    const QList<int> pa = parts(a);
    const QList<int> pb = parts(b);
    for (int i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        const int va = i < pa.size() ? pa[i] : 0;
        const int vb = i < pb.size() ? pb[i] : 0;
        if (va != vb)
            return va < vb ? -1 : 1;
    }
    return 0;
}

void AppController::checkUpdate()
{
    if (m_updateStatus == 1) // 防止重复点击
        return;
    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);
    m_updateStatus = 1;
    m_updateMessage.clear();
    Q_EMIT updateStatusChanged();

    QNetworkRequest req(
        QUrl(QStringLiteral("https://api.github.com/repos/tangkris1101-alt/SmartScheduler/releases/latest")));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "SmartScheduler-UpdateCheck");
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_updateStatus = 4;
            m_updateMessage = tr("自动检查失败（%1）。私有仓库无法匿名查询，请打开发布页查看。")
                                  .arg(reply->errorString());
            Q_EMIT updateStatusChanged();
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        const QString tag = doc.object().value(QStringLiteral("tag_name")).toString();
        if (tag.isEmpty()) {
            m_updateStatus = 4;
            m_updateMessage = tr("未能解析发布信息。");
        } else if (compareVersions(tag, appVersion()) > 0) {
            m_updateStatus = 3;
            m_latestVersion = tag;
            m_updateMessage = tr("发现新版本 %1，建议更新。").arg(tag);
        } else {
            m_updateStatus = 2;
            m_latestVersion = tag;
            m_updateMessage = tr("当前已是最新版本。");
        }
        Q_EMIT updateStatusChanged();
    });
}

void AppController::openReleasesPage()
{
    QDesktopServices::openUrl(
        QUrl(QStringLiteral("https://github.com/tangkris1101-alt/SmartScheduler/releases")));
}

void AppController::openLogDir()
{
    const QString dir = QFileInfo(logFilePath()).absolutePath();
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

bool AppController::exportLog(const QString &pathOrUrl)
{
    const QString dest = toLocalPath(pathOrUrl);
    if (dest.isEmpty())
        return false;
    QFile::remove(dest);
    return QFile::copy(logFilePath(), dest);
}
