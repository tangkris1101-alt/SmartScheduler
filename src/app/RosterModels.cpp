#include "RosterModels.h"

#include "AppController.h"

#include <QRegularExpression>

namespace {

// 生成不与现有名单冲突的默认名。
// numericSeries（班级）：现有名字统一为「前缀+数字+班」时顺延同前缀下一个号
// （1班/2班 → 3班；高一1班/高一2班 → 高一3班）；空名单从「1班」起；
// 前缀不一致或无数字命名习惯时，回退为 base、base2……去重。
QString nextEntityName(const QString &base, const QStringList &existing, bool numericSeries)
{
    if (numericSeries) {
        static const QRegularExpression re(QStringLiteral("^(.*?)(\\d+)班$"));
        QString prefix;
        bool prefixSet = false, mixed = false;
        int maxNum = 0;
        for (const QString &n : existing) {
            const QRegularExpressionMatch m = re.match(n);
            if (!m.hasMatch())
                continue; // 不规则命名不参与顺延
            if (!prefixSet) {
                prefix = m.captured(1);
                prefixSet = true;
            } else if (m.captured(1) != prefix) {
                mixed = true; // 前缀不一致（如高一1班/高二1班并存），无法判断顺延谁
                break;
            }
            maxNum = std::max(maxNum, m.captured(2).toInt());
        }
        if (prefixSet && !mixed) {
            QString cand;
            do {
                cand = prefix + QString::number(++maxNum) + QStringLiteral("班");
            } while (existing.contains(cand));
            return cand;
        }
        if (existing.isEmpty())
            return QStringLiteral("1班");
    }
    if (!existing.contains(base))
        return base;
    for (int i = 2;; ++i) {
        const QString cand = base + QString::number(i);
        if (!existing.contains(cand))
            return cand;
    }
}

template <typename List>
QStringList entityNames(const List &entities)
{
    QStringList names;
    names.reserve(entities.size());
    for (const auto &e : entities)
        names.append(e.name);
    return names;
}

} // namespace

// ==================== NamedEntityModel ====================

NamedEntityModel::NamedEntityModel(AppController *app, QObject *parent)
    : QAbstractListModel(parent)
    , m_app(app)
{
    // 项目切换（新建/打开/关闭）后全量重建视图；首次重建由子类构造末尾触发
    connect(m_app, &AppController::projectChanged, this, [this] { fullReset(); });
}

sched::School *NamedEntityModel::school() const
{
    return m_app->school();
}

int NamedEntityModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_view.size();
}

Qt::ItemFlags NamedEntityModel::flags(const QModelIndex &index) const
{
    return index.isValid() ? (Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsEnabled)
                           : Qt::NoItemFlags;
}

bool NamedEntityModel::matchesFilter(const sched::NamedEntity &e) const
{
    if (m_filter.isEmpty())
        return true;
    const QString f = m_filter.toLower();
    return e.name.contains(m_filter, Qt::CaseInsensitive) || e.code.toLower().contains(f)
        || e.longName.contains(m_filter, Qt::CaseInsensitive)
        || e.shortName.contains(m_filter, Qt::CaseInsensitive);
}

void NamedEntityModel::fullReset()
{
    beginResetModel();
    m_view.clear();
    const int n = sourceCount();
    for (int i = 0; i < n; ++i) {
        if (matchesFilter(*entityAt(i)))
            m_view.append(i);
    }
    endResetModel();
    Q_EMIT countChanged();
}

void NamedEntityModel::setFilter(const QString &filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    fullReset();
    Q_EMIT filterChanged();
}

QVariant NamedEntityModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_view.size())
        return {};
    const int src = m_view[index.row()];
    // 防御：项目切换的信号级联中 m_view 可能短暂指向已替换的旧数据，越界读取会崩溃
    if (src < 0 || src >= sourceCount())
        return {};
    const sched::NamedEntity *e = entityAt(src);
    switch (role) {
    case IdRole: return e->id;
    case NameRole: return e->name;
    case LongNameRole: return e->longName;
    case ShortNameRole: return e->shortName;
    case CodeRole: return e->code;
    case PriorityRole: return e->priority;
    case RemarkRole: return e->remark;
    default: return extraData(role, *e);
    }
}

bool NamedEntityModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_view.size())
        return false;
    const int src = m_view[index.row()];
    if (src < 0 || src >= sourceCount())
        return false;
    sched::NamedEntity *e = entityAt(src);
    bool ok = true;
    switch (role) {
    case NameRole: e->name = value.toString(); break;
    case LongNameRole: e->longName = value.toString(); break;
    case ShortNameRole: e->shortName = value.toString(); break;
    case CodeRole: e->code = value.toString(); break;
    case PriorityRole: e->priority = value.toInt(); break;
    case RemarkRole: e->remark = value.toString(); break;
    default: ok = extraSet(role, *e, value); break;
    }
    if (!ok)
        return false;
    m_app->markDirty();
    Q_EMIT dataChanged(index, index, {role});
    return true;
}

QVariant NamedEntityModel::extraData(int role, const sched::NamedEntity &e) const
{
    Q_UNUSED(role);
    Q_UNUSED(e);
    return {};
}

