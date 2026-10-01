#include "AppController.h"

#include <QSet>
#include <QStringList>

// 本文件承接「AI 工具扩展」：连堂设置、前 N 节、任课老师、老师日最多课、
// 按组批量不排，以及对应的只读工具（老师课表）。主文件只保留一个转发钩子。

namespace {

// 名称匹配：先精确，再包含（与主文件同一规则）
template<typename T>
qint64 matchEntity(const QVector<T> &list, const QString &name)
{
    const QString want = name.trimmed();
    if (want.isEmpty())
        return -1;
    for (const auto &e : list) {
        if (e.name == want)
            return e.id;
    }
    for (const auto &e : list) {
        if (e.name.contains(want, Qt::CaseInsensitive) || want.contains(e.name, Qt::CaseInsensitive))
            return e.id;
    }
    return -1;
}

} // namespace

bool AppController::aiRunReadToolExtra(const QString &tool, const QVariantMap &args,
                                       QVariantMap *out) const
{
    const int ppd = m_school.timeConfig.periodsPerDay;
    if (tool == QLatin1StringView("list_time_sync_groups")) {
        *out = QVariantMap{{QStringLiteral("ok"), true},
                           {QStringLiteral("data"), timeSyncGroups()}};
        return true;
    }
    if (tool == QLatin1StringView("query_teacher_schedule")) {
        const QString name = args.value(QStringLiteral("teacherName")).toString();
        const qint64 tid = matchEntity(m_school.teachers, name);
        if (tid < 0) {
            *out = QVariantMap{{QStringLiteral("ok"), false},
                               {QStringLiteral("error"), tr("找不到老师：%1（可先用 list_teachers）").arg(name)}};
            return true;
        }
        QVariantList rows;
        for (const auto &a : m_school.schedule.assignments) {
            for (const auto &e : m_school.planEntries) {
                if (e.classId != a.classId || e.courseId != a.courseId
                    || !e.teacherIds.contains(tid))
                    continue;
                const sched::ClassInfo *c = m_school.findClass(a.classId);
                const sched::Course *co = m_school.findCourse(a.courseId);
                rows.append(QVariantMap{{QStringLiteral("星期"), a.slotIndex / ppd + 1},
                                        {QStringLiteral("节次"), a.slotIndex % ppd + 1},
                                        {QStringLiteral("班级"), c ? c->name : QString()},
                                        {QStringLiteral("课程"), co ? co->name : QString()}});
                break;
            }
        }
        QVariantList planRows;
        for (const auto &e : m_school.planEntries) {
            if (!e.teacherIds.contains(tid))
                continue;
            const sched::ClassInfo *c = m_school.findClass(e.classId);
            const sched::Course *co = m_school.findCourse(e.courseId);
            planRows.append(QVariantMap{{QStringLiteral("班级"), c ? c->name : QString()},
                                        {QStringLiteral("课程"), co ? co->name : QString()},
                                        {QStringLiteral("周节数"), e.weeklyPeriods}});
        }
        *out = QVariantMap{{QStringLiteral("ok"), true},
                           {QStringLiteral("data"),
                            QVariantMap{{QStringLiteral("任课"), planRows},
                                        {QStringLiteral("已排时段"), rows}}}};
        return true;
    }
    return false;
}

