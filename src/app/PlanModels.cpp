#include "PlanModels.h"

#include "AppController.h"

// ==================== PlanListModel ====================

PlanListModel::PlanListModel(AppController *app, QObject *parent)
    : QAbstractListModel(parent)
    , m_app(app)
{
    connect(m_app, &AppController::projectChanged, this, [this] { fullReset(); });
}

void PlanListModel::fullReset()
{
    beginResetModel();
    m_view.clear();
    const auto &entries = m_app->school()->planEntries;
    for (int i = 0; i < entries.size(); ++i) {
        if (entries[i].classId == m_classId)
            m_view.append(i);
    }
    endResetModel();
    Q_EMIT countChanged();
}

void PlanListModel::setCurrentClassId(qint64 id)
{
    if (m_classId == id)
        return;
    m_classId = id;
    fullReset();
    Q_EMIT currentClassIdChanged();
}

int PlanListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_view.size();
}

sched::PlanEntry *PlanListModel::entryAtRow(int row) const
{
    if (row < 0 || row >= m_view.size())
        return nullptr;
    return const_cast<sched::PlanEntry *>(&m_app->school()->planEntries[m_view[row]]);
}

const sched::PlanEntry *PlanListModel::entryAt(int sourceRow) const
{
    return &m_app->school()->planEntries[sourceRow];
}

QVariant PlanListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_view.size())
        return {};
    const sched::PlanEntry *e = entryAt(m_view[index.row()]);
    const sched::School *s = m_app->school();
    switch (role) {
    case IdRole: return e->id;
    case ClassIdRole: return e->classId;
    case CourseIdRole: return e->courseId;
    case CourseNameRole: {
        const sched::Course *c = s->findCourse(e->courseId);
        return c ? c->name : QStringLiteral("（未选课程）");
    }
    case TeacherIdsRole: return QVariant::fromValue(e->teacherIds);
    case TeacherNamesRole: {
        QStringList names;
        for (qint64 tid : e->teacherIds) {
            if (const sched::Teacher *t = s->findTeacher(tid))
                names.append(t->name);
        }
        return names.join(QStringLiteral("、"));
    }
    case WeeklyPeriodsRole: return e->weeklyPeriods;
    case ChainedCountRole: return e->chainedCount;
    case ChainLengthRole: return e->chainLength;
    case MorningPeriodsRole: return e->morningPeriods;
    case IsCombinedRole: return e->isCombined;
    case ClassroomIdRole: return e->classroomId;
    case ClassroomNameRole: {
        const sched::Classroom *r = s->findClassroom(e->classroomId);
        return r ? r->name : QStringLiteral("本班教室");
    }
    case StartWeekRole: return e->startWeek;
    case EndWeekRole: return e->endWeek;
    case SyncCourseIdRole: return e->syncCourseId;
    case SyncCourseNameRole: {
        const sched::Course *c = s->findCourse(e->syncCourseId);
        return c ? c->name : QStringLiteral("无");
    }
    case ProgressGroupRole: return e->progressGroup;
    case DuplicateRole: {
        // 同班同课重复 → 红色警告
        for (const auto &other : s->planEntries) {
            if (other.id != e->id && other.classId == e->classId && other.courseId == e->courseId
                && e->courseId >= 0)
                return true;
        }
        return e->courseId < 0; // 未选课程也标红
    }
    case TeacherWarnRole: {
        // 课程设定了教研组且有任课老师不在组内 → 黄色提示
        const sched::Course *c = s->findCourse(e->courseId);
        if (!c || c->groupId < 0 || e->teacherIds.isEmpty())
            return false;
        const sched::Group *g = s->findGroup(c->groupId);
        if (!g)
            return false;
        for (qint64 tid : e->teacherIds) {
            if (!g->memberIds.contains(tid))
                return true;
        }
        return false;
    }
    default: return {};
    }
}

