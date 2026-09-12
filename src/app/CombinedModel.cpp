#include "CombinedModel.h"

#include "AppController.h"

CombinedModel::CombinedModel(AppController *app, QObject *parent)
    : QAbstractListModel(parent)
    , m_app(app)
{
    connect(m_app, &AppController::projectChanged, this, [this] { fullReset(); });
}

void CombinedModel::fullReset()
{
    beginResetModel();
    endResetModel();
    Q_EMIT countChanged();
}

int CombinedModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_app->school()->combinedGroups.size();
}

int CombinedModel::count() const
{
    return m_app->school()->combinedGroups.size();
}

namespace {

// 组内成员的老师交集（用于校验与展示）
QVector<qint64> teacherIntersection(const sched::School &s, const sched::CombinedGroup &g)
{
    QVector<qint64> result;
    bool first = true;
    for (const auto &m : g.entries) {
        QVector<qint64> tids;
        for (const auto &e : s.planEntries) {
            if (e.classId == m.first && e.courseId == m.second) {
                tids = e.teacherIds;
                break;
            }
        }
        if (first) {
            result = tids;
            first = false;
        } else {
            for (auto it = result.begin(); it != result.end();) {
                if (!tids.contains(*it))
                    it = result.erase(it);
                else
                    ++it;
            }
        }
    }
    return result;
}

} // namespace

QVariant CombinedModel::data(const QModelIndex &index, int role) const
{
    const auto &groups = m_app->school()->combinedGroups;
    if (!index.isValid() || index.row() < 0 || index.row() >= groups.size())
        return {};
    const sched::CombinedGroup &g = groups[index.row()];
    const sched::School *s = m_app->school();
    switch (role) {
    case IdRole: return g.id;
    case NameRole: return g.name;
    case MemberCountRole: return g.entries.size();
    case MemberTextRole: {
        QStringList parts;
        for (const auto &m : g.entries) {
            const sched::ClassInfo *c = s->findClass(m.first);
            const sched::Course *co = s->findCourse(m.second);
            parts.append(QStringLiteral("%1·%2")
                             .arg(c ? c->name : QStringLiteral("?"),
                                  co ? co->name : QStringLiteral("?")));
        }
        return parts.join(QStringLiteral("、"));
    }
    case CourseNameRole: {
        if (g.entries.isEmpty())
            return QString();
        const sched::Course *co = s->findCourse(g.entries.first().second);
        return co ? co->name : QStringLiteral("?");
    }
    case TeacherNamesRole: {
        QStringList names;
        for (qint64 tid : teacherIntersection(*s, g)) {
            if (const sched::Teacher *t = s->findTeacher(tid))
                names.append(t->name);
        }
        return names.join(QStringLiteral("、"));
    }
    case MemberListRole: {
        QVariantList list;
        for (const auto &m : g.entries) {
            const sched::ClassInfo *c = s->findClass(m.first);
            const sched::Course *co = s->findCourse(m.second);
            list.append(QVariantMap{
                {QStringLiteral("classId"), m.first},
                {QStringLiteral("courseId"), m.second},
                {QStringLiteral("label"),
                 QStringLiteral("%1·%2").arg(c ? c->name : QStringLiteral("?"),
                                             co ? co->name : QStringLiteral("?"))}});
        }
        return list;
    }
    case ValidRole: {
        if (g.entries.size() < 2)
            return false;
        const qint64 courseId = g.entries.first().second;
        for (const auto &m : g.entries) {
            if (m.second != courseId)
                return false;
        }
        return !teacherIntersection(*s, g).isEmpty();
    }
    default: return {};
    }
}

QHash<int, QByteArray> CombinedModel::roleNames() const
{
    return {{IdRole, "id"},           {NameRole, "name"},
            {MemberCountRole, "memberCount"}, {MemberTextRole, "memberText"},
            {CourseNameRole, "courseName"},   {TeacherNamesRole, "teacherNames"},
            {MemberListRole, "memberList"},   {ValidRole, "valid"}};
}

qint64 CombinedModel::addGroup(const QString &name)
{
    sched::CombinedGroup g;
    g.name = name.isEmpty() ? QStringLiteral("新合班组") : name;
    const qint64 id = m_app->school()->addCombinedGroup(g);
    fullReset();
    m_app->markDirty();
    return id;
}