QVariantList AppController::aiToolSchemasExtra() const
{
    const auto fn = [](const QString &name, const QString &desc, const QVariantMap &props,
                       const QVariantList &required) {
        return QVariantMap{{QStringLiteral("name"), name},
                           {QStringLiteral("description"), desc},
                           {QStringLiteral("parameters"),
                            QVariantMap{{QStringLiteral("type"), QStringLiteral("object")},
                                        {QStringLiteral("properties"), props},
                                        {QStringLiteral("required"), required}}}};
    };
    const QVariantMap classNames{{QStringLiteral("type"), QStringLiteral("array")},
                                 {QStringLiteral("description"), tr("班级名列表")}};
    const QVariantMap courseName{{QStringLiteral("type"), QStringLiteral("string")},
                                 {QStringLiteral("description"), tr("课程名")}};
    const QVariantMap days{{QStringLiteral("type"), QStringLiteral("array")},
                           {QStringLiteral("description"), tr("星期列表，1=周一…7=周日；省略=每天")}};
    const QVariantMap periods{{QStringLiteral("type"), QStringLiteral("array")},
                              {QStringLiteral("description"), tr("节次列表，1 起；省略=该天全部节次")}};
    QVariantList out;
    out << fn(QStringLiteral("create_time_sync_group"),
              tr("新建同时段组（走班）：组内「班级-课程」的第 k 次课必须排在同一节，"
                 "例如 16班生物 = 14班政治 = 1班地理 同一节"),
              {{QStringLiteral("name"),
                QVariantMap{{QStringLiteral("type"), QStringLiteral("string")},
                            {QStringLiteral("description"), tr("组名，如「高三走班1」")}}},
               {QStringLiteral("entries"),
                QVariantMap{{QStringLiteral("type"), QStringLiteral("array")},
                            {QStringLiteral("description"),
                             tr("成员列表 [{className, courseName}]，至少两个")}}}},
              {QStringLiteral("entries")});
    out << fn(QStringLiteral("remove_time_sync_group"),
              tr("删除同时段组（按组名）"),
              {{QStringLiteral("name"),
                QVariantMap{{QStringLiteral("type"), QStringLiteral("string")},
                            {QStringLiteral("description"), tr("组名（可先用 list_time_sync_groups）")}}}},
              {QStringLiteral("name")});
    out << fn(QStringLiteral("list_time_sync_groups"), tr("列出全部同时段组"), {}, {});
    out << fn(QStringLiteral("query_teacher_schedule"),
              tr("查看某位老师教哪些班、什么时段（帮你在改之前定位）"),
              {{QStringLiteral("teacherName"),
                QVariantMap{{QStringLiteral("type"), QStringLiteral("string")},
                            {QStringLiteral("description"), tr("老师姓名")}}}},
              {QStringLiteral("teacherName")});
    out << fn(QStringLiteral("set_plan_chained"),
              tr("设置连堂：某课程在一批班里每周连堂几次、每次几节（times=0 表示取消）"),
              {{QStringLiteral("courseName"), courseName},
               {QStringLiteral("classNames"), classNames},
               {QStringLiteral("times"),
                QVariantMap{{QStringLiteral("type"), QStringLiteral("integer")},
                            {QStringLiteral("description"), tr("每周连堂次数")}}},
               {QStringLiteral("length"),
                QVariantMap{{QStringLiteral("type"), QStringLiteral("integer")},
                            {QStringLiteral("description"), tr("每次连几节，默认 2")}}}},
              {QStringLiteral("courseName"), QStringLiteral("classNames"),
               QStringLiteral("times")});
    out << fn(QStringLiteral("set_plan_early_periods"),
              tr("设置「前 N 节」偏好（软约束）：这门课尽量排在每天前 N 节，0 表示取消"),
              {{QStringLiteral("courseName"), courseName},
               {QStringLiteral("classNames"), classNames},
               {QStringLiteral("earlyPeriods"),
                QVariantMap{{QStringLiteral("type"), QStringLiteral("integer")},
                            {QStringLiteral("description"), tr("前几节，如 3")}}}},
              {QStringLiteral("courseName"), QStringLiteral("classNames"),
               QStringLiteral("earlyPeriods")});
    out << fn(QStringLiteral("set_plan_teachers"),
              tr("设置任课老师：rotate=true 时按班轮流分配（如自习每班一位看班老师）"),
              {{QStringLiteral("courseName"), courseName},
               {QStringLiteral("classNames"), classNames},
               {QStringLiteral("teacherNames"),
                QVariantMap{{QStringLiteral("type"), QStringLiteral("array")},
                            {QStringLiteral("description"), tr("老师姓名列表")}}},
               {QStringLiteral("rotate"),
                QVariantMap{{QStringLiteral("type"), QStringLiteral("boolean")},
                            {QStringLiteral("description"), tr("是否按班轮流分配，默认 false")}}}},
              {QStringLiteral("courseName"), QStringLiteral("classNames"),
               QStringLiteral("teacherNames")});
    out << fn(QStringLiteral("set_teacher_daily_max"),
              tr("设置老师「一天最多课」（软约束；-1 表示不限）"),
              {{QStringLiteral("teacherNames"),
                QVariantMap{{QStringLiteral("type"), QStringLiteral("array")},
                            {QStringLiteral("description"), tr("老师姓名列表")}}},
               {QStringLiteral("maxPerDay"),
                QVariantMap{{QStringLiteral("type"), QStringLiteral("integer")},
                            {QStringLiteral("description"), tr("每天最多几节")}}}},
              {QStringLiteral("teacherNames"), QStringLiteral("maxPerDay")});
    out << fn(QStringLiteral("batch_no_schedule_by_group"),
              tr("按组批量设置/恢复「不排时间」：年级组=班级、教研组=老师、课程组=课程"),
              {{QStringLiteral("groupName"),
                QVariantMap{{QStringLiteral("type"), QStringLiteral("string")},
                            {QStringLiteral("description"), tr("组名（可先用 list_groups 查看）")}}},
               {QStringLiteral("days"), days},
               {QStringLiteral("periods"), periods},
               {QStringLiteral("forbidden"),
                QVariantMap{{QStringLiteral("type"), QStringLiteral("boolean")},
                            {QStringLiteral("description"), tr("true 禁排 / false 恢复，默认 true")}}}},
              {QStringLiteral("groupName"), QStringLiteral("days")});
    return out;
}