bool NamedEntityModel::extraSet(int role, sched::NamedEntity &e, const QVariant &value)
{
    Q_UNUSED(role);
    Q_UNUSED(e);
    Q_UNUSED(value);
    return false;
}

QHash<int, QByteArray> NamedEntityModel::roleNames() const
{
    QHash<int, QByteArray> names = {
        {IdRole, "id"},       {NameRole, "name"},         {LongNameRole, "longName"},
        {CodeRole, "code"},   {ShortNameRole, "shortName"}, {PriorityRole, "priority"},
        {RemarkRole, "remark"},
    };
    names.insert(extraRoleNames());
    return names;
}

int NamedEntityModel::roleForName(const QString &name) const
{
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.begin(); it != names.end(); ++it) {
        if (it.value() == name.toUtf8())
            return it.key();
    }
    return -1;
}

QVariant NamedEntityModel::get(int row, const QString &roleName) const
{
    const int role = roleForName(roleName);
    if (role < 0)
        return {};
    return data(index(row), role);
}

bool NamedEntityModel::set(int row, const QString &roleName, const QVariant &value)
{
    const int role = roleForName(roleName);
    if (role < 0)
        return false;
    return setData(index(row), value, role);
}

qint64 NamedEntityModel::appendRow()
{
    sched::NamedEntity *e = createEntity();
    fullReset();
    m_app->markDirty();
    return e->id;
}

int NamedEntityModel::appendRows(const QStringList &names)
{
    int added = 0;
    for (const QString &raw : names) {
        const QString name = raw.trimmed();
        if (name.isEmpty())
            continue;
        sched::NamedEntity *e = createEntity();
        e->name = name;
        ++added;
    }
    if (added > 0) {
        fullReset();
        m_app->markDirty();
    }
    return added;
}

bool NamedEntityModel::removeRow(int row)
{
    if (row < 0 || row >= m_view.size())
        return false;
    const qint64 id = entityAt(m_view[row])->id;
    if (!removeEntity(id))
        return false;
    fullReset();
    m_app->markDirty();
    return true;
}

qint64 NamedEntityModel::idAt(int row) const
{
    if (row < 0 || row >= m_view.size())
        return -1;
    return entityAt(m_view[row])->id;
}

// ==================== ClassListModel ====================

ClassListModel::ClassListModel(AppController *app, QObject *parent)
    : NamedEntityModel(app, parent)
{
    fullReset();
}

int ClassListModel::sourceCount() const { return school()->classes.size(); }
sched::NamedEntity *ClassListModel::entityAt(int sourceRow) const
{
    return const_cast<sched::ClassInfo *>(&school()->classes[sourceRow]);
}
sched::NamedEntity *ClassListModel::createEntity()
{
    sched::ClassInfo c;
    c.name = nextEntityName(QStringLiteral("新班级"), entityNames(school()->classes), true);
    const qint64 id = school()->addClass(c);
    return school()->findClass(id);
}
bool ClassListModel::removeEntity(qint64 id) { return school()->removeClass(id); }

QVariant ClassListModel::extraData(int role, const sched::NamedEntity &e) const
{
    const auto &c = static_cast<const sched::ClassInfo &>(e);
    switch (role) {
    case GradeIdRole: return c.gradeId;
    case GradeNameRole: {
        const sched::Group *g = school()->findGroup(c.gradeId);
        return g ? g->name : QString();
    }
    default: return {};
    }
}

bool ClassListModel::extraSet(int role, sched::NamedEntity &e, const QVariant &value)
{
    auto &c = static_cast<sched::ClassInfo &>(e);
    if (role == GradeIdRole) {
        c.gradeId = value.toLongLong();
        return true;
    }
    return false;
}

QHash<int, QByteArray> ClassListModel::extraRoleNames() const
{
    return {{GradeIdRole, "gradeId"}, {GradeNameRole, "gradeName"}};
}

// ==================== CourseListModel ====================

CourseListModel::CourseListModel(AppController *app, QObject *parent)
    : NamedEntityModel(app, parent)
{
    fullReset();
}

int CourseListModel::sourceCount() const { return school()->courses.size(); }
sched::NamedEntity *CourseListModel::entityAt(int sourceRow) const
{
    return const_cast<sched::Course *>(&school()->courses[sourceRow]);
}
sched::NamedEntity *CourseListModel::createEntity()
{
    sched::Course c;
    c.name = nextEntityName(QStringLiteral("新课程"), entityNames(school()->courses), false);
    const qint64 id = school()->addCourse(c);
    return school()->findCourse(id);
}
bool CourseListModel::removeEntity(qint64 id) { return school()->removeCourse(id); }