QHash<int, QByteArray> PlanListModel::roleNames() const
{
    return {{IdRole, "id"},
            {ClassIdRole, "classId"},
            {CourseIdRole, "courseId"},
            {CourseNameRole, "courseName"},
            {TeacherIdsRole, "teacherIds"},
            {TeacherNamesRole, "teacherNames"},
            {WeeklyPeriodsRole, "weeklyPeriods"},
            {ChainedCountRole, "chainedCount"},
            {ChainLengthRole, "chainLength"},
            {MorningPeriodsRole, "morningPeriods"},
            {IsCombinedRole, "isCombined"},
            {ClassroomIdRole, "classroomId"},
            {ClassroomNameRole, "classroomName"},
            {StartWeekRole, "startWeek"},
            {EndWeekRole, "endWeek"},
            {SyncCourseIdRole, "syncCourseId"},
            {SyncCourseNameRole, "syncCourseName"},
            {ProgressGroupRole, "progressGroup"},
            {DuplicateRole, "duplicate"},
            {TeacherWarnRole, "teacherWarn"}};
}

int PlanListModel::roleForName(const QString &name) const
{
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.begin(); it != names.end(); ++it) {
        if (it.value() == name.toUtf8())
            return it.key();
    }
    return -1;
}

QVariant PlanListModel::get(int row, const QString &roleName) const
{
    const int role = roleForName(roleName);
    return role < 0 ? QVariant() : data(index(row), role);
}

bool PlanListModel::set(int row, const QString &roleName, const QVariant &value)
{
    sched::PlanEntry *e = entryAtRow(row);
    if (!e)
        return false;
    const int role = roleForName(roleName);
    switch (role) {
    case CourseIdRole: e->courseId = value.toLongLong(); break;
    case WeeklyPeriodsRole: e->weeklyPeriods = value.toDouble(); break;
    case ChainedCountRole: e->chainedCount = value.toInt(); break;
    case ChainLengthRole: e->chainLength = value.toInt(); break;
    case MorningPeriodsRole: e->morningPeriods = value.toInt(); break;
    case IsCombinedRole: e->isCombined = value.toBool(); break;
    case ClassroomIdRole: e->classroomId = value.toLongLong(); break;
    case StartWeekRole: e->startWeek = value.toInt(); break;
    case EndWeekRole: e->endWeek = value.toInt(); break;
    case SyncCourseIdRole: e->syncCourseId = value.toLongLong(); break;
    case ProgressGroupRole: e->progressGroup = value.toInt(); break;
    default: return false;
    }
    m_app->markDirty();
    const QModelIndex idx = index(row);
    Q_EMIT dataChanged(idx, idx);
    Q_EMIT countChanged(); // 周课时合计可能变化
    return true;
}

void PlanListModel::toggleTeacher(int row, qint64 teacherId)
{
    sched::PlanEntry *e = entryAtRow(row);
    if (!e)
        return;
    if (e->teacherIds.contains(teacherId))
        e->teacherIds.removeAll(teacherId);
    else
        e->teacherIds.append(teacherId);
    m_app->markDirty();
    Q_EMIT dataChanged(index(row), index(row), {TeacherIdsRole, TeacherNamesRole, TeacherWarnRole});
}

qint64 PlanListModel::addEntry(qint64 courseId)
{
    sched::PlanEntry e;
    e.classId = m_classId;
    e.courseId = courseId;
    e.weeklyPeriods = 2;
    e.endWeek = 0;
    const qint64 id = m_app->school()->addPlanEntry(e);
    fullReset();
    m_app->markDirty();
    return id;
}

bool PlanListModel::removeRow(int row)
{
    sched::PlanEntry *e = entryAtRow(row);
    if (!e)
        return false;
    const qint64 id = e->id;
    if (!m_app->school()->removePlanEntry(id))
        return false;
    fullReset();
    m_app->markDirty();
    return true;
}

int PlanListModel::copyToClass(qint64 targetClassId, bool overwrite)
{
    sched::School *s = m_app->school();
    if (targetClassId == m_classId || !s->findClass(targetClassId))
        return 0;
    int copied = 0;
    for (int srcRow : m_view) {
        const sched::PlanEntry src = *entryAt(srcRow);
        // 目标班同课已存在？
        sched::PlanEntry *existing = nullptr;
        for (auto &e : s->planEntries) {
            if (e.classId == targetClassId && e.courseId == src.courseId) {
                existing = &e;
                break;
            }
        }
        if (existing && !overwrite)
            continue;
        if (existing) {
            const qint64 keepId = existing->id;
            *existing = src;
            existing->id = keepId;
            existing->classId = targetClassId;
        } else {
            sched::PlanEntry copy = src;
            copy.id = -1; // 重新分配
            copy.classId = targetClassId;
            s->addPlanEntry(copy);
        }
        copied++;
    }
    if (copied > 0) {
        m_app->markDirty();
        Q_EMIT countChanged();
    }
    return copied;
}