// ---------------------------------------------------------------- 干跑
bool AppController::planAiOperationExtra(const QString &tool, const QVariantMap &args,
                                         AiOpPlan *out) const
{
    AiOpPlan p;
    const auto resolveClasses = [&](const QVariantList &names, QVariantList *ids, QStringList *missing) {
        for (const QVariant &v : names) {
            const QString name = v.toString().trimmed();
            if (name.isEmpty())
                continue;
            const qint64 id = matchEntity(m_school.classes, name);
            if (id < 0)
                missing->append(name);
            else
                ids->append(id);
        }
    };
    const auto resolveSlots = [&](const QVariantList &days, const QVariantList &periods,
                                  QVariantList *slots) {
        const sched::TimeConfig &tc = m_school.timeConfig;
        for (const QVariant &dv : days) {
            const int day = dv.toInt();
            if (day < 1 || day > tc.daysPerWeek)
                continue;
            if (!periods.isEmpty()) {
                for (const QVariant &pv : periods) {
                    const int per = pv.toInt();
                    if (per >= 1 && per <= tc.periodsPerDay)
                        slots->append((day - 1) * tc.periodsPerDay + per - 1);
                }
            } else {
                for (int per = 1; per <= tc.periodsPerDay; ++per)
                    slots->append((day - 1) * tc.periodsPerDay + per - 1);
            }
        }
    };
    const auto courseNameOf = [this](qint64 id) {
        const sched::Course *c = m_school.findCourse(id);
        return c ? c->name : QString();
    };

    if (tool == QLatin1StringView("set_plan_chained")) {
        p.title = tr("设置连堂");
        p.courseId = matchEntity(m_school.courses, args.value(QStringLiteral("courseName")).toString());
        if (p.courseId < 0) {
            p.error = tr("找不到课程");
            return *out = p, true;
        }
        QStringList missing;
        resolveClasses(args.value(QStringLiteral("classNames")).toList(), &p.classIds, &missing);
        p.chainedTimes = qMax(0, args.value(QStringLiteral("times")).toInt());
        p.chainedLength = qMax(2, args.value(QStringLiteral("length")).toInt());
        if (p.classIds.isEmpty()) {
            p.error = tr("班级为空");
            return *out = p, true;
        }
        int updated = 0, noPlan = 0, tooFew = 0;
        for (const QVariant &cv : p.classIds) {
            bool found = false;
            for (const auto &e : m_school.planEntries) {
                if (e.classId != cv.toLongLong() || e.courseId != p.courseId)
                    continue;
                found = true;
                if (e.chainedCount != p.chainedTimes
                    || (p.chainedTimes > 0 && e.chainLength != p.chainedLength))
                    ++updated;
                if (p.chainedTimes > 0
                    && e.weeklyPeriods + 1e-6 < p.chainedTimes * p.chainedLength)
                    ++tooFew;
            }
            if (!found)
                ++noPlan;
        }
        p.changes = updated;
        p.summary = p.chainedTimes == 0
                        ? tr("取消「%1」在 %2 个班的连堂：%3 个班会变")
                              .arg(courseNameOf(p.courseId)).arg(p.classIds.size()).arg(updated)
                        : tr("把「%1」在 %2 个班设成每周 %3 次连堂、每次 %4 节：%5 个班会变")
                              .arg(courseNameOf(p.courseId)).arg(p.classIds.size())
                              .arg(p.chainedTimes).arg(p.chainedLength).arg(updated);
        if (noPlan > 0)
            p.warnings.append(tr("%1 个班的计划里没有这门课，将跳过").arg(noPlan));
        if (tooFew > 0)
            p.warnings.append(tr("%1 个班周节数不够 %2 节（次数 × 每段节数），建议先加大周节数")
                                  .arg(tooFew).arg(p.chainedTimes * p.chainedLength));
        p.valid = true;
        return *out = p, true;
    }

    if (tool == QLatin1StringView("set_plan_early_periods")) {
        p.title = tr("设置「前 N 节」偏好");
        p.courseId = matchEntity(m_school.courses, args.value(QStringLiteral("courseName")).toString());
        if (p.courseId < 0) {
            p.error = tr("找不到课程");
            return *out = p, true;
        }
        QStringList missing;
        resolveClasses(args.value(QStringLiteral("classNames")).toList(), &p.classIds, &missing);
        p.earlyPeriods = qMax(0, args.value(QStringLiteral("earlyPeriods")).toInt());
        if (p.classIds.isEmpty()) {
            p.error = tr("班级为空");
            return *out = p, true;
        }
        int updated = 0, noPlan = 0;
        for (const QVariant &cv : p.classIds) {
            bool found = false;
            for (const auto &e : m_school.planEntries) {
                if (e.classId != cv.toLongLong() || e.courseId != p.courseId)
                    continue;
                found = true;
                if (e.earlyPeriods != p.earlyPeriods)
                    ++updated;
            }
            if (!found)
                ++noPlan;
        }
        p.changes = updated;
        p.summary = p.earlyPeriods <= 0
                        ? tr("取消「%1」的前 N 节偏好：%2 个班会变").arg(courseNameOf(p.courseId)).arg(updated)
                        : tr("让「%1」在 %2 个班尽量排在每天前 %3 节：%4 个班会变（软约束）")
                              .arg(courseNameOf(p.courseId)).arg(p.classIds.size())
                              .arg(p.earlyPeriods).arg(updated);
        if (noPlan > 0)
            p.warnings.append(tr("%1 个班的计划里没有这门课，将跳过").arg(noPlan));
        p.valid = true;
        return *out = p, true;
    }

    if (tool == QLatin1StringView("set_plan_teachers")) {
        p.title = tr("设置任课老师");
        p.courseId = matchEntity(m_school.courses, args.value(QStringLiteral("courseName")).toString());
        if (p.courseId < 0) {
            p.error = tr("找不到课程");
            return *out = p, true;
        }
        QStringList missing;
        resolveClasses(args.value(QStringLiteral("classNames")).toList(), &p.classIds, &missing);
        QStringList missingTeachers;
        for (const QVariant &v : args.value(QStringLiteral("teacherNames")).toList()) {
            const QString name = v.toString().trimmed();
            if (name.isEmpty())
                continue;
            const qint64 tid = matchEntity(m_school.teachers, name);
            if (tid < 0)
                missingTeachers << name;
            else
                p.teacherIds.append(tid);
        }
        p.rotateTeachers = args.value(QStringLiteral("rotate")).toBool();
        if (p.classIds.isEmpty() || p.teacherIds.isEmpty()) {
            p.error = tr("班级或老师为空");
            return *out = p, true;
        }
        QStringList names;
        for (const QVariant &tv : p.teacherIds) {
            if (const sched::Teacher *t = m_school.findTeacher(tv.toLongLong()))
                names << t->name;
        }
        int updated = 0, noPlan = 0, index = 0;
        for (const QVariant &cv : p.classIds) {
            QVector<qint64> assign;
            if (p.rotateTeachers)
                assign.append(p.teacherIds.at(index % p.teacherIds.size()).toLongLong());
            else
                for (const QVariant &tv : p.teacherIds)
                    assign.append(tv.toLongLong());
            ++index;
            bool found = false;
            for (const auto &e : m_school.planEntries) {
                if (e.classId != cv.toLongLong() || e.courseId != p.courseId)
                    continue;
                found = true;
                if (e.teacherIds != assign)
                    ++updated;
            }
            if (!found)
                ++noPlan;
        }
        p.changes = updated;
        p.summary = p.rotateTeachers
                        ? tr("把「%1」的老师按班轮流分配（%2）：%3 个班会变")
                              .arg(courseNameOf(p.courseId), names.join(QStringLiteral("、")))
                              .arg(updated)
                        : tr("把「%1」在 %2 个班的老师设为 %3：%4 个班会变")
                              .arg(courseNameOf(p.courseId)).arg(p.classIds.size())
                              .arg(names.join(QStringLiteral("、"))).arg(updated);
        if (noPlan > 0)
            p.warnings.append(tr("%1 个班的计划里没有这门课，将跳过").arg(noPlan));
        if (!missingTeachers.isEmpty())
            p.warnings.append(tr("未找到老师：%1").arg(missingTeachers.join(QStringLiteral("、"))));
        p.valid = true;
        return *out = p, true;
    }

    if (tool == QLatin1StringView("set_teacher_daily_max")) {
        p.title = tr("设置老师日最多课");
        QStringList missing;
        for (const QVariant &v : args.value(QStringLiteral("teacherNames")).toList()) {
            const QString name = v.toString().trimmed();
            if (name.isEmpty())
                continue;
            const qint64 tid = matchEntity(m_school.teachers, name);
            if (tid < 0)
                missing << name;
            else
                p.entityIds.append(tid);
        }
        p.dailyMax = args.value(QStringLiteral("maxPerDay")).toInt();
        if (p.entityIds.isEmpty()) {
            p.error = tr("未找到老师，请先用 list_teachers 看看名字");
            return *out = p, true;
        }
        int updated = 0;
        for (const QVariant &iv : p.entityIds) {
            const sched::Teacher *t = m_school.findTeacher(iv.toLongLong());
            if (t && t->maxPeriodsPerDay != p.dailyMax)
                ++updated;
        }
        p.changes = updated;
        p.summary = tr("把 %1 位老师的「一天最多课」设为 %2：%3 位会变（软约束）")
                        .arg(p.entityIds.size())
                        .arg(p.dailyMax < 0 ? tr("不限") : QString::number(p.dailyMax))
                        .arg(updated);
        if (!missing.isEmpty())
            p.warnings.append(tr("未找到老师：%1").arg(missing.join(QStringLiteral("、"))));
        p.valid = true;
        return *out = p, true;
    }

    if (tool == QLatin1StringView("batch_no_schedule_by_group")) {
        p.title = tr("按组批量设置不排");
        const QString groupName = args.value(QStringLiteral("groupName")).toString();
        for (const auto &g : m_school.groups) {
            if (g.name == groupName || g.name.contains(groupName, Qt::CaseInsensitive)) {
                p.groupId = g.id;
                p.entityType = g.type == sched::Group::Type::Grade ? 0
                            : g.type == sched::Group::Type::TeacherGroup ? 2 : 1;
                break;
            }
        }
        if (p.groupId < 0) {
            p.error = tr("找不到组：%1（可先用 list_groups 看看）").arg(groupName);
            return *out = p, true;
        }
        resolveSlots(args.value(QStringLiteral("days")).toList(),
                     args.value(QStringLiteral("periods")).toList(), &p.slots);
        p.forbidden = !args.contains(QStringLiteral("forbidden"))
                   || args.value(QStringLiteral("forbidden")).toBool();
        if (p.slots.isEmpty()) {
            p.error = tr("时段为空（days 里给星期，periods 可选）");
            return *out = p, true;
        }
        const sched::Group *g = m_school.findGroup(p.groupId);
        int set = 0, cleared = 0;
        for (qint64 id : g->memberIds) {
            const sched::NoScheduleConstraint *c = nullptr;
            for (const auto &nc : m_school.noScheduleConstraints) {
                if (nc.entityType == static_cast<sched::NoScheduleConstraint::EntityType>(p.entityType)
                    && nc.entityId == id) {
                    c = &nc;
                    break;
                }
            }
            for (const QVariant &sv : p.slots) {
                const bool has = c && c->slots.contains(sv.toInt());
                if (p.forbidden && !has)
                    ++set;
                else if (!p.forbidden && has)
                    ++cleared;
            }
        }
        p.changes = p.forbidden ? set : cleared;
        p.summary = p.forbidden
                        ? tr("把「%1」的 %2 个成员在 %3 个时段设为不排：新增 %4 格")
                              .arg(g->name).arg(g->memberIds.size()).arg(p.slots.size()).arg(set)
                        : tr("恢复「%1」的 %2 个成员的不排：恢复 %3 格")
                              .arg(g->name).arg(g->memberIds.size()).arg(cleared);
        p.valid = true;
        return *out = p, true;
    }

    if (tool == QLatin1StringView("create_time_sync_group")) {
        p.title = tr("新建同时段组（走班）");
        p.groupId = -2; // 标记：新建
        const QString name = args.value(QStringLiteral("name")).toString().trimmed();
        int valid = 0;
        QStringList missing;
        for (const QVariant &v : args.value(QStringLiteral("entries")).toList()) {
            const QVariantMap m = v.toMap();
            const QString cn = m.value(QStringLiteral("className")).toString();
            const QString co = m.value(QStringLiteral("courseName")).toString();
            const qint64 cid = matchEntity(m_school.classes, cn);
            const qint64 coid = matchEntity(m_school.courses, co);
            if (cid < 0 || coid < 0) {
                missing << QStringLiteral("%1·%2").arg(cn, co);
                continue;
            }
            ++valid;
            p.syncEntries.append(QVariantMap{{QStringLiteral("classId"), cid},
                                             {QStringLiteral("courseId"), coid}});
            if (p.details.size() < 20) {
                const sched::ClassInfo *c = m_school.findClass(cid);
                p.details.append(tr("%1 的「%2」将与其它成员同节").arg(c ? c->name : cn, co));
            }
        }
        if (valid < 2) {
            p.error = tr("至少需要两个有效的「班级·课程」成员");
            return *out = p, true;
        }
        p.changes = valid;
        p.groupName = name;
        p.summary = tr("新建同时段组「%1」：%2 个成员必须排在同一节（硬约束，走班）")
                        .arg(name.isEmpty() ? tr("同时段组") : name)
                        .arg(valid);
        if (!missing.isEmpty())
            p.warnings.append(tr("以下成员未找到，将被忽略：%1").arg(missing.join(QStringLiteral("、"))));
        p.valid = true;
        return *out = p, true;
    }

    if (tool == QLatin1StringView("remove_time_sync_group")) {
        p.title = tr("删除同时段组");
        const QString name = args.value(QStringLiteral("name")).toString();
        for (const auto &g : m_school.timeSyncGroups) {
            if (g.name == name || g.name.contains(name, Qt::CaseInsensitive)) {
                p.groupId = g.id;
                break;
            }
        }
        if (p.groupId < 0) {
            p.error = tr("找不到同时段组：%1（可先用 list_time_sync_groups）").arg(name);
            return *out = p, true;
        }
        p.changes = 1;
        p.summary = tr("删除同时段组「%1」").arg(name);
        p.valid = true;
        return *out = p, true;
    }

    return false;
}

