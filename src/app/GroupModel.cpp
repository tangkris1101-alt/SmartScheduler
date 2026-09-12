#include "GroupModel.h"

#include "AppController.h"

GroupModel::GroupModel(AppController *app, QObject *parent)
    : QAbstractListModel(parent)
    , m_app(app)
{
    connect(m_app, &AppController::projectChanged, this, [this] { fullReset(); });
}

void GroupModel::fullReset()
{
    beginResetModel();
    endResetModel();
    Q_EMIT countChanged();
}

int GroupModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_app->school()->groups.size();
}

int GroupModel::count() const
{
    return m_app->school()->groups.size();
}

QString GroupModel::typeName(int type) const
{
    switch (static_cast<sched::Group::Type>(type)) {
    case sched::Group::Type::Grade: return QStringLiteral("年级");
    case sched::Group::Type::TeacherGroup: return QStringLiteral("老师教研组");
    case sched::Group::Type::CourseGroup: return QStringLiteral("课程教研组");
    }
    return {};
}

QString GroupModel::memberNames(const sched::Group &g) const
{
    QStringList names;
    const sched::School *s = m_app->school();
    for (qint64 id : g.memberIds) {
        QString name;
        switch (g.type) {
        case sched::Group::Type::Grade:
            if (const auto *e = s->findClass(id))
                name = e->name;
            break;
        case sched::Group::Type::TeacherGroup:
            if (const auto *e = s->findTeacher(id))
                name = e->name;
            break;
        case sched::Group::Type::CourseGroup:
            if (const auto *e = s->findCourse(id))
                name = e->name;
            break;
        }
        if (!name.isEmpty())
            names.append(name);
    }
    return names.join(QStringLiteral("、"));
}

QVariant GroupModel::data(const QModelIndex &index, int role) const
{
    const auto &groups = m_app->school()->groups;
    if (!index.isValid() || index.row() < 0 || index.row() >= groups.size())
        return {};
    const sched::Group &g = groups[index.row()];
    switch (role) {
    case IdRole: return g.id;
    case NameRole: return g.name;
    case TypeRole: return static_cast<int>(g.type);
    case TypeNameRole: return typeName(static_cast<int>(g.type));
    case MemberCountRole: return g.memberIds.size();
    case MemberNamesRole: return memberNames(g);
    default: return {};
    }
}

bool GroupModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    auto &groups = m_app->school()->groups;
    if (!index.isValid() || index.row() < 0 || index.row() >= groups.size())
        return false;
    sched::Group &g = groups[index.row()];
    if (role == NameRole) {
        g.name = value.toString();
    } else if (role == TypeRole) {
        const auto newType = static_cast<sched::Group::Type>(value.toInt());
        if (newType != g.type) {
            g.type = newType;
            g.memberIds.clear(); // 类型改变后旧成员不再有效
        }
    } else {
        return false;
    }
    m_app->markDirty();
    Q_EMIT dataChanged(index, index);
    return true;
}

QHash<int, QByteArray> GroupModel::roleNames() const
{
    return {{IdRole, "id"},           {NameRole, "name"},
            {TypeRole, "type"},       {TypeNameRole, "typeName"},
            {MemberCountRole, "memberCount"}, {MemberNamesRole, "memberNames"}};
}

qint64 GroupModel::addGroup(int type, const QString &name)
{
    sched::Group g;
    g.type = static_cast<sched::Group::Type>(type);
    g.name = name.isEmpty() ? QStringLiteral("新名单") : name;
    const qint64 id = m_app->school()->addGroup(g);
    fullReset();
    m_app->markDirty();
    return id;
}

bool GroupModel::removeRow(int row)
{
    if (row < 0 || row >= m_app->school()->groups.size())
        return false;
    const qint64 id = m_app->school()->groups[row].id;
    if (!m_app->school()->removeGroup(id))
        return false;
    fullReset();
    m_app->markDirty();
    return true;
}

int GroupModel::rowOfGroup(qint64 groupId) const
{
    const auto &groups = m_app->school()->groups;
    for (int i = 0; i < groups.size(); ++i) {
        if (groups[i].id == groupId)
            return i;
    }
    return -1;
}

QVariant GroupModel::get(int row, const QString &roleName) const
{
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.begin(); it != names.end(); ++it) {
        if (it.value() == roleName.toUtf8())
            return data(index(row), it.key());
    }
    return {};
}

bool GroupModel::set(int row, const QString &roleName, const QVariant &value)
{
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.begin(); it != names.end(); ++it) {
        if (it.value() == roleName.toUtf8())
            return setData(index(row), value, it.key());
    }
    return false;
}

qint64 GroupModel::idAt(int row) const
{
    const auto &groups = m_app->school()->groups;
    return (row >= 0 && row < groups.size()) ? groups[row].id : -1;
}

QVariantList GroupModel::candidates(qint64 groupId) const
{
    QVariantList out;
    const sched::School *s = m_app->school();
    const sched::Group *g = s->findGroup(groupId);
    if (!g)
        return out;
    auto addAll = [&out](const auto &entities) {
        for (const auto &e : entities)
            out.append(QVariantMap{{QStringLiteral("id"), e.id}, {QStringLiteral("name"), e.name}});
    };
    switch (g->type) {
    case sched::Group::Type::Grade: addAll(s->classes); break;
    case sched::Group::Type::TeacherGroup: addAll(s->teachers); break;
    case sched::Group::Type::CourseGroup: addAll(s->courses); break;
    }
    return out;
}

bool GroupModel::isMember(qint64 groupId, qint64 memberId) const
{
    const sched::Group *g = m_app->school()->findGroup(groupId);
    return g && g->memberIds.contains(memberId);
}

void GroupModel::toggleMember(qint64 groupId, qint64 memberId)
{
    sched::Group *g = m_app->school()->findGroup(groupId);
    if (!g)
        return;
    if (g->memberIds.contains(memberId))
        g->memberIds.removeAll(memberId);
    else
        g->memberIds.append(memberId);
    m_app->markDirty();
    const int row = rowOfGroup(groupId);
    if (row >= 0)
        Q_EMIT dataChanged(index(row), index(row), {MemberCountRole, MemberNamesRole});
}
