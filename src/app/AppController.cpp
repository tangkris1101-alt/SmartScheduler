#include "AppController.h"

#include "SchedulerEngine.h"

#include <QFile>
#include <QSettings>
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