// ---------------------------------------------------------------- 应用
bool AppController::applyAiOperationExtra(const QString &tool, const AiOpPlan &p, QVariantMap *out)
{
    QVariantMap r;
    if (tool == QLatin1StringView("set_plan_chained")) {
        r = batchSetPlanChainedImpl(p.courseId, p.classIds, p.chainedTimes, p.chainedLength);
        r.insert(QStringLiteral("changed"), r.value(QStringLiteral("updated")).toInt() > 0);
    } else if (tool == QLatin1StringView("set_plan_early_periods")) {
        r = batchSetPlanEarlyPeriodsImpl(p.courseId, p.classIds, p.earlyPeriods);
        r.insert(QStringLiteral("changed"), r.value(QStringLiteral("updated")).toInt() > 0);
    } else if (tool == QLatin1StringView("set_plan_teachers")) {
        r = batchSetPlanTeachersImpl(p.courseId, p.classIds, p.teacherIds, p.rotateTeachers);
        r.insert(QStringLiteral("changed"), r.value(QStringLiteral("updated")).toInt() > 0);
    } else if (tool == QLatin1StringView("set_teacher_daily_max")) {
        r = batchSetTeacherDailyMaxImpl(p.entityIds, p.dailyMax);
        r.insert(QStringLiteral("changed"), r.value(QStringLiteral("updated")).toInt() > 0);
    } else if (tool == QLatin1StringView("create_time_sync_group")) {
        const qint64 id = createTimeSyncGroupByIds(p.groupName, p.syncEntries);
        r = QVariantMap{{QStringLiteral("created"), id >= 0 ? 1 : 0},
                        {QStringLiteral("changed"), id >= 0}};
        *out = r;
        return true;
    } else if (tool == QLatin1StringView("remove_time_sync_group")) {
        const bool ok = removeTimeSyncGroup(p.groupId);
        r = QVariantMap{{QStringLiteral("removed"), ok ? 1 : 0},
                        {QStringLiteral("changed"), ok}};
        *out = r;
        return true;
    } else if (tool == QLatin1StringView("batch_no_schedule_by_group")) {
        r = batchSetNoScheduleByGroup(p.groupId, p.slots, p.forbidden);
        r.insert(QStringLiteral("changed"),
                 r.value(QStringLiteral("set")).toInt() > 0
                     || r.value(QStringLiteral("cleared")).toInt() > 0);
    } else {
        return false;
    }
    *out = r;
    return true;
}