bool CombinedModel::removeRow(int row)
{
    if (row < 0 || row >= count())
        return false;
    const qint64 id = m_app->school()->combinedGroups[row].id;
    if (!m_app->school()->removeCombinedGroup(id))
        return false;
    fullReset();
    m_app->markDirty();
    return true;
}

qint64 CombinedModel::idAt(int row) const
{
    const auto &groups = m_app->school()->combinedGroups;
    return (row >= 0 && row < groups.size()) ? groups[row].id : -1;
}

QVariant CombinedModel::get(int row, const QString &roleName) const
{
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.begin(); it != names.end(); ++it) {
        if (it.value() == roleName.toUtf8())
            return data(index(row), it.key());
    }
    return {};
}

QString CombinedModel::validateAdd(const sched::CombinedGroup &g, const sched::PlanEntry &e,
                                   QString *error) const
{
    Q_UNUSED(error);
    // 课程一致性
    for (const auto &m : g.entries) {
        if (m.second != e.courseId)
            return QStringLiteral("合班组成员课程必须一致（组内为其他课程）");
    }
    // 已在组内？
    for (const auto &m : g.entries) {
        if (m.first == e.classId && m.second == e.courseId)
            return QStringLiteral("该班的这门课已在组内");
    }
    // 老师交集：新条目与现有成员至少共享一位老师
    if (!g.entries.isEmpty()) {
        const sched::School *s = m_app->school();
        bool shared = false;
        for (const auto &m : g.entries) {
            for (const auto &me : s->planEntries) {
                if (me.classId != m.first || me.courseId != m.second)
                    continue;
                for (qint64 t : e.teacherIds) {
                    if (me.teacherIds.contains(t))
                        shared = true;
                }
            }
        }
        if (!shared)
            return QStringLiteral("合班组成员必须有共同的任课老师");
    }
    return {};
}

QString CombinedModel::addEntry(qint64 groupId, qint64 entryId)
{
    sched::School *s = m_app->school();
    sched::CombinedGroup *g = s->findCombinedGroup(groupId);
    sched::PlanEntry *e = s->findPlanEntry(entryId);
    if (!g || !e)
        return QStringLiteral("组或条目不存在");
    const QString err = validateAdd(*g, *e, nullptr);
    if (!err.isEmpty())
        return err;
    g->entries.append({e->classId, e->courseId});
    m_app->markDirty();
    Q_EMIT dataChanged(index(0), index(count() - 1));
    return {};
}

void CombinedModel::removeEntry(qint64 groupId, qint64 classId, qint64 courseId)
{
    sched::School *s = m_app->school();
    sched::CombinedGroup *g = s->findCombinedGroup(groupId);
    if (!g)
        return;
    for (auto it = g->entries.begin(); it != g->entries.end(); ++it) {
        if (it->first == classId && it->second == courseId) {
            g->entries.erase(it);
            break;
        }
    }
    m_app->markDirty();
    Q_EMIT dataChanged(index(0), index(count() - 1));
}

QVariantList CombinedModel::unlinkedEntries() const
{
    QVariantList out;
    const sched::School *s = m_app->school();
    for (const auto &e : s->planEntries) {
        if (!e.isCombined)
            continue;
        bool linked = false;
        for (const auto &g : s->combinedGroups) {
            if (g.entries.contains({e.classId, e.courseId})) {
                linked = true;
                break;
            }
        }
        if (linked)
            continue;
        const sched::ClassInfo *c = s->findClass(e.classId);
        const sched::Course *co = s->findCourse(e.courseId);
        QStringList teacherNames;
        for (qint64 tid : e.teacherIds) {
            if (const sched::Teacher *t = s->findTeacher(tid))
                teacherNames.append(t->name);
        }
        out.append(QVariantMap{
            {QStringLiteral("entryId"), e.id},
            {QStringLiteral("text"),
             QStringLiteral("%1 · %2 · %3")
                 .arg(c ? c->name : QStringLiteral("?"), co ? co->name : QStringLiteral("?"),
                      teacherNames.join(QStringLiteral("、")))},
            {QStringLiteral("courseId"), e.courseId}});
    }
    return out;
}