double PlanListModel::totalWeeklyPeriods() const
{
    double total = 0;
    for (int srcRow : m_view)
        total += entryAt(srcRow)->weeklyPeriods;
    return total;
}

// ==================== DutyModel ====================

DutyModel::DutyModel(AppController *app, QObject *parent)
    : QAbstractListModel(parent)
    , m_app(app)
{
    connect(m_app, &AppController::projectChanged, this, [this] { fullReset(); });
}

void DutyModel::fullReset()
{
    beginResetModel();
    m_view.clear();
    const auto &entries = m_app->school()->planEntries;
    for (int i = 0; i < entries.size(); ++i) {
        if (entries[i].teacherIds.contains(m_teacherId))
            m_view.append(i);
    }
    endResetModel();
    Q_EMIT countChanged();
}

void DutyModel::setCurrentTeacherId(qint64 id)
{
    if (m_teacherId == id)
        return;
    m_teacherId = id;
    fullReset();
    Q_EMIT currentTeacherIdChanged();
}

int DutyModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_view.size();
}

QVariant DutyModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_view.size())
        return {};
    const sched::PlanEntry &e = m_app->school()->planEntries[m_view[index.row()]];
    const sched::School *s = m_app->school();
    switch (role) {
    case EntryIdRole: return e.id;
    case ClassIdRole: return e.classId;
    case ClassNameRole: {
        const sched::ClassInfo *c = s->findClass(e.classId);
        return c ? c->name : QStringLiteral("?");
    }
    case CourseIdRole: return e.courseId;
    case CourseNameRole: {
        const sched::Course *c = s->findCourse(e.courseId);
        return c ? c->name : QStringLiteral("?");
    }
    case WeeklyPeriodsRole: return e.weeklyPeriods;
    case ProgressGroupRole: return e.progressGroup;
    default: return {};
    }
}

QHash<int, QByteArray> DutyModel::roleNames() const
{
    return {{EntryIdRole, "entryId"},       {ClassIdRole, "classId"},
            {ClassNameRole, "className"},   {CourseIdRole, "courseId"},
            {CourseNameRole, "courseName"}, {WeeklyPeriodsRole, "weeklyPeriods"},
            {ProgressGroupRole, "progressGroup"}};
}

int DutyModel::setDuty(qint64 courseId, const QList<qint64> &classIds, double weeklyPeriods)
{
    sched::School *s = m_app->school();
    int affected = 0;
    for (qint64 classId : classIds) {
        sched::PlanEntry *found = nullptr;
        for (auto &e : s->planEntries) {
            if (e.classId == classId && e.courseId == courseId) {
                found = &e;
                break;
            }
        }
        if (found) {
            if (!found->teacherIds.contains(m_teacherId)) {
                found->teacherIds.append(m_teacherId);
                affected++;
            }
        } else {
            sched::PlanEntry e;
            e.classId = classId;
            e.courseId = courseId;
            e.teacherIds = {m_teacherId};
            e.weeklyPeriods = weeklyPeriods;
            s->addPlanEntry(e);
            affected++;
        }
    }
    if (affected > 0) {
        fullReset();
        m_app->markDirty();
    }
    return affected;
}

bool DutyModel::removeRow(int row)
{
    if (row < 0 || row >= m_view.size())
        return false;
    sched::PlanEntry &e = m_app->school()->planEntries[m_view[row]];
    e.teacherIds.removeAll(m_teacherId);
    fullReset();
    m_app->markDirty();
    return true;
}

QVariant DutyModel::get(int row, const QString &roleName) const
{
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.begin(); it != names.end(); ++it) {
        if (it.value() == roleName.toUtf8())
            return data(index(row), it.key());
    }
    return {};
}

bool DutyModel::set(int row, const QString &roleName, const QVariant &value)
{
    if (row < 0 || row >= m_view.size())
        return false;
    sched::PlanEntry &e = m_app->school()->planEntries[m_view[row]];
    if (roleName == QLatin1StringView("progressGroup")) {
        e.progressGroup = value.toInt();
    } else if (roleName == QLatin1StringView("weeklyPeriods")) {
        e.weeklyPeriods = value.toDouble();
    } else {
        return false;
    }
    m_app->markDirty();
    Q_EMIT dataChanged(index(row), index(row));
    return true;
}