// ---------------------------------------------------------------- 批量实现（供界面与 AI 共用）
QVariantMap AppController::batchSetPlanChained(qint64 courseId, const QVariantList &classIds,
                                               int times, int length)
{
    if (!m_hasProject || courseId < 0 || classIds.isEmpty())
        return QVariantMap{{QStringLiteral("updated"), 0}};
    cancelScheduler();
    makeBatchBackup(tr("批量设置连堂"));
    const QVariantMap r = batchSetPlanChainedImpl(courseId, classIds, times, length);
    if (r.value(QStringLiteral("updated")).toInt() == 0)
        discardBatchBackup();
    return r;
}

QVariantMap AppController::batchSetPlanChainedImpl(qint64 courseId, const QVariantList &classIds,
                                                   int times, int length)
{
    QVariantMap out{{QStringLiteral("updated"), 0},
                    {QStringLiteral("missing"), 0},
                    {QStringLiteral("affected"), 0}};
    if (length < 2)
        length = 2;
    int updated = 0, missing = 0;
    QSet<qint64> affected;
    for (const QVariant &cv : classIds) {
        const qint64 cid = cv.toLongLong();
        if (!m_school.findClass(cid))
            continue;
        bool found = false;
        for (auto &e : m_school.planEntries) {
            if (e.classId != cid || e.courseId != courseId)
                continue;
            found = true;
            if (e.chainedCount != times || (times > 0 && e.chainLength != length)) {
                e.chainedCount = times;
                e.chainLength = length;
                ++updated;
                affected.insert(cid);
            }
        }
        if (!found)
            ++missing;
    }
    if (updated == 0)
        return out;
    out[QStringLiteral("updated")] = updated;
    out[QStringLiteral("missing")] = missing;
    out[QStringLiteral("affected")] = affected.size();
    markDirty();
    Q_EMIT projectChanged();
    return out;
}

