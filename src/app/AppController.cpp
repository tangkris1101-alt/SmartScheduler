#include "AppController.h"

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
    Q_EMIT projectChanged();
    Q_EMIT projectDirtyChanged();
    Q_EMIT statsChanged();
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
    m_store.close();
    m_school = sched::School();
    m_currentPath.clear();
    m_hasProject = false;
    m_dirty = false;
    Q_EMIT projectChanged();
    Q_EMIT projectDirtyChanged();
    Q_EMIT statsChanged();
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