QVariant CourseListModel::extraData(int role, const sched::NamedEntity &e) const
{
    const auto &c = static_cast<const sched::Course &>(e);
    switch (role) {
    case GroupIdRole: return c.groupId;
    case IsMajorRole: return c.isMajor;
    case IsScienceRole: return c.isScience;
    case EvenSpreadRole: return c.evenSpread;
    case ProgressAlignCountRole: return c.progressAlignCount;
    case AmPmPolicyRole: return static_cast<int>(c.amPmPolicy);
    case AmPmTextRole:
        switch (c.amPmPolicy) {
        case sched::AmPmPolicy::Morning: return QStringLiteral("优先上午");
        case sched::AmPmPolicy::Afternoon: return QStringLiteral("优先下午");
        default: return QStringLiteral("自动");
        }
    default: return {};
    }
}

bool CourseListModel::extraSet(int role, sched::NamedEntity &e, const QVariant &value)
{
    auto &c = static_cast<sched::Course &>(e);
    switch (role) {
    case GroupIdRole: c.groupId = value.toLongLong(); return true;
    case IsMajorRole: c.isMajor = value.toBool(); return true;
    case IsScienceRole: c.isScience = value.toBool(); return true;
    case EvenSpreadRole: c.evenSpread = value.toBool(); return true;
    case ProgressAlignCountRole: c.progressAlignCount = value.toInt(); return true;
    case AmPmPolicyRole: c.amPmPolicy = static_cast<sched::AmPmPolicy>(value.toInt()); return true;
    default: return false;
    }
}

QHash<int, QByteArray> CourseListModel::extraRoleNames() const
{
    return {{GroupIdRole, "groupId"},         {IsMajorRole, "isMajor"},
            {IsScienceRole, "isScience"},     {EvenSpreadRole, "evenSpread"},
            {ProgressAlignCountRole, "progressAlignCount"},
            {AmPmPolicyRole, "amPmPolicy"},   {AmPmTextRole, "amPmText"}};
}

// ==================== TeacherListModel ====================

TeacherListModel::TeacherListModel(AppController *app, QObject *parent)
    : NamedEntityModel(app, parent)
{
    fullReset();
}

int TeacherListModel::sourceCount() const { return school()->teachers.size(); }
sched::NamedEntity *TeacherListModel::entityAt(int sourceRow) const
{
    return const_cast<sched::Teacher *>(&school()->teachers[sourceRow]);
}
sched::NamedEntity *TeacherListModel::createEntity()
{
    sched::Teacher t;
    t.name = nextEntityName(QStringLiteral("新老师"), entityNames(school()->teachers), false);
    const qint64 id = school()->addTeacher(t);
    return school()->findTeacher(id);
}
bool TeacherListModel::removeEntity(qint64 id) { return school()->removeTeacher(id); }

QVariant TeacherListModel::extraData(int role, const sched::NamedEntity &e) const
{
    const auto &t = static_cast<const sched::Teacher &>(e);
    switch (role) {
    case GroupIdRole: return t.groupId;
    case MaxPeriodsPerDayRole: return t.maxPeriodsPerDay;
    case MutualExclusionGroupRole: return t.mutualExclusionGroup;
    default: return {};
    }
}

bool TeacherListModel::extraSet(int role, sched::NamedEntity &e, const QVariant &value)
{
    auto &t = static_cast<sched::Teacher &>(e);
    switch (role) {
    case GroupIdRole: t.groupId = value.toLongLong(); return true;
    case MaxPeriodsPerDayRole: t.maxPeriodsPerDay = value.toInt(); return true;
    case MutualExclusionGroupRole: t.mutualExclusionGroup = value.toLongLong(); return true;
    default: return false;
    }
}

QHash<int, QByteArray> TeacherListModel::extraRoleNames() const
{
    return {{GroupIdRole, "groupId"},
            {MaxPeriodsPerDayRole, "maxPeriodsPerDay"},
            {MutualExclusionGroupRole, "mutualExclusionGroup"}};
}

// ==================== ClassroomListModel ====================

ClassroomListModel::ClassroomListModel(AppController *app, QObject *parent)
    : NamedEntityModel(app, parent)
{
    fullReset();
}

int ClassroomListModel::sourceCount() const { return school()->classrooms.size(); }
sched::NamedEntity *ClassroomListModel::entityAt(int sourceRow) const
{
    return const_cast<sched::Classroom *>(&school()->classrooms[sourceRow]);
}
sched::NamedEntity *ClassroomListModel::createEntity()
{
    sched::Classroom c;
    c.name = nextEntityName(QStringLiteral("新教室"), entityNames(school()->classrooms), false);
    const qint64 id = school()->addClassroom(c);
    return school()->findClassroom(id);
}
bool ClassroomListModel::removeEntity(qint64 id) { return school()->removeClassroom(id); }

QVariant ClassroomListModel::extraData(int role, const sched::NamedEntity &e) const
{
    const auto &c = static_cast<const sched::Classroom &>(e);
    if (role == CapacityRole)
        return c.capacity;
    return {};
}

bool ClassroomListModel::extraSet(int role, sched::NamedEntity &e, const QVariant &value)
{
    auto &c = static_cast<sched::Classroom &>(e);
    if (role == CapacityRole) {
        c.capacity = std::max(1, value.toInt());
        return true;
    }
    return false;
}

QHash<int, QByteArray> ClassroomListModel::extraRoleNames() const
{
    return {{CapacityRole, "capacity"}};
}