QVariantMap AppController::batchSetPlanEarlyPeriods(qint64 courseId, const QVariantList &classIds,
                                                    int earlyPeriods)
{
    if (!m_hasProject || courseId < 0 || classIds.isEmpty())
        return QVariantMap{{QStringLiteral("updated"), 0}};
    cancelScheduler();
    makeBatchBackup(tr("批量设置前 N 节"));
    const QVariantMap r = batchSetPlanEarlyPeriodsImpl(courseId, classIds, earlyPeriods);
    if (r.value(QStringLiteral("updated")).toInt() == 0)
        discardBatchBackup();
    return r;
}

QVariantMap AppController::batchSetPlanEarlyPeriodsImpl(qint64 courseId, const QVariantList &classIds,
                                                        int earlyPeriods)
{
    QVariantMap out{{QStringLiteral("updated"), 0},
                    {QStringLiteral("missing"), 0},
                    {QStringLiteral("affected"), 0}};
    int updated = 0, missing = 0;
    QSet<qint64> affected;
    for (const QVariant &cv : classIds) {
        const qint64 cid = cv.toLongLong();
        if (!m_school.findClass(cid))
            continue;
        bool found = false;
        for (auto &e : m_school.planEntries) {
            if (e.classId != cid || e.courseId != courseId)
                continue;
            found = true;
            if (e.earlyPeriods != earlyPeriods) {
                e.earlyPeriods = earlyPeriods;
                ++updated;
                affected.insert(cid);
            }
        }
        if (!found)
            ++missing;
    }
    if (updated == 0)
        return out;
    out[QStringLiteral("updated")] = updated;
    out[QStringLiteral("missing")] = missing;
    out[QStringLiteral("affected")] = affected.size();
    markDirty();
    Q_EMIT projectChanged();
    return out;
}

QVariantMap AppController::batchSetPlanTeachers(qint64 courseId, const QVariantList &classIds,
                                                const QVariantList &teacherIds, bool rotate)
{
    if (!m_hasProject || courseId < 0 || classIds.isEmpty())
        return QVariantMap{{QStringLiteral("updated"), 0}};
    cancelScheduler();
    makeBatchBackup(tr("批量设置任课老师"));
    const QVariantMap r = batchSetPlanTeachersImpl(courseId, classIds, teacherIds, rotate);
    if (r.value(QStringLiteral("updated")).toInt() == 0)
        discardBatchBackup();
    return r;
}

QVariantMap AppController::batchSetPlanTeachersImpl(qint64 courseId, const QVariantList &classIds,
                                                    const QVariantList &teacherIds, bool rotate)
{
    QVariantMap out{{QStringLiteral("updated"), 0},
                    {QStringLiteral("missing"), 0},
                    {QStringLiteral("affected"), 0}};
    int updated = 0, missing = 0, index = 0;
    QSet<qint64> affected;
    for (const QVariant &cv : classIds) {
        const qint64 cid = cv.toLongLong();
        if (!m_school.findClass(cid))
            continue;
        QVector<qint64> assign;
        if (rotate && !teacherIds.isEmpty()) {
            assign.append(teacherIds.at(index % teacherIds.size()).toLongLong());
        } else {
            for (const QVariant &tv : teacherIds)
                assign.append(tv.toLongLong());
        }
        ++index;
        bool found = false;
        for (auto &e : m_school.planEntries) {
            if (e.classId != cid || e.courseId != courseId)
                continue;
            found = true;
            if (e.teacherIds != assign) {
                e.teacherIds = assign;
                ++updated;
                affected.insert(cid);
            }
        }
        if (!found)
            ++missing;
    }
    if (updated == 0)
        return out;
    out[QStringLiteral("updated")] = updated;
    out[QStringLiteral("missing")] = missing;
    out[QStringLiteral("affected")] = affected.size();
    markDirty();
    Q_EMIT projectChanged();
    return out;
}

QVariantMap AppController::batchSetTeacherDailyMax(const QVariantList &teacherIds, int maxPerDay)
{
    if (!m_hasProject || teacherIds.isEmpty())
        return QVariantMap{{QStringLiteral("updated"), 0}};
    cancelScheduler();
    makeBatchBackup(tr("批量设置老师日最多课"));
    const QVariantMap r = batchSetTeacherDailyMaxImpl(teacherIds, maxPerDay);
    if (r.value(QStringLiteral("updated")).toInt() == 0)
        discardBatchBackup();
    return r;
}

QVariantMap AppController::batchSetTeacherDailyMaxImpl(const QVariantList &teacherIds, int maxPerDay)
{
    QVariantMap out{{QStringLiteral("updated"), 0}, {QStringLiteral("missing"), 0}};
    int updated = 0, missing = 0;
    for (const QVariant &tv : teacherIds) {
        sched::Teacher *t = m_school.findTeacher(tv.toLongLong());
        if (!t) {
            ++missing;
            continue;
        }
        if (t->maxPeriodsPerDay != maxPerDay) {
            t->maxPeriodsPerDay = maxPerDay;
            ++updated;
        }
    }
    if (updated == 0)
        return out;
    out[QStringLiteral("updated")] = updated;
    out[QStringLiteral("missing")] = missing;
    markDirty();
    Q_EMIT projectChanged();
    return out;
}

QVariantMap AppController::batchSetNoScheduleByGroup(qint64 groupId, const QVariantList &slots,
                                                     bool forbidden)
{
    QVariantMap out{{QStringLiteral("set"), 0},
                    {QStringLiteral("cleared"), 0},
                    {QStringLiteral("affected"), 0}};
    const sched::Group *g = m_school.findGroup(groupId);
    if (!g || slots.isEmpty())
        return out;
    const int entityType = g->type == sched::Group::Type::Grade ? 0
                         : g->type == sched::Group::Type::TeacherGroup ? 2 : 1;
    cancelScheduler();
    makeBatchBackup(forbidden ? tr("按组批量不排") : tr("按组批量恢复不排"));
    QVariantList ids;
    for (qint64 id : g->memberIds)
        ids.append(id);
    const QVariantMap r = batchSetNoScheduleImpl(entityType, ids, slots, forbidden);
    if (r.value(QStringLiteral("set")).toInt() == 0 && r.value(QStringLiteral("cleared")).toInt() == 0)
        discardBatchBackup();
    out[QStringLiteral("entityType")] = entityType;
    out[QStringLiteral("members")] = ids.size();
    out[QStringLiteral("set")] = r.value(QStringLiteral("set"));
    out[QStringLiteral("cleared")] = r.value(QStringLiteral("cleared"));
    out[QStringLiteral("affected")] = r.value(QStringLiteral("affected"));
    return out;
}

// ---------------------------------------------------------------- 同时段组（走班）
QVariantList AppController::timeSyncGroups() const
{
    QVariantList out;
    for (const auto &g : m_school.timeSyncGroups) {
        QVariantList entries;
        for (const auto &e : g.entries) {
            const sched::ClassInfo *c = m_school.findClass(e.first);
            const sched::Course *co = m_school.findCourse(e.second);
            entries.append(QVariantMap{{QStringLiteral("className"), c ? c->name : QString()},
                                       {QStringLiteral("courseName"), co ? co->name : QString()}});
        }
        out.append(QVariantMap{{QStringLiteral("id"), g.id},
                               {QStringLiteral("name"), g.name},
                               {QStringLiteral("entries"), entries}});
    }
    return out;
}

qint64 AppController::createTimeSyncGroup(const QString &name, const QVariantList &entries)
{
    QVariantList ids;
    for (const QVariant &v : entries) {
        const QVariantMap m = v.toMap();
        const qint64 cid = matchEntity(m_school.classes, m.value(QStringLiteral("className")).toString());
        const qint64 coid = matchEntity(m_school.courses, m.value(QStringLiteral("courseName")).toString());
        if (cid >= 0 && coid >= 0)
            ids.append(QVariantMap{{QStringLiteral("classId"), cid},
                                   {QStringLiteral("courseId"), coid}});
    }
    return createTimeSyncGroupByIds(name, ids);
}

qint64 AppController::createTimeSyncGroupByIds(const QString &name, const QVariantList &idPairs)
{
    if (!m_hasProject)
        return -1;
    sched::TimeSyncGroup g;
    g.name = name.trimmed().isEmpty() ? tr("同时段组") : name.trimmed();
    for (const QVariant &v : idPairs) {
        const QVariantMap m = v.toMap();
        const qint64 cid = m.value(QStringLiteral("classId")).toLongLong();
        const qint64 coid = m.value(QStringLiteral("courseId")).toLongLong();
        if (cid >= 0 && coid >= 0)
            g.entries.append({cid, coid});
    }
    if (g.entries.size() < 2)
        return -1;
    cancelScheduler();
    makeBatchBackup(tr("新建同时段组"));
    const qint64 id = m_school.addTimeSyncGroup(g);
    markDirty();
    Q_EMIT projectChanged();
    return id;
}

bool AppController::removeTimeSyncGroup(qint64 id)
{
    if (id < 0)
        return false;
    cancelScheduler();
    makeBatchBackup(tr("删除同时段组"));
    if (!m_school.removeTimeSyncGroup(id)) {
        discardBatchBackup();
        return false;
    }
    markDirty();
    Q_EMIT projectChanged();
    return true;
}
