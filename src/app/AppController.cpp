#include "AppController.h"

#include "ExcelIo.h"
#include "SchedulerEngine.h"
#include "metrics/QualityMetrics.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <algorithm>

namespace {

constexpr int kMaxRecentFiles = 8;

} // namespace

AppController::AppController(QObject *parent)
    : QObject(parent)
{
    // 自动更新：启动后先查一次，之后每 5 分钟静默复查；发现新版本立即下载，
    // 只有「安装」需要用户确认（下载就绪后由界面提示）。
    // 单元测试用的是另一套 applicationName，跳过网络轮询保持测试确定性。
    if (QCoreApplication::applicationName() != QLatin1String("SmartScheduler"))
        return;
    QTimer::singleShot(3000, this, [this]() { checkUpdateSilently(); });
    auto *updateTimer = new QTimer(this);
    updateTimer->setInterval(5 * 60 * 1000);
    connect(updateTimer, &QTimer::timeout, this, [this]() { checkUpdateSilently(); });
    updateTimer->start();
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

namespace {

// 班级名 → 分组前缀："高一1班"→"高一"；"3班"→""（无前缀，不建组）
QString classGroupPrefix(const QString &className)
{
    bool hasDigit = false;
    int firstDigit = -1;
    for (int i = 0; i < className.size(); ++i) {
        if (className.at(i).isDigit()) {
            hasDigit = true;
            firstDigit = i;
            break;
        }
    }
    if (!hasDigit || firstDigit <= 0)
        return {}; // 没有数字或数字开头（如「3班」）不自动分组
    QString prefix = className.left(firstDigit).trimmed();
    while (prefix.endsWith(QStringLiteral("班")))
        prefix.chop(1);
    return prefix;
}

// 前缀 → 年级组名："高一"→"高一年级"；"一年级"→"一年级"
QString gradeGroupNameFor(const QString &prefix)
{
    if (prefix.endsWith(QStringLiteral("年级")) || prefix.endsWith(QStringLiteral("级")))
        return prefix;
    return prefix + QStringLiteral("年级");
}

} // namespace


// ---- AI 工具层所需的辅助（定义见文件后部）----
namespace {
// 名称匹配：先精确，再包含（大小写不敏感）；找不到返回 -1
template<typename T>
qint64 matchByName(const QVector<T> &list, const QString &name)
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

sched::NoScheduleConstraint *noScheduleFor(sched::School &school, int entityType,
                                           qint64 entityId);
} // namespace

// ---- 只读工具 ----
QVariantMap AppController::aiRunReadTool(const QString &tool, const QVariantMap &args) const
{
    const auto ok = [](const QVariant &data) {
        return QVariantMap{{QStringLiteral("ok"), true}, {QStringLiteral("data"), data}};
    };
    const auto err = [](const QString &msg) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), msg}};
    };
    const int ppd = m_school.timeConfig.periodsPerDay;
    const auto dayOf = [ppd](int slot) { return slot / ppd + 1; };
    const auto periodOf = [ppd](int slot) { return slot % ppd + 1; };
    const auto courseNameOf = [this](qint64 id) {
        const sched::Course *c = m_school.findCourse(id);
        return c ? c->name : QStringLiteral("?");
    };

    if (tool == QLatin1StringView("list_classes")) {
        QVariantList out;
        for (const auto &c : m_school.classes) {
            const sched::Group *g = c.gradeId >= 0 ? m_school.findGroup(c.gradeId) : nullptr;
            out.append(QVariantMap{{QStringLiteral("name"), c.name},
                                   {QStringLiteral("grade"), g ? g->name : QString()}});
        }
        return ok(out);
    }
    if (tool == QLatin1StringView("list_courses")) {
        QVariantList out;
        for (const auto &c : m_school.courses) {
            out.append(QVariantMap{{QStringLiteral("name"), c.name},
                                   {QStringLiteral("主课"), c.isMajor},
                                   {QStringLiteral("上下午"), static_cast<int>(c.amPmPolicy)}});
        }
        return ok(out);
    }
    if (tool == QLatin1StringView("list_teachers")) {
        QVariantList out;
        for (const auto &t : m_school.teachers) {
            int load = 0;
            for (const auto &e : m_school.planEntries) {
                if (e.teacherIds.contains(t.id) && !e.noLoad) // 不挂老师的格子不计课时
                    load += int(e.weeklyPeriods);
            }
            out.append(QVariantMap{{QStringLiteral("name"), t.name},
                                   {QStringLiteral("日最多课"), t.maxPeriodsPerDay},
                                   {QStringLiteral("周节数"), load}});
        }
        return ok(out);
    }
    if (tool == QLatin1StringView("list_groups")) {
        QVariantList out;
        for (const auto &g : m_school.groups) {
            QStringList members;
            for (qint64 id : g.memberIds) {
                if (const sched::ClassInfo *c = m_school.findClass(id))
                    members << c->name;
                else if (const sched::Teacher *t = m_school.findTeacher(id))
                    members << t->name;
                else if (const sched::Course *co = m_school.findCourse(id))
                    members << co->name;
            }
            out.append(QVariantMap{{QStringLiteral("type"), static_cast<int>(g.type)},
                                   {QStringLiteral("name"), g.name},
                                   {QStringLiteral("members"), members}});
        }
        return ok(out);
    }
    if (tool == QLatin1StringView("query_schedule")) {
        const qint64 cid =
            matchByName(m_school.classes, args.value(QStringLiteral("className")).toString());
        if (cid < 0)
            return err(tr("找不到班级：%1").arg(args.value(QStringLiteral("className")).toString()));
        const int day = args.value(QStringLiteral("day")).toInt(); // 0 = 整周
        QVariantList out;
        for (const auto &a : m_school.schedule.assignments) {
            if (a.classId != cid)
                continue;
            if (day > 0 && dayOf(a.slotIndex) != day)
                continue;
            out.append(QVariantMap{{QStringLiteral("星期"), dayOf(a.slotIndex)},
                                   {QStringLiteral("节次"), periodOf(a.slotIndex)},
                                   {QStringLiteral("课程"), courseNameOf(a.courseId)},
                                   {QStringLiteral("半周"), a.half}});
        }
        return ok(out);
    }
    if (tool == QLatin1StringView("query_plan")) {
        const qint64 cid =
            matchByName(m_school.classes, args.value(QStringLiteral("className")).toString());
        if (cid < 0)
            return err(tr("找不到班级"));
        QVariantList out;
        for (const auto &e : m_school.planEntries) {
            if (e.classId != cid)
                continue;
            QStringList teachers;
            for (qint64 t : e.teacherIds) {
                if (const sched::Teacher *tt = m_school.findTeacher(t))
                    teachers << tt->name;
            }
            out.append(QVariantMap{{QStringLiteral("课程"), courseNameOf(e.courseId)},
                                   {QStringLiteral("周节数"), e.weeklyPeriods},
                                   {QStringLiteral("连课次数"), e.chainedCount},
                                   {QStringLiteral("连课节数"), e.chainLength},
                                   {QStringLiteral("老师"), teachers.join(QStringLiteral("、"))}});
        }
        return ok(out);
    }
    if (tool == QLatin1StringView("query_preassign")) {
        const qint64 cid =
            matchByName(m_school.classes, args.value(QStringLiteral("className")).toString());
        if (cid < 0)
            return err(tr("找不到班级"));
        QVariantList out;
        for (const auto &p : m_school.preAssignments) {
            if (p.classId != cid)
                continue;
            out.append(QVariantMap{{QStringLiteral("星期"), dayOf(p.slotIndex)},
                                   {QStringLiteral("节次"), periodOf(p.slotIndex)},
                                   {QStringLiteral("课程"), courseNameOf(p.courseId)},
                                   {QStringLiteral("半周"), p.half}});
        }
        return ok(out);
    }
    if (tool == QLatin1StringView("query_no_schedule")) {
        const QString type = args.value(QStringLiteral("entityType")).toString();
        const int t = type == QLatin1StringView("class") ? 0
                    : type == QLatin1StringView("course") ? 1
                    : type == QLatin1StringView("teacher") ? 2 : 3;
        qint64 id = -1;
        const QString name = args.value(QStringLiteral("name")).toString();
        if (t == 0)
            id = matchByName(m_school.classes, name);
        else if (t == 1)
            id = matchByName(m_school.courses, name);
        else if (t == 2)
            id = matchByName(m_school.teachers, name);
        else
            id = matchByName(m_school.classrooms, name);
        if (id < 0)
            return err(tr("找不到对象：%1").arg(name));
        const sched::NoScheduleConstraint *c = nullptr;
        for (const auto &nc : m_school.noScheduleConstraints) {
            if (nc.entityType == static_cast<sched::NoScheduleConstraint::EntityType>(t)
                && nc.entityId == id) {
                c = &nc;
                break;
            }
        }
        QVariantList out;
        if (c) {
            QList<int> slots = c->slots.values();
            std::sort(slots.begin(), slots.end());
            for (int slot : slots)
                out.append(QVariantMap{{QStringLiteral("星期"), dayOf(slot)},
                                       {QStringLiteral("节次"), periodOf(slot)}});
        }
        return ok(out);
    }
    if (tool == QLatin1StringView("quality_report")) {
        const sched::QualityMetricsResult r = sched::computeQualityMetrics(m_school);
        QVariantList ind;
        for (const auto &i : r.indicators) {
            ind.append(QVariantMap{{QStringLiteral("指标"), i.name},
                                   {QStringLiteral("实测"), i.value},
                                   {QStringLiteral("应有"), i.required},
                                   {QStringLiteral("单位"), i.unit},
                                   {QStringLiteral("硬性"), i.hard}});
        }
        return ok(QVariantMap{{QStringLiteral("指标"), ind},
                              {QStringLiteral("老师五项平均"),
                               QVariantMap{{QStringLiteral("教案连续性"), r.avgContinuity},
                                           {QStringLiteral("教师开窗"), r.avgWindow},
                                           {QStringLiteral("周内均衡"), r.avgBalance},
                                           {QStringLiteral("先后公平"), r.avgFairness},
                                           {QStringLiteral("综合"), r.avgComposite}}}});
    }
    QVariantMap extra;
    if (aiRunReadToolExtra(tool, args, &extra))
        return extra;
    return err(tr("未知的只读工具：%1").arg(tool));
}


// ---- 变更工具：干跑（描述）与一次应用 ----
namespace {

// 一条 AI 变更操作的规范化计划定义在 AppController.h（干跑与应用共用）
QString slotLabel(const sched::TimeConfig &tc, int slot)
{
    static const char *kDays[] = {"周一", "周二", "周三", "周四", "周五", "周六", "周日"};
    const int day = tc.slotDay(slot);
    return QStringLiteral("%1 第%2节").arg(QString::fromUtf8(kDays[qBound(0, day, 6)]))
                                     .arg(tc.slotPeriod(slot) + 1);
}

} // namespace

AppController::AiOpPlan AppController::planAiOperation(const QString &tool,
                                                       const QVariantMap &args) const
{
    AiOpPlan p;
    const int ppd = m_school.timeConfig.periodsPerDay;
    const sched::TimeConfig &tc = m_school.timeConfig;

    const auto resolveClasses = [&](const QVariantList &names, QVariantList *ids, QStringList *missing) {
        for (const QVariant &v : names) {
            const QString name = v.toString().trimmed();
            if (name.isEmpty())
                continue;
            const qint64 id = matchByName(m_school.classes, name);
            if (id < 0)
                missing->append(name);
            else
                ids->append(id);
        }
    };
    const auto resolveSlots = [&](const QVariantList &days, int period, const QVariantList &periods,
                                  QVariantList *slots) {
        for (const QVariant &dv : days) {
            const int day = dv.toInt();
            if (day < 1 || day > tc.daysPerWeek)
                continue;
            if (!periods.isEmpty()) {
                for (const QVariant &pv : periods) {
                    const int per = pv.toInt();
                    if (per >= 1 && per <= ppd)
                        slots->append((day - 1) * ppd + per - 1);
                }
            } else if (period >= 1 && period <= ppd) {
                slots->append((day - 1) * ppd + period - 1);
            } else if (period <= 0) { // 未给节次 → 整天
                for (int per = 1; per <= ppd; ++per)
                    slots->append((day - 1) * ppd + per - 1);
            }
        }
    };

    if (tool == QLatin1StringView("batch_preassign")) {
        p.title = tr("批量固定预排");
        p.courseId = matchByName(m_school.courses, args.value(QStringLiteral("courseName")).toString());
        if (p.courseId < 0) {
            p.error = tr("找不到课程：%1").arg(args.value(QStringLiteral("courseName")).toString());
            return p;
        }
        QStringList missing;
        resolveClasses(args.value(QStringLiteral("classNames")).toList(), &p.classIds, &missing);
        resolveSlots(args.value(QStringLiteral("days")).toList(),
                     args.value(QStringLiteral("period")).toInt(), {}, &p.slots);
        p.half = args.value(QStringLiteral("half")).toInt();
        p.conflictMode = args.value(QStringLiteral("conflictMode")).toInt();
        if (p.classIds.isEmpty() || p.slots.isEmpty()) {
            p.error = tr("班级或时段为空");
            return p;
        }
        const sched::Course *course = m_school.findCourse(p.courseId);
        int fresh = 0, skipSame = 0, conflict = 0, notInPlan = 0;
        for (const QVariant &cv : p.classIds) {
            const qint64 cid = cv.toLongLong();
            bool inPlan = false;
            for (const auto &e : m_school.planEntries) {
                if (e.classId == cid && e.courseId == p.courseId) {
                    inPlan = true;
                    break;
                }
            }
            for (const QVariant &sv : p.slots) {
                const int slot = sv.toInt();
                if (!inPlan) {
                    ++notInPlan;
                    continue;
                }
                bool same = false, other = false;
                for (const auto &pre : m_school.preAssignments) {
                    if (pre.classId != cid || pre.slotIndex != slot)
                        continue;
                    if (pre.half == p.half && pre.courseId == p.courseId)
                        same = true;
                    else if (pre.half == 0 || p.half == 0)
                        other = true;
                }
                if (same) {
                    ++skipSame;
                    continue;
                }
                if (other && p.conflictMode != 1) {
                    ++conflict;
                    continue;
                }
                ++fresh;
                if (p.details.size() < 30) {
                    const sched::ClassInfo *c = m_school.findClass(cid);
                    p.details.append(tr("%1 %2 → 钉「%3」%4")
                                         .arg(c ? c->name : QStringLiteral("?"),
                                              slotLabel(tc, slot), course ? course->name : QString(),
                                              p.half == 1 ? tr("（单周）")
                                              : p.half == 2 ? tr("（双周）") : QString()));
                }
            }
        }
        p.changes = fresh;
        p.summary = tr("在 %1 个班 × %2 个时段设置「%3」预排：新增 %4 条，跳过 %5 条"
                       "（已存在 %6、与其他预排冲突 %7、班级无此课 %8）")
                        .arg(p.classIds.size()).arg(p.slots.size())
                        .arg(course ? course->name : QString()).arg(fresh).arg(skipSame + conflict + notInPlan)
                        .arg(skipSame).arg(conflict).arg(notInPlan);
        if (!missing.isEmpty())
            p.warnings.append(tr("有班名未找到：%1").arg(missing.join(QStringLiteral("、"))));
        if (notInPlan > 0)
            p.warnings.append(tr("有 %1 个格子的班级教学计划里没有这门课，将跳过（可先批量加课）")
                                  .arg(notInPlan));
        p.valid = true;
        return p;
    }

    if (tool == QLatin1StringView("batch_clear_preassign")) {
        p.title = tr("批量清除预排");
        QStringList missing;
        resolveClasses(args.value(QStringLiteral("classNames")).toList(), &p.classIds, &missing);
        resolveSlots(args.value(QStringLiteral("days")).toList(),
                     args.value(QStringLiteral("period")).toInt(), {}, &p.slots);
        const QString courseName = args.value(QStringLiteral("courseName")).toString();
        if (!courseName.isEmpty()) {
            p.courseId = matchByName(m_school.courses, courseName);
            if (p.courseId < 0) {
                p.error = tr("找不到课程：%1").arg(courseName);
                return p;
            }
        }
        if (p.classIds.isEmpty() || p.slots.isEmpty()) {
            p.error = tr("班级或时段为空");
            return p;
        }
        QSet<int> slotSet;
        for (const QVariant &v : p.slots)
            slotSet.insert(v.toInt());
        QSet<qint64> classSet;
        for (const QVariant &v : p.classIds)
            classSet.insert(v.toLongLong());
        for (const auto &pre : m_school.preAssignments) {
            if (!classSet.contains(pre.classId) || !slotSet.contains(pre.slotIndex))
                continue;
            if (p.courseId >= 0 && pre.courseId != p.courseId)
                continue;
            ++p.changes;
            if (p.details.size() < 30) {
                const sched::ClassInfo *c = m_school.findClass(pre.classId);
                const sched::Course *co = m_school.findCourse(pre.courseId);
                p.details.append(tr("%1 %2：移除「%3」%4")
                                     .arg(c ? c->name : QStringLiteral("?"), slotLabel(tc, pre.slotIndex),
                                          co ? co->name : QString(),
                                          pre.half == 1 ? tr("（单周）")
                                          : pre.half == 2 ? tr("（双周）") : QString()));
            }
        }
        p.summary = tr("将清除 %1 条预排").arg(p.changes);
        if (p.changes == 0)
            p.warnings.append(tr("这些时段本来就没有预排，无需清除"));
        p.valid = true;
        return p;
    }

    if (tool == QLatin1StringView("batch_no_schedule")) {
        p.title = tr("批量设置不排时间");
        const QString type = args.value(QStringLiteral("entityType")).toString();
        p.entityType = type == QLatin1StringView("class") ? 0
                     : type == QLatin1StringView("course") ? 1
                     : type == QLatin1StringView("teacher") ? 2 : 3;
        p.forbidden = args.value(QStringLiteral("forbidden")).toBool();
        if (args.contains(QStringLiteral("forbidden")) && !args.value(QStringLiteral("forbidden")).toBool())
            p.forbidden = false;
        QStringList missing;
        for (const QVariant &v : args.value(QStringLiteral("names")).toList()) {
            const QString name = v.toString().trimmed();
            if (name.isEmpty())
                continue;
            qint64 id = -1;
            if (p.entityType == 0)
                id = matchByName(m_school.classes, name);
            else if (p.entityType == 1)
                id = matchByName(m_school.courses, name);
            else if (p.entityType == 2)
                id = matchByName(m_school.teachers, name);
            else
                id = matchByName(m_school.classrooms, name);
            if (id < 0)
                missing.append(name);
            else
                p.entityIds.append(id);
        }
        resolveSlots(args.value(QStringLiteral("days")).toList(), 0,
                     args.value(QStringLiteral("periods")).toList(), &p.slots);
        if (p.entityIds.isEmpty() || p.slots.isEmpty()) {
            p.error = tr("对象或时段为空");
            return p;
        }
        int set = 0, cleared = 0;
        for (const QVariant &iv : p.entityIds) {
            const qint64 id = iv.toLongLong();
            const sched::NoScheduleConstraint *c = nullptr;
            for (const auto &nc : m_school.noScheduleConstraints) {
                if (nc.entityType
                        == static_cast<sched::NoScheduleConstraint::EntityType>(p.entityType)
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
                        ? tr("把 %1 个对象 × %2 个时段设为「不排」：新增 %3 格")
                              .arg(p.entityIds.size()).arg(p.slots.size()).arg(set)
                        : tr("恢复 %1 个对象 × %2 个时段的「不排」：恢复 %3 格")
                              .arg(p.entityIds.size()).arg(p.slots.size()).arg(cleared);
        if (p.details.size() < 30) {
            p.details.append(tr("时段：%1").arg(
                p.slots.size() > 6
                    ? tr("%1 个时段（如 %2）").arg(p.slots.size())
                          .arg(slotLabel(tc, p.slots.first().toInt()))
                    : [&] {
                          QStringList l;
                          for (const QVariant &v : p.slots)
                              l << slotLabel(tc, v.toInt());
                          return l.join(QStringLiteral("、"));
                      }()));
        }
        if (!missing.isEmpty())
            p.warnings.append(tr("未找到对象：%1").arg(missing.join(QStringLiteral("、"))));
        p.valid = true;
        return p;
    }

    if (tool == QLatin1StringView("batch_weekly_periods")) {
        p.title = tr("批量改周课时");
        p.courseId = matchByName(m_school.courses, args.value(QStringLiteral("courseName")).toString());
        if (p.courseId < 0) {
            p.error = tr("找不到课程");
            return p;
        }
        QStringList missing;
        resolveClasses(args.value(QStringLiteral("classNames")).toList(), &p.classIds, &missing);
        p.weeklyPeriods = args.value(QStringLiteral("weeklyPeriods")).toDouble();
        if (p.classIds.isEmpty() || p.weeklyPeriods < 0) {
            p.error = tr("班级或周节数不正确");
            return p;
        }
        int updated = 0, noPlan = 0;
        for (const QVariant &cv : p.classIds) {
            bool found = false;
            for (const auto &e : m_school.planEntries) {
                if (e.classId != cv.toLongLong() || e.courseId != p.courseId)
                    continue;
                found = true;
                if (!qFuzzyCompare(e.weeklyPeriods + 1.0, p.weeklyPeriods + 1.0))
                    ++updated;
            }
            if (!found)
                ++noPlan;
        }
        p.changes = updated;
        const sched::Course *course = m_school.findCourse(p.courseId);
        p.summary = tr("把「%1」在 %2 个班的周节数改为 %3 节：%4 个班会变，%5 个班没有这门课")
                        .arg(course ? course->name : QString())
                        .arg(p.classIds.size())
                        .arg(p.weeklyPeriods)
                        .arg(updated)
                        .arg(noPlan);
        if (noPlan > 0)
            p.warnings.append(tr("有 %1 个班的教学计划里没有这门课（如需可先加课）").arg(noPlan));
        p.valid = true;
        return p;
    }

    if (tool == QLatin1StringView("move_cell")) {
        p.title = tr("调课");
        p.moveClassId =
            matchByName(m_school.classes, args.value(QStringLiteral("className")).toString());
        if (p.moveClassId < 0) {
            p.error = tr("找不到班级");
            return p;
        }
        const int fromDay = args.value(QStringLiteral("fromDay")).toInt();
        const int fromPeriod = args.value(QStringLiteral("fromPeriod")).toInt();
        const int toDay = args.value(QStringLiteral("toDay")).toInt();
        const int toPeriod = args.value(QStringLiteral("toPeriod")).toInt();
        p.moveFromSlot = (fromDay - 1) * ppd + fromPeriod - 1;
        p.moveToSlot = (toDay - 1) * ppd + toPeriod - 1;
        if (!tc.isValidSlot(p.moveFromSlot) || !tc.isValidSlot(p.moveToSlot)) {
            p.error = tr("星期或节次超出范围");
            return p;
        }
        qint64 fromCourse = -1, toCourse = -1;
        for (const auto &a : m_school.schedule.assignments) {
            if (a.classId != p.moveClassId)
                continue;
            if (a.slotIndex == p.moveFromSlot)
                fromCourse = a.courseId;
            if (a.slotIndex == p.moveToSlot)
                toCourse = a.courseId;
        }
        const sched::ClassInfo *c = m_school.findClass(p.moveClassId);
        if (fromCourse < 0) {
            p.error = tr("%1 的 %2 本来就没有课").arg(c ? c->name : QString(),
                                                     slotLabel(tc, p.moveFromSlot));
            return p;
        }
        if (toCourse >= 0) {
            p.error = tr("目标格 %1 已有课（%2）；请先把它调走或指定空格")
                          .arg(slotLabel(tc, p.moveToSlot), m_school.findCourse(toCourse)->name);
            return p;
        }
        p.changes = 1;
        p.summary = tr("把 %1 的 %2 的「%3」挪到 %4")
                        .arg(c ? c->name : QString(), slotLabel(tc, p.moveFromSlot),
                             m_school.findCourse(fromCourse)->name, slotLabel(tc, p.moveToSlot));
        p.valid = true;
        return p;
    }

    if (planAiOperationExtra(tool, args, &p))
        return p;
    p.error = tr("未知的变更工具：%1").arg(tool);
    return p;
}

QVariantMap AppController::aiDescribe(const QString &tool, const QVariantMap &args) const
{
    const AiOpPlan p = planAiOperation(tool, args);
    return QVariantMap{{QStringLiteral("ok"), p.valid},
                       {QStringLiteral("error"), p.error},
                       {QStringLiteral("title"), p.title},
                       {QStringLiteral("summary"), p.summary},
                       {QStringLiteral("details"), p.details},
                       {QStringLiteral("warnings"), p.warnings},
                       {QStringLiteral("changes"), p.changes}};
}

QVariantMap AppController::applyAiOperations(const QVariantList &ops)
{
    QVariantMap out;
    if (!m_hasProject || ops.isEmpty()) {
        out.insert(QStringLiteral("applied"), 0);
        out.insert(QStringLiteral("changed"), false);
        return out;
    }
    cancelScheduler();
    makeBatchBackup(tr("AI 变更"));

    QVariantList results;
    bool changed = false;
    int applied = 0;
    for (const QVariant &v : ops) {
        const QVariantMap op = v.toMap();
        const QString tool = op.value(QStringLiteral("tool")).toString();
        const AiOpPlan p = planAiOperation(tool, op.value(QStringLiteral("args")).toMap());
        if (!p.valid) {
            results.append(QVariantMap{{QStringLiteral("tool"), tool},
                                       {QStringLiteral("ok"), false},
                                       {QStringLiteral("error"), p.error}});
            continue;
        }
        QVariantMap r;
        QVariantMap ext;
        if (tool == QLatin1StringView("batch_preassign")) {
            r = batchSetPreassignmentsImpl(p.courseId, p.classIds, p.slots, p.conflictMode, p.half);
            changed = changed || r.value(QStringLiteral("set")).toInt() > 0;
        } else if (tool == QLatin1StringView("batch_clear_preassign")) {
            r = batchClearPreassignmentsImpl(p.courseId, p.classIds, p.slots);
            changed = changed || r.value(QStringLiteral("cleared")).toInt() > 0;
        } else if (tool == QLatin1StringView("batch_no_schedule")) {
            r = batchSetNoScheduleImpl(p.entityType, p.entityIds, p.slots, p.forbidden);
            changed = changed || r.value(QStringLiteral("set")).toInt() > 0
                   || r.value(QStringLiteral("cleared")).toInt() > 0;
        } else if (tool == QLatin1StringView("batch_weekly_periods")) {
            r = batchSetWeeklyPeriodsImpl(p.courseId, p.classIds, p.weeklyPeriods);
            changed = changed || r.value(QStringLiteral("updated")).toInt() > 0;
        } else if (tool == QLatin1StringView("move_cell")) {
            qint64 course = -1;
            int half = 0;
            for (const auto &a : m_school.schedule.assignments) {
                if (a.classId == p.moveClassId && a.slotIndex == p.moveFromSlot) {
                    course = a.courseId;
                    half = a.half;
                    break;
                }
            }
            if (course >= 0) {
                m_school.schedule.removeAssignment(p.moveClassId, p.moveFromSlot, half);
                m_school.schedule.setAssignment(p.moveClassId, p.moveToSlot, half, course);
                markDirty();
                Q_EMIT scheduleChanged();
                changed = true;
                r = QVariantMap{{QStringLiteral("moved"), 1}};
            } else {
                r = QVariantMap{{QStringLiteral("moved"), 0}};
            }
        } else if (applyAiOperationExtra(tool, p, &ext)) {
            r = ext;
            changed = changed || r.value(QStringLiteral("changed")).toBool();
        } else {
            results.append(QVariantMap{{QStringLiteral("tool"), tool},
                                       {QStringLiteral("ok"), false},
                                       {QStringLiteral("error"), tr("未知工具")}});
            continue;
        }
        ++applied;
        results.append(QVariantMap{{QStringLiteral("tool"), tool},
                                   {QStringLiteral("ok"), true},
                                   {QStringLiteral("result"), r}});
    }
    if (!changed)
        discardBatchBackup();
    out.insert(QStringLiteral("applied"), applied);
    out.insert(QStringLiteral("changed"), changed);
    out.insert(QStringLiteral("results"), results);
    return out;
}

QVariantMap AppController::addClassesWithAutoGroup(const QStringList &names)
{
    QVariantMap out;
    if (!m_hasProject)
        return out;
    int created = 0;
    QSet<qint64> touched;
    for (const QString &raw : names) {
        const QString name = raw.trimmed();
        if (name.isEmpty())
            continue;
        sched::ClassInfo c;
        c.name = name;
        const QString prefix = classGroupPrefix(name);
        if (!prefix.isEmpty()) {
            const QString gname = gradeGroupNameFor(prefix);
            qint64 gid = -1;
            for (const auto &g : m_school.groups) {
                if (g.type == sched::Group::Type::Grade && g.name == gname) {
                    gid = g.id;
                    break;
                }
            }
            if (gid < 0) {
                sched::Group g;
                g.type = sched::Group::Type::Grade;
                g.name = gname;
                gid = m_school.addGroup(g);
            }
            c.gradeId = gid;
        }
        const qint64 cid = m_school.addClass(c);
        ++created;
        if (c.gradeId >= 0) {
            m_school.findGroup(c.gradeId)->memberIds.append(cid);
            touched.insert(c.gradeId);
        }
    }
    if (created == 0)
        return out;

    QVariantList groups;
    for (qint64 gid : touched) {
        const sched::Group *g = m_school.findGroup(gid);
        if (!g)
            continue;
        groups.append(QVariantMap{{QStringLiteral("name"), g->name},
                                  {QStringLiteral("memberCount"), g->memberIds.size()}});
    }
    out.insert(QStringLiteral("created"), created);
    out.insert(QStringLiteral("groups"), groups);
    markDirty();
    Q_EMIT projectChanged(); // 名单/分组模型全量重建
    return out;
}

// ==================== AI 编排：工具层 ====================
//
// 工具定义由客户端生成并随请求下发（中继只透传），所以这里加工具不需要动服务端。
// 只读工具立即执行；变更工具走 aiDescribe() 干跑 → 待办卡 → applyAiOperations() 应用。

namespace {

QVariantMap toolParam(const QString &type, const QString &desc)
{
    return {{QStringLiteral("type"), type}, {QStringLiteral("description"), desc}};
}

} // namespace

QVariantList AppController::aiTools() const
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
    const QVariantMap classNames = toolParam(QStringLiteral("array"),
                                             tr("班级名列表，如 [\"高一1班\",\"高一2班\"]；"
                                                "可用 list_classes 查询"));
    const QVariantMap courseName = toolParam(QStringLiteral("string"), tr("课程名，如 \"自习\""));
    const QVariantMap days = toolParam(QStringLiteral("array"),
                                       tr("星期列表，1=周一 … 7=周日"));
    const QVariantMap period = toolParam(QStringLiteral("integer"), tr("第几节，1 起"));
    const QVariantMap half = toolParam(QStringLiteral("integer"),
                                       tr("半周：0 整节（默认）、1 单周、2 双周；"
                                          "两门课各占半周即可共用同一格"));

    QVariantList out;
    // ---- 只读 ----
    out << fn(QStringLiteral("list_classes"), tr("列出全部班级"), {}, {});
    out << fn(QStringLiteral("list_courses"), tr("列出全部课程及其周节数设置"), {}, {});
    out << fn(QStringLiteral("list_teachers"), tr("列出全部老师及其每日最多课时"),
              {}, {});
    out << fn(QStringLiteral("list_groups"), tr("列出年级组与教研组及其成员"), {}, {});
    out << fn(QStringLiteral("query_schedule"),
              tr("查看某班课表（可选只看某一天），返回每格的课程"),
              {{QStringLiteral("className"), toolParam(QStringLiteral("string"), tr("班级名"))},
               {QStringLiteral("day"), toolParam(QStringLiteral("integer"), tr("星期 1-7，省略=整周"))}},
              {QStringLiteral("className")});
    out << fn(QStringLiteral("query_plan"), tr("查看某班教学计划（课程/周节数/连堂/老师）"),
              {{QStringLiteral("className"), toolParam(QStringLiteral("string"), tr("班级名"))}},
              {QStringLiteral("className")});
    out << fn(QStringLiteral("query_preassign"), tr("查看某班的固定预排"),
              {{QStringLiteral("className"), toolParam(QStringLiteral("string"), tr("班级名"))}},
              {QStringLiteral("className")});
    out << fn(QStringLiteral("query_no_schedule"), tr("查看某个对象的「不排时间」"),
              {{QStringLiteral("entityType"),
                toolParam(QStringLiteral("string"), tr("class/course/teacher/classroom"))},
               {QStringLiteral("name"), toolParam(QStringLiteral("string"), tr("对象名"))}},
              {QStringLiteral("entityType"), QStringLiteral("name")});
    out << fn(QStringLiteral("quality_report"),
              tr("读排课质量报告（十项指标 + 老师五项），可用于判断改完之后是否更好"), {}, {});
    // ---- 变更（挂起待批）----
    out << fn(QStringLiteral("batch_preassign"),
              tr("批量固定预排：给一批班级的指定时段钉上某门课（如全年级每天第 8 节自习）"),
              {{QStringLiteral("courseName"), courseName}, {QStringLiteral("classNames"), classNames},
               {QStringLiteral("days"), days}, {QStringLiteral("period"), period},
               {QStringLiteral("half"), half}},
              {QStringLiteral("courseName"), QStringLiteral("classNames"), QStringLiteral("days"),
               QStringLiteral("period")});
    out << fn(QStringLiteral("batch_clear_preassign"),
              tr("批量清除预排：把一批班级指定时段的预排去掉（可按课程）"),
              {{QStringLiteral("classNames"), classNames}, {QStringLiteral("days"), days},
               {QStringLiteral("period"), period}, {QStringLiteral("courseName"), courseName}},
              {QStringLiteral("classNames"), QStringLiteral("days"), QStringLiteral("period")});
    out << fn(QStringLiteral("batch_no_schedule"),
              tr("批量设置/恢复「不排时间」：对象可以是班级、课程、老师、教室"),
              {{QStringLiteral("entityType"),
                toolParam(QStringLiteral("string"), tr("class/course/teacher/classroom"))},
               {QStringLiteral("names"), toolParam(QStringLiteral("array"), tr("对象名列表"))},
               {QStringLiteral("days"), days},
               {QStringLiteral("periods"),
                toolParam(QStringLiteral("array"), tr("节次列表，1 起；省略=该天全部节次"))},
               {QStringLiteral("forbidden"),
                toolParam(QStringLiteral("boolean"), tr("true 禁排 / false 恢复，默认 true"))}},
              {QStringLiteral("entityType"), QStringLiteral("names"), QStringLiteral("days")});
    out << fn(QStringLiteral("batch_weekly_periods"),
              tr("批量改周课时：把某门课在多个班级的周节数一次改到位"),
              {{QStringLiteral("courseName"), courseName}, {QStringLiteral("classNames"), classNames},
               {QStringLiteral("weeklyPeriods"),
                toolParam(QStringLiteral("number"), tr("新的周节数"))}},
              {QStringLiteral("courseName"), QStringLiteral("classNames"),
               QStringLiteral("weeklyPeriods")});
    out += aiToolSchemasExtra();
    out << fn(QStringLiteral("move_cell"),
              tr("调课：把某班某格的课挪到另一格（整节）"),
              {{QStringLiteral("className"), toolParam(QStringLiteral("string"), tr("班级名"))},
               {QStringLiteral("fromDay"), toolParam(QStringLiteral("integer"), tr("原星期 1-7"))},
               {QStringLiteral("fromPeriod"), toolParam(QStringLiteral("integer"), tr("原节次"))},
               {QStringLiteral("toDay"), toolParam(QStringLiteral("integer"), tr("目标星期"))},
               {QStringLiteral("toPeriod"), toolParam(QStringLiteral("integer"), tr("目标节次"))}},
              {QStringLiteral("className"), QStringLiteral("fromDay"), QStringLiteral("fromPeriod"),
               QStringLiteral("toDay"), QStringLiteral("toPeriod")});
    return out;
}

// ==================== 批量操作（一次设置/清除 + 单级撤销）====================

namespace {
// 按 (实体类型, 实体 id) 取出不排约束（无则 nullptr）
sched::NoScheduleConstraint *noScheduleFor(sched::School &school, int entityType, qint64 entityId)
{
    const auto type = static_cast<sched::NoScheduleConstraint::EntityType>(entityType);
    for (auto &c : school.noScheduleConstraints) {
        if (c.entityType == type && c.entityId == entityId)
            return &c;
    }
    return nullptr;
}
} // namespace

void AppController::makeBatchBackup(const QString &label)
{
    m_batchBackup.preAssignments = m_school.preAssignments;
    m_batchBackup.noSchedule = m_school.noScheduleConstraints;
    m_batchBackup.planEntries = m_school.planEntries;
    m_batchBackup.schedule = m_school.schedule;
    m_batchBackup.timeSyncGroups = m_school.timeSyncGroups;
    m_batchBackup.combinedGroups = m_school.combinedGroups;
    m_batchBackup.label = label;
    m_hasBatchBackup = true;
}

void AppController::discardBatchBackup()
{
    m_batchBackup = BatchBackup();
    m_hasBatchBackup = false;
}

bool AppController::undoLastBatch()
{
    if (!m_hasBatchBackup)
        return false;
    cancelScheduler();
    m_school.preAssignments = m_batchBackup.preAssignments;
    m_school.noScheduleConstraints = m_batchBackup.noSchedule;
    m_school.planEntries = m_batchBackup.planEntries;
    m_school.schedule = m_batchBackup.schedule;
    m_school.timeSyncGroups = m_batchBackup.timeSyncGroups;
    m_school.combinedGroups = m_batchBackup.combinedGroups;
    discardBatchBackup();
    markDirty();
    Q_EMIT projectChanged();
    Q_EMIT constraintsChanged();
    Q_EMIT scheduleChanged();
    return true;
}

QVariantList AppController::groupMemberIds(qint64 groupId) const
{
    QVariantList out;
    const sched::Group *g = m_school.findGroup(groupId);
    if (!g)
        return out;
    for (qint64 id : g->memberIds)
        out.append(id);
    return out;
}

QVariantMap AppController::batchSetPreassignments(qint64 courseId, const QVariantList &classIds,
                                                  const QVariantList &slots, int conflictMode,
                                                  int half)
{
    QVariantMap out{{QStringLiteral("set"), 0},
                    {QStringLiteral("skipped"), 0},
                    {QStringLiteral("occupied"), 0},
                    {QStringLiteral("affected"), 0}};
    if (!m_hasProject || courseId < 0 || slots.isEmpty())
        return out;
    cancelScheduler();
    makeBatchBackup(tr("批量预排"));
    const QVariantMap r = batchSetPreassignmentsImpl(courseId, classIds, slots, conflictMode, half);
    if (r.value(QStringLiteral("set")).toInt() == 0)
        discardBatchBackup();
    return r;
}

QVariantMap AppController::batchSetPreassignmentsImpl(qint64 courseId, const QVariantList &classIds,
                                                      const QVariantList &slots, int conflictMode,
                                                      int half)
{
    QVariantMap out{{QStringLiteral("set"), 0},
                    {QStringLiteral("skipped"), 0},
                    {QStringLiteral("occupied"), 0},
                    {QStringLiteral("affected"), 0}};

    int set = 0, skipped = 0, occupied = 0;
    QSet<qint64> affected;
    for (const QVariant &cv : classIds) {
        const qint64 cid = cv.toLongLong();
        if (!m_school.findClass(cid))
            continue;
        bool inPlan = false;
        for (const auto &e : m_school.planEntries) {
            if (e.classId == cid && e.courseId == courseId) {
                inPlan = true;
                break;
            }
        }
        for (const QVariant &sv : slots) {
            const int slot = sv.toInt();
            if (!m_school.timeConfig.isValidSlot(slot))
                continue;
            if (!inPlan) {
                ++skipped;
                continue;
            }
            // 半周语义：同半周重复 → 跳过；单周与双周可共存（共用格）；整节与任意半周互斥
            bool sameParity = false, conflicting = false;
            for (const auto &p : m_school.preAssignments) {
                if (p.classId != cid || p.slotIndex != slot)
                    continue;
                if (p.half == half && p.courseId == courseId) {
                    sameParity = true;
                } else if (p.half == 0 || half == 0) {
                    conflicting = true;
                }
            }
            if (sameParity) {
                ++skipped;
                continue;
            }
            if (conflicting) {
                if (conflictMode != 1) { // 0=跳过
                    ++skipped;
                    continue;
                }
                for (int i = m_school.preAssignments.size() - 1; i >= 0; --i) {
                    const auto &p = m_school.preAssignments[i];
                    if (p.classId != cid || p.slotIndex != slot)
                        continue;
                    if (p.half == 0 || half == 0) // 只清互斥的那些，保留可共存的半周
                        m_school.removePreAssignment(p.id);
                }
            }
            sched::PreAssignment pre;
            pre.classId = cid;
            pre.courseId = courseId;
            pre.slotIndex = slot;
            pre.half = half;
            m_school.addPreAssignment(pre);
            ++set;
            affected.insert(cid);
            for (const auto &a : m_school.schedule.assignments) {
                if (a.classId == cid && a.slotIndex == slot) {
                    ++occupied; // 当前课表该格已有课：下次排课会被覆盖，给用户提示
                    break;
                }
            }
        }
    }
    out[QStringLiteral("skipped")] = skipped;
    out[QStringLiteral("occupied")] = occupied;
    out[QStringLiteral("affected")] = affected.size();
    if (set == 0)
        return out;
    out[QStringLiteral("set")] = set;
    markDirty();
    Q_EMIT constraintsChanged();
    return out;
}

QVariantMap AppController::batchClearPreassignments(qint64 courseId, const QVariantList &classIds,
                                                    const QVariantList &slots)
{
    QVariantMap out{{QStringLiteral("cleared"), 0}, {QStringLiteral("affected"), 0}};
    if (!m_hasProject || slots.isEmpty())
        return out;
    cancelScheduler();
    makeBatchBackup(tr("批量清除预排"));
    const QVariantMap r = batchClearPreassignmentsImpl(courseId, classIds, slots);
    if (r.value(QStringLiteral("cleared")).toInt() == 0)
        discardBatchBackup();
    return r;
}

QVariantMap AppController::batchClearPreassignmentsImpl(qint64 courseId, const QVariantList &classIds,
                                                        const QVariantList &slots)
{
    QVariantMap out{{QStringLiteral("cleared"), 0}, {QStringLiteral("affected"), 0}};

    QSet<qint64> classSet, slotSet;
    for (const QVariant &v : classIds)
        classSet.insert(v.toLongLong());
    for (const QVariant &v : slots)
        slotSet.insert(v.toInt());

    int cleared = 0;
    QSet<qint64> affected;
    for (int i = m_school.preAssignments.size() - 1; i >= 0; --i) {
        const auto &p = m_school.preAssignments[i];
        if (!classSet.isEmpty() && !classSet.contains(p.classId))
            continue;
        if (!slotSet.contains(p.slotIndex))
            continue;
        if (courseId >= 0 && p.courseId != courseId)
            continue;
        affected.insert(p.classId);
        m_school.removePreAssignment(p.id);
        ++cleared;
    }
    if (cleared == 0)
        return out;
    out[QStringLiteral("cleared")] = cleared;
    out[QStringLiteral("affected")] = affected.size();
    markDirty();
    Q_EMIT constraintsChanged();
    return out;
}

QVariantMap AppController::batchSetNoSchedule(int entityType, const QVariantList &ids,
                                              const QVariantList &slots, bool forbidden)
{
    QVariantMap out{{QStringLiteral("set"), 0},
                    {QStringLiteral("cleared"), 0},
                    {QStringLiteral("affected"), 0}};
    if (!m_hasProject || ids.isEmpty() || slots.isEmpty())
        return out;
    cancelScheduler();
    makeBatchBackup(forbidden ? tr("批量不排") : tr("批量恢复不排"));
    const QVariantMap r = batchSetNoScheduleImpl(entityType, ids, slots, forbidden);
    if (r.value(QStringLiteral("set")).toInt() == 0 && r.value(QStringLiteral("cleared")).toInt() == 0)
        discardBatchBackup();
    return r;
}

QVariantMap AppController::batchSetNoScheduleImpl(int entityType, const QVariantList &ids,
                                                  const QVariantList &slots, bool forbidden)
{
    QVariantMap out{{QStringLiteral("set"), 0},
                    {QStringLiteral("cleared"), 0},
                    {QStringLiteral("affected"), 0}};

    auto entityExists = [this, entityType](qint64 id) {
        using ET = sched::NoScheduleConstraint::EntityType;
        switch (static_cast<ET>(entityType)) {
        case ET::Class:
            return m_school.findClass(id) != nullptr;
        case ET::Course:
            return m_school.findCourse(id) != nullptr;
        case ET::Teacher:
            return m_school.findTeacher(id) != nullptr;
        case ET::Classroom:
            return m_school.findClassroom(id) != nullptr;
        }
        return false;
    };

    int set = 0, cleared = 0;
    QSet<qint64> affected;
    for (const QVariant &iv : ids) {
        const qint64 id = iv.toLongLong();
        if (!entityExists(id))
            continue;
        for (const QVariant &sv : slots) {
            const int slot = sv.toInt();
            if (!m_school.timeConfig.isValidSlot(slot))
                continue;
            sched::NoScheduleConstraint *c = noScheduleFor(m_school, entityType, id);
            if (forbidden) {
                if (!c) {
                    sched::NoScheduleConstraint nc;
                    nc.entityType = static_cast<sched::NoScheduleConstraint::EntityType>(entityType);
                    nc.entityId = id;
                    m_school.addNoScheduleConstraint(nc);
                    c = &m_school.noScheduleConstraints.last(); // add 追加在末尾
                }
                if (!c->slots.contains(slot)) {
                    c->slots.insert(slot);
                    ++set;
                    affected.insert(id);
                }
            } else {
                if (!c || !c->slots.contains(slot))
                    continue;
                c->slots.remove(slot);
                ++cleared;
                affected.insert(id);
                if (c->slots.isEmpty()) {
                    m_school.removeNoScheduleConstraint(c->id);
                    c = nullptr;
                }
            }
        }
    }
    if (set == 0 && cleared == 0)
        return out;
    out[QStringLiteral("set")] = set;
    out[QStringLiteral("cleared")] = cleared;
    out[QStringLiteral("affected")] = affected.size();
    markDirty();
    Q_EMIT constraintsChanged();
    return out;
}

QVariantMap AppController::batchSetWeeklyPeriods(qint64 courseId, const QVariantList &classIds,
                                                 double weeklyPeriods)
{
    QVariantMap out{{QStringLiteral("updated"), 0},
                    {QStringLiteral("missing"), 0},
                    {QStringLiteral("affected"), 0}};
    if (!m_hasProject || courseId < 0)
        return out;
    cancelScheduler();
    makeBatchBackup(tr("批量改课时"));
    const QVariantMap r = batchSetWeeklyPeriodsImpl(courseId, classIds, weeklyPeriods);
    if (r.value(QStringLiteral("updated")).toInt() == 0)
        discardBatchBackup();
    return r;
}

QVariantMap AppController::batchSetWeeklyPeriodsImpl(qint64 courseId, const QVariantList &classIds,
                                                     double weeklyPeriods)
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
            if (!qFuzzyCompare(e.weeklyPeriods + 1.0, weeklyPeriods + 1.0)) {
                e.weeklyPeriods = weeklyPeriods;
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

void AppController::removeRecentFile(const QString &path)
{
    QStringList files = recentFiles();
    if (!files.removeAll(path))
        return;
    QSettings().setValue(QStringLiteral("recentFiles"), files);
    Q_EMIT recentFilesChanged();
}

void AppController::clearRecentFiles()
{
    if (recentFiles().isEmpty())
        return;
    QSettings().setValue(QStringLiteral("recentFiles"), QStringList());
    Q_EMIT recentFilesChanged();
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

void AppController::updateProjectInfo(const QString &name, int daysPerWeek, int periodsPerDay,
                                      int morningPeriods, int weeksPerTerm)
{
    if (!m_hasProject)
        return;
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return;
    cancelScheduler();
    daysPerWeek = std::clamp(daysPerWeek, 1, 7);
    periodsPerDay = std::clamp(periodsPerDay, 1, 12);
    morningPeriods = std::clamp(morningPeriods, 0, periodsPerDay);
    weeksPerTerm = std::clamp(weeksPerTerm, 1, 30);

    const bool structureChanged = daysPerWeek != m_school.timeConfig.daysPerWeek
                                  || periodsPerDay != m_school.timeConfig.periodsPerDay;
    m_school.name = trimmed;
    m_school.timeConfig.daysPerWeek = daysPerWeek;
    m_school.timeConfig.periodsPerDay = periodsPerDay;
    m_school.timeConfig.morningPeriods = morningPeriods;
    m_school.timeConfig.weeksPerTerm = weeksPerTerm;

    if (structureChanged) {
        // 槽位索引含义改变：既有课表与按槽位记录的约束全部失效
        m_school.schedule = sched::Schedule();
        m_school.noScheduleConstraints.clear();
        m_school.preAssignments.clear();
        m_school.teacherActivities.clear();
        discardResult();
        Q_EMIT scheduleChanged();
        Q_EMIT constraintsChanged();
    }
    markDirty();
    Q_EMIT projectChanged();
}

void AppController::applyTemplate(int tpl)
{
    if (!m_hasProject || tpl <= 0 || tpl > 3)
        return;
    sched::School &s = m_school;

    auto addCourse = [&](const QString &name, bool major, bool science,
                         sched::AmPmPolicy ampm = sched::AmPmPolicy::Auto) {
        sched::Course c;
        c.name = name;
        c.isMajor = major;
        c.isScience = science;
        c.amPmPolicy = ampm;
        s.addCourse(c);
    };
    auto addRoom = [&](const QString &name, int cap) {
        sched::Classroom r;
        r.name = name;
        r.capacity = cap;
        s.addClassroom(r);
    };
    // 年级组 + 班级，并把班级自动勾选进年级组
    auto addGrade = [&](const QString &gradeName, int classCount) {
        sched::Group g;
        g.type = sched::Group::Type::Grade;
        g.name = gradeName;
        const qint64 gid = s.addGroup(g);
        for (int i = 1; i <= classCount; ++i) {
            sched::ClassInfo c;
            c.name = QStringLiteral("%1%2班").arg(gradeName).arg(i);
            c.gradeId = gid;
            s.findGroup(gid)->memberIds.append(s.addClass(c));
        }
    };

    if (tpl == 1) { // 小学
        addCourse(QStringLiteral("语文"), true, false, sched::AmPmPolicy::Morning);
        addCourse(QStringLiteral("数学"), true, true, sched::AmPmPolicy::Morning);
        addCourse(QStringLiteral("英语"), true, false, sched::AmPmPolicy::Morning);
        addCourse(QStringLiteral("道德与法治"), false, false);
        addCourse(QStringLiteral("科学"), false, true);
        addCourse(QStringLiteral("体育"), false, false, sched::AmPmPolicy::Afternoon);
        addCourse(QStringLiteral("音乐"), false, false);
        addCourse(QStringLiteral("美术"), false, false);
        addCourse(QStringLiteral("信息技术"), false, true);
        addCourse(QStringLiteral("劳动"), false, false);
        const QStringList grades = {QStringLiteral("一年级"), QStringLiteral("二年级"),
                                    QStringLiteral("三年级"), QStringLiteral("四年级"),
                                    QStringLiteral("五年级"), QStringLiteral("六年级")};
        for (const auto &g : grades)
            addGrade(g, 3);
        addRoom(QStringLiteral("操场"), 8);
        addRoom(QStringLiteral("音乐室"), 2);
        addRoom(QStringLiteral("美术室"), 2);
        addRoom(QStringLiteral("计算机房"), 2);
        addRoom(QStringLiteral("科学实验室"), 2);
    } else if (tpl == 2) { // 初中
        addCourse(QStringLiteral("语文"), true, false, sched::AmPmPolicy::Morning);
        addCourse(QStringLiteral("数学"), true, true, sched::AmPmPolicy::Morning);
        addCourse(QStringLiteral("英语"), true, false, sched::AmPmPolicy::Morning);
        addCourse(QStringLiteral("物理"), true, true);
        addCourse(QStringLiteral("化学"), false, true);
        addCourse(QStringLiteral("生物"), false, true);
        addCourse(QStringLiteral("历史"), false, false);
        addCourse(QStringLiteral("地理"), false, false);
        addCourse(QStringLiteral("道德与法治"), false, false);
        addCourse(QStringLiteral("体育"), false, false, sched::AmPmPolicy::Afternoon);
        addCourse(QStringLiteral("音乐"), false, false);
        addCourse(QStringLiteral("美术"), false, false);
        addCourse(QStringLiteral("信息技术"), false, true);
        const QStringList grades = {QStringLiteral("初一"), QStringLiteral("初二"),
                                    QStringLiteral("初三")};
        for (const auto &g : grades)
            addGrade(g, 4);
        addRoom(QStringLiteral("操场"), 8);
        addRoom(QStringLiteral("计算机房"), 3);
        addRoom(QStringLiteral("物理实验室"), 2);
        addRoom(QStringLiteral("化学实验室"), 2);
        addRoom(QStringLiteral("生物实验室"), 2);
        addRoom(QStringLiteral("音乐室"), 2);
        addRoom(QStringLiteral("美术室"), 2);
    } else { // 高中
        addCourse(QStringLiteral("语文"), true, false, sched::AmPmPolicy::Morning);
        addCourse(QStringLiteral("数学"), true, true, sched::AmPmPolicy::Morning);
        addCourse(QStringLiteral("英语"), true, false, sched::AmPmPolicy::Morning);
        addCourse(QStringLiteral("物理"), true, true);
        addCourse(QStringLiteral("化学"), true, true);
        addCourse(QStringLiteral("生物"), false, true);
        addCourse(QStringLiteral("历史"), false, false);
        addCourse(QStringLiteral("地理"), false, false);
        addCourse(QStringLiteral("思想政治"), false, false);
        addCourse(QStringLiteral("体育"), false, false, sched::AmPmPolicy::Afternoon);
        addCourse(QStringLiteral("信息技术"), false, true);
        addCourse(QStringLiteral("通用技术"), false, false);
        const QStringList grades = {QStringLiteral("高一"), QStringLiteral("高二"),
                                    QStringLiteral("高三")};
        for (const auto &g : grades)
            addGrade(g, 4);
        addRoom(QStringLiteral("操场"), 8);
        addRoom(QStringLiteral("计算机房"), 3);
        addRoom(QStringLiteral("物理实验室"), 2);
        addRoom(QStringLiteral("化学实验室"), 2);
        addRoom(QStringLiteral("生物实验室"), 2);
    }

    markDirty();
    // 先发 projectChanged 让模型一致复位（与 openProject 相同的信号顺序约定）
    Q_EMIT projectChanged();
    Q_EMIT constraintsChanged();
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
    m_resultPending = false;
    // 先发 projectChanged 让所有模型同步复位，再发其它信号 —— QML 对任一信号的
    // 同步响应（如列表重建）都会读到一致的模型视图，避免 m_view 指向旧数据崩溃
    Q_EMIT projectChanged();
    Q_EMIT resultPendingChanged();
    addRecentFile(path);
    // 打印设置随项目载入
    const QVariant saved = m_store.setting(QStringLiteral("print"));
    if (saved.isValid() && !saved.isNull())
        m_printSettings = saved.toMap();
    Q_EMIT printSettingsChanged();
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

// 某班某槽位是否被班级级不排约束禁止
bool classSlotForbidden(const sched::School &school, qint64 classId, int slot)
{
    for (const auto &c : school.noScheduleConstraints) {
        if (c.entityType == sched::NoScheduleConstraint::EntityType::Class && c.entityId == classId
            && c.slots.contains(slot))
            return true;
    }
    return false;
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

QVariantList AppController::gradeGroups() const
{
    QVariantList out;
    for (const auto &g : m_school.groups) {
        if (g.type != sched::Group::Type::Grade)
            continue;
        out.append(QVariantMap{{QStringLiteral("id"), g.id},
                               {QStringLiteral("name"), g.name},
                               {QStringLiteral("memberCount"), g.memberIds.size()},
                               {QStringLiteral("maskDays"), g.maskDays},
                               {QStringLiteral("maskPeriods"), g.maskPeriods}});
    }
    return out;
}

void AppController::applyGradeMask(qint64 groupId, int activeDays, int activePeriods, bool forbidden)
{
    sched::Group *g = m_school.findGroup(groupId);
    if (!g || g->type != sched::Group::Type::Grade)
        return;
    const int ppd = m_school.timeConfig.periodsPerDay;
    const int total = m_school.timeConfig.slotCount();
    activeDays = std::clamp(activeDays, 1, m_school.timeConfig.daysPerWeek);
    activePeriods = std::clamp(activePeriods, 1, ppd);
    const int oldDays = g->maskDays;
    const int oldPeriods = g->maskPeriods;

    auto outside = [ppd](int slot, int days, int periods) {
        return slot / ppd >= days || slot % ppd >= periods;
    };
    auto applyRange = [&](int days, int periods, bool block) {
        bool touched = false;
        for (qint64 cid : g->memberIds) {
            if (!m_school.findClass(cid))
                continue;
            for (int slot = 0; slot < total; ++slot) {
                if (!outside(slot, days, periods))
                    continue;
                sched::NoScheduleConstraint *c = findNoSchedule(
                    m_school, static_cast<int>(sched::NoScheduleConstraint::EntityType::Class), cid);
                if (block) {
                    if (!c) {
                        sched::NoScheduleConstraint nc;
                        nc.entityType = sched::NoScheduleConstraint::EntityType::Class;
                        nc.entityId = cid;
                        m_school.addNoScheduleConstraint(nc);
                        c = &m_school.noScheduleConstraints.last(); // add 追加在末尾
                        touched = true;
                    }
                    if (!c->slots.contains(slot)) {
                        c->slots.insert(slot);
                        touched = true;
                    }
                } else {
                    if (!c || !c->slots.contains(slot))
                        continue;
                    c->slots.remove(slot);
                    touched = true;
                    if (c->slots.isEmpty()) {
                        m_school.removeNoScheduleConstraint(c->id);
                        c = nullptr;
                    }
                }
            }
        }
        return touched;
    };

    bool changed = false;
    // 先撤销上一次遮罩，保证「应用」与「清除」都精确落在遮罩范围上
    if (oldDays > 0 && oldPeriods > 0)
        changed |= applyRange(oldDays, oldPeriods, false);
    if (forbidden) {
        changed |= applyRange(activeDays, activePeriods, true);
        g->maskDays = activeDays;
        g->maskPeriods = activePeriods;
        changed = true; // 遮罩状态本身也是数据
    } else {
        if (oldDays > 0) {
            g->maskDays = -1;
            g->maskPeriods = -1;
            changed = true;
        }
    }
    if (!changed)
        return;
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
                               {QStringLiteral("slotIndex"), p.slotIndex},
                               {QStringLiteral("half"), p.half}});
    }
    return out;
}

void AppController::togglePreAssignment(qint64 classId, qint64 courseId, int slot, int half)
{
    if (courseId < 0) { // 清除该格全部预排（含单/双周）
        bool removed = false;
        for (int i = m_school.preAssignments.size() - 1; i >= 0; --i) {
            const auto &p = m_school.preAssignments[i];
            if (p.classId == classId && p.slotIndex == slot) {
                m_school.removePreAssignment(p.id);
                removed = true;
            }
        }
        if (removed) {
            markDirty();
            Q_EMIT constraintsChanged();
        }
        return;
    }
    // 同班同 slot 同半周只允许一条预排：命中则取消；不同 half 允许并存（单双周共用一格）
    for (const auto &p : m_school.preAssignments) {
        if (p.classId == classId && p.slotIndex == slot && p.half == half) {
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
    p.half = half;
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
        bool hasCourse = false;
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
            hasCourse = true;
            break; // 整节优先；单双周同格只取第一条
        }
        // 单双周同格第二条
        QString secondText;
        for (const auto &a : m_school.schedule.assignments) {
            if (a.classId == classId && a.slotIndex == slot && a.half != 0) {
                const sched::Course *co = m_school.findCourse(a.courseId);
                const QString t = co ? co->name : QStringLiteral("?");
                if (!hasCourse) {
                    cell = {{QStringLiteral("text"), t},
                            {QStringLiteral("sub"), QString()},
                            {QStringLiteral("courseId"), a.courseId},
                            {QStringLiteral("half"), a.half},
                            {QStringLiteral("major"), co && co->isMajor},
                            {QStringLiteral("pre"), false}};
                    hasCourse = true;
                } else if (cell.value(QStringLiteral("half")).toInt() != 0
                           && cell.value(QStringLiteral("courseId")).toLongLong() != a.courseId) {
                    secondText = t + (a.half == 1 ? QStringLiteral("单") : QStringLiteral("双"));
                }
            }
        }
        if (!secondText.isEmpty() && hasCourse)
            cell.insert(QStringLiteral("sub2"), secondText);
        if (classSlotForbidden(m_school, classId, slot))
            cell.insert(QStringLiteral("blocked"), true);
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
                if (e.noLoad)
                    break; // 不挂老师：老师个人课表上不显示这一格（发布前要摘掉）
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
            if (classSlotForbidden(m_school, c.id, slot))
                cell.insert(QStringLiteral("blocked"), true);
            cells[slot] = cell;
        }
        rows.append(QVariantMap{{QStringLiteral("classId"), c.id},
                                {QStringLiteral("className"), c.name},
                                {QStringLiteral("cells"), cells}});
    }
    return rows;
}

// ==================== 质量报告（十项指标 + 老师五项）====================

namespace {

QVariantList indicatorsToVariant(const QVector<sched::QualityIndicator> &list)
{
    QVariantList out;
    for (const auto &i : list) {
        out.append(QVariantMap{{QStringLiteral("key"), i.key},
                               {QStringLiteral("name"), i.name},
                               {QStringLiteral("hard"), i.hard},
                               {QStringLiteral("higherBetter"), i.higherBetter},
                               {QStringLiteral("value"), i.value},
                               {QStringLiteral("required"), i.required},
                               {QStringLiteral("unit"), i.unit},
                               {QStringLiteral("isRatio"), i.isRatio}});
    }
    return out;
}

QVariantList teachersToVariant(const QVector<sched::TeacherQuality> &list)
{
    QVariantList out;
    for (const auto &t : list) {
        out.append(QVariantMap{{QStringLiteral("id"), t.teacherId},
                               {QStringLiteral("name"), t.name},
                               {QStringLiteral("periods"), t.weeklyPeriods},
                               {QStringLiteral("continuity"), t.continuity},
                               {QStringLiteral("window"), t.window},
                               {QStringLiteral("balance"), t.balance},
                               {QStringLiteral("fairness"), t.fairness},
                               {QStringLiteral("composite"), t.composite}});
    }
    return out;
}

QVariantMap averagesToVariant(const sched::QualityMetricsResult &r)
{
    return {{QStringLiteral("continuity"), r.avgContinuity},
            {QStringLiteral("window"), r.avgWindow},
            {QStringLiteral("balance"), r.avgBalance},
            {QStringLiteral("fairness"), r.avgFairness},
            {QStringLiteral("composite"), r.avgComposite}};
}

// 指标格式化：有分母的显示 "值 / 分母"，否则显示数值
QString formatIndicator(const sched::QualityIndicator &i)
{
    auto num = [](double v) {
        if (qFuzzyIsNull(v - qRound(v)))
            return QString::number(qRound(v));
        return QString::number(v, 'f', 2);
    };
    if (i.isRatio && i.required > 0)
        return QStringLiteral("%1 / %2").arg(num(i.value), num(i.required));
    return num(i.value);
}

QString indicatorBadge(const sched::QualityIndicator &i)
{
    return i.hard ? QStringLiteral("<span class=\"badge hard\">硬</span>")
                  : QStringLiteral("<span class=\"badge soft\">软</span>");
}

QString indicatorDirection(const sched::QualityIndicator &i)
{
    return i.higherBetter ? QStringLiteral("↑ 越高越好") : QStringLiteral("↓ 越低越好");
}

} // namespace

QVariantMap AppController::qualityReport() const
{
    QVariantMap out;
    const sched::QualityMetricsResult overall = sched::computeQualityMetrics(m_school);
    out.insert(QStringLiteral("indicators"), indicatorsToVariant(overall.indicators));
    out.insert(QStringLiteral("teachers"), teachersToVariant(overall.teachers));
    out.insert(QStringLiteral("averages"), averagesToVariant(overall));
    out.insert(QStringLiteral("teacherCount"), overall.teachers.size());
    out.insert(QStringLiteral("hasSchedule"), hasSchedule());
    int hardViolations = 0;
    for (const auto &i : overall.indicators) {
        if (!i.hard)
            continue;
        hardViolations += i.isRatio ? std::max(0, qRound(i.required - i.value)) : qRound(i.value);
    }
    out.insert(QStringLiteral("hardViolations"), hardViolations);

    QVariantList grades;
    for (const auto &g : m_school.groups) {
        if (g.type != sched::Group::Type::Grade)
            continue;
        QVector<qint64> ids;
        for (qint64 cid : g.memberIds) {
            if (m_school.findClass(cid))
                ids.append(cid);
        }
        if (ids.isEmpty())
            continue;
        const sched::QualityMetricsResult r = sched::computeQualityMetrics(m_school, ids);
        grades.append(QVariantMap{{QStringLiteral("id"), g.id},
                                  {QStringLiteral("name"), g.name},
                                  {QStringLiteral("classCount"), ids.size()},
                                  {QStringLiteral("indicators"), indicatorsToVariant(r.indicators)},
                                  {QStringLiteral("averages"), averagesToVariant(r)}});
    }
    out.insert(QStringLiteral("grades"), grades);
    return out;
}

QString AppController::buildQualityReportHtml(const sched::School &school)
{
    const sched::QualityMetricsResult overall = sched::computeQualityMetrics(school);
    QString h;
    h.reserve(64 * 1024);
    h += QStringLiteral(
        "<!DOCTYPE html>\n<html lang=\"zh-CN\"><head><meta charset=\"utf-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        "<title>%1 排课质量报告</title>\n<style>\n"
        ":root{--brand:#2563eb;--hard:#dc2626;--soft:#b45309;--ok:#15803d;--line:#e2e8f0;"
        "--bg:#f8fafc;--ink:#0f172a;--muted:#64748b;}\n"
        "*{box-sizing:border-box}\n"
        "body{margin:0;padding:32px 28px 64px;font:15px/1.65 \"Microsoft YaHei\",\"PingFang SC\","
        "system-ui,sans-serif;color:var(--ink);background:#fff;max-width:1100px;margin:0 auto}\n"
        "h1{font-size:26px;margin:0 0 6px}\n"
        "h2{font-size:19px;margin:34px 0 12px;padding-bottom:6px;border-bottom:2px solid var(--line)}\n"
        "h3{font-size:16px;margin:22px 0 10px}\n"
        ".sub{color:var(--muted);font-size:13px;margin:0 0 4px}\n"
        ".cards{display:flex;flex-wrap:wrap;gap:12px;margin:18px 0}\n"
        ".card{flex:1 1 200px;border:1px solid var(--line);border-radius:10px;padding:12px 14px;"
        "background:var(--bg);break-inside:avoid}\n"
        ".card .k{font-size:12px;color:var(--muted)}\n"
        ".card .v{font-size:22px;font-weight:700;margin-top:2px}\n"
        ".good{color:var(--ok)}.bad{color:var(--hard)}\n"
        "table{width:100%;border-collapse:collapse;margin:10px 0 18px;font-size:14px}\n"
        "th,td{border:1px solid var(--line);padding:7px 10px;text-align:left;vertical-align:top}\n"
        "th{background:var(--bg);font-weight:600;position:sticky;top:0}\n"
        "td.num{text-align:right;font-variant-numeric:tabular-nums}\n"
        ".badge{display:inline-block;border-radius:999px;padding:1px 8px;font-size:12px;"
        "border:1px solid currentColor}\n"
        ".badge.hard{color:var(--hard)}.badge.soft{color:var(--soft)}\n"
        ".dir{color:var(--muted);font-size:12px}\n"
        ".note{border-left:4px solid var(--brand);background:var(--bg);padding:10px 14px;"
        "border-radius:0 8px 8px 0;break-inside:avoid}\n"
        ".formula{font-family:Consolas,Menlo,monospace;background:var(--bg);border:1px solid "
        "var(--line);border-radius:6px;padding:6px 10px;display:inline-block;margin:4px 0}\n"
        "footer{margin-top:36px;color:var(--muted);font-size:12px}\n"
        "@media print{@page{size:A4 landscape;margin:12mm}\n"
        " body{padding:0;max-width:none;font-size:12px;-webkit-print-color-adjust:exact;"
        "print-color-adjust:exact}\n"
        " h2{break-after:avoid} table{break-inside:auto} tr{break-inside:avoid}\n"
        " th{position:static} .cards{gap:8px} .card{flex:1 1 160px}}\n"
        "</style></head><body>\n")
             .arg(school.name.toHtmlEscaped());

    h += QStringLiteral("<h1>%1 · 排课质量报告</h1>\n")
             .arg(school.name.toHtmlEscaped());
    h += QStringLiteral("<p class=\"sub\">作息：每周 %1 天 × 每天 %2 节（上午 %3 节） · 学期 %4 周 · "
                        "自动排课结果质量评估</p>\n")
             .arg(school.timeConfig.daysPerWeek)
             .arg(school.timeConfig.periodsPerDay)
             .arg(school.timeConfig.morningPeriods)
             .arg(school.timeConfig.weeksPerTerm);

    // 摘要
    int clash = 0, courseForbid = 0, teacherForbid = 0;
    QString placedText;
    for (const auto &i : overall.indicators) {
        if (i.key == QLatin1StringView("teacherClash"))
            clash = qRound(i.value);
        else if (i.key == QLatin1StringView("courseForbiddenSlot"))
            courseForbid = qRound(i.value);
        else if (i.key == QLatin1StringView("teacherForbiddenSlot"))
            teacherForbid = qRound(i.value);
        else if (i.key == QLatin1StringView("placedPeriods"))
            placedText = formatIndicator(i);
    }
    h += QStringLiteral("<div class=\"cards\">\n");
    h += QStringLiteral("<div class=\"card\"><div class=\"k\">排上的节数</div><div class=\"v\">%1</div></div>\n")
             .arg(placedText.isEmpty() ? QStringLiteral("-") : placedText);
    h += QStringLiteral("<div class=\"card\"><div class=\"k\">老师撞课</div><div class=\"v %1\">%2 处</div></div>\n")
             .arg(clash == 0 ? QStringLiteral("good") : QStringLiteral("bad"))
             .arg(clash);
    h += QStringLiteral("<div class=\"card\"><div class=\"k\">学科排进不排时段</div><div class=\"v %1\">%2 节</div></div>\n")
             .arg(courseForbid == 0 ? QStringLiteral("good") : QStringLiteral("bad"))
             .arg(courseForbid);
    h += QStringLiteral("<div class=\"card\"><div class=\"k\">老师排进不排时段</div><div class=\"v %1\">%2 节</div></div>\n")
             .arg(teacherForbid == 0 ? QStringLiteral("good") : QStringLiteral("bad"))
             .arg(teacherForbid);
    h += QStringLiteral("<div class=\"card\"><div class=\"k\">老师五项综合（平均，越低越好）</div>"
                        "<div class=\"v\">%1</div></div>\n")
             .arg(QString::number(overall.avgComposite, 'f', 2));
    h += QStringLiteral("</div>\n");

    // 十项指标（全校）
    h += QStringLiteral("<h2>十项指标（全校）</h2>\n<table>\n"
                        "<tr><th>这一项</th><th>硬/软</th><th>方向</th><th>实测</th><th>单位</th></tr>\n");
    for (const auto &i : overall.indicators) {
        h += QStringLiteral("<tr><td>%1</td><td>%2</td><td class=\"dir\">%3</td>"
                            "<td class=\"num\">%4</td><td>%5</td></tr>\n")
                 .arg(i.name.toHtmlEscaped(), indicatorBadge(i), indicatorDirection(i),
                      formatIndicator(i), i.unit.toHtmlEscaped());
    }
    h += QStringLiteral("</table>\n");

    // 老师五项平均
    h += QStringLiteral("<h2>老师五项（全校平均）</h2>\n<table>\n"
                        "<tr><th>教案连续性 ↑1</th><th>教师开窗 ↓</th><th>周内均衡 ↓</th>"
                        "<th>平行班先后公平 ↓</th><th>综合 ↓</th></tr>\n");
    h += QStringLiteral("<tr><td class=\"num\">%1</td><td class=\"num\">%2</td><td class=\"num\">%3</td>"
                        "<td class=\"num\">%4</td><td class=\"num\">%5</td></tr>\n")
             .arg(QString::number(overall.avgContinuity, 'f', 3),
                  QString::number(overall.avgWindow, 'f', 2),
                  QString::number(overall.avgBalance, 'f', 2),
                  QString::number(overall.avgFairness, 'f', 2),
                  QString::number(overall.avgComposite, 'f', 2));
    h += QStringLiteral("</table>\n");

    // 按年级组
    for (const auto &g : school.groups) {
        if (g.type != sched::Group::Type::Grade)
            continue;
        QVector<qint64> ids;
        for (qint64 cid : g.memberIds) {
            if (school.findClass(cid))
                ids.append(cid);
        }
        if (ids.isEmpty())
            continue;
        const sched::QualityMetricsResult r = sched::computeQualityMetrics(school, ids);
        h += QStringLiteral("<h2>%1（%2 个班）</h2>\n")
                 .arg(g.name.toHtmlEscaped())
                 .arg(ids.size());
        h += QStringLiteral("<table>\n<tr><th>十项指标</th><th>硬/软</th><th>实测</th><th>单位</th></tr>\n");
        for (const auto &i : r.indicators) {
            h += QStringLiteral("<tr><td>%1</td><td>%2</td><td class=\"num\">%3</td><td>%4</td></tr>\n")
                     .arg(i.name.toHtmlEscaped(), indicatorBadge(i), formatIndicator(i),
                          i.unit.toHtmlEscaped());
        }
        h += QStringLiteral("</table>\n");
        h += QStringLiteral("<p class=\"sub\">老师五项平均：教案连续性 %1 · 教师开窗 %2 · 周内均衡 %3 · "
                            "平行班先后公平 %4 · 综合 %5</p>\n")
                 .arg(QString::number(r.avgContinuity, 'f', 3),
                      QString::number(r.avgWindow, 'f', 2),
                      QString::number(r.avgBalance, 'f', 2),
                      QString::number(r.avgFairness, 'f', 2),
                      QString::number(r.avgComposite, 'f', 2));
    }

    // 老师明细
    if (!overall.teachers.isEmpty()) {
        h += QStringLiteral("<h2>老师明细（按周节数排序）</h2>\n<table>\n"
                            "<tr><th>老师</th><th>周节数</th><th>教案连续性 ↑1</th><th>教师开窗 ↓</th>"
                            "<th>周内均衡 ↓</th><th>平行班先后公平 ↓</th><th>综合 ↓</th></tr>\n");
        for (const auto &t : overall.teachers) {
            h += QStringLiteral("<tr><td>%1</td><td class=\"num\">%2</td><td class=\"num\">%3</td>"
                                "<td class=\"num\">%4</td><td class=\"num\">%5</td>"
                                "<td class=\"num\">%6</td><td class=\"num\">%7</td></tr>\n")
                     .arg(t.name.toHtmlEscaped())
                     .arg(t.weeklyPeriods)
                     .arg(QString::number(t.continuity, 'f', 3),
                          QString::number(t.window, 'f', 2),
                          QString::number(t.balance, 'f', 2),
                          QString::number(t.fairness, 'f', 2),
                          QString::number(t.composite, 'f', 2));
        }
        h += QStringLiteral("</table>\n");
    }

    // 口径说明
    h += QStringLiteral(
        "<h2>指标怎么算</h2>\n"
        "<div class=\"note\">硬指标不许破（课时排满、老师不撞课、不排时段、连堂）；软指标是舒适度，"
        "越低越好，可据此手动微调。</div>\n"
        "<h3>十项指标</h3>\n<ol>\n"
        "<li><b>排上的节数</b>：教学计划要求的周课时里已经排上的节数（单双周各按 0.5 节）。</li>\n"
        "<li><b>老师撞课</b>：同一位老师同一节出现在两个班（单周与双周同格不算，约定合班不算）。</li>\n"
        "<li><b>学科排进了不排的时段</b>：课程被设了「不排时间」，却仍被排进那些格子。</li>\n"
        "<li><b>老师排进了不排的时段</b>：老师被设了「不排时间」，却仍被排课。</li>\n"
        "<li><b>该连堂的连上了</b>：计划要求连堂的课，实际形成连续节的达成数 / 要求数。</li>\n"
        "<li><b>连堂落在第 1-2 或 4-5 节</b>：连堂尽量不跨大课间、不跨午休，落在习惯位置。</li>\n"
        "<li><b>语数英排在上午</b>：主课（课程设置里勾选「主课」）落在上午节次的比例。</li>\n"
        "<li><b>同一科一天上 3 节及以上</b>：某班某天同一科连上三节以上，学生吃不消的次数。</li>\n"
        "<li><b>老师一天上 5 节及以上</b>：某位老师某天累计 5 节以上，算一个人·天。</li>\n"
        "<li><b>老师连上 3 节及以上</b>：中间没有空堂连着上三节以上（午休隔开的不算）。</li>\n"
        "</ol>\n"
        "<h3>老师五项</h3>\n"
        "<p><span class=\"formula\">教案连续性 C = 1 / (1 + D̄)</span><br>"
        "同一位老师、同一门课、同一天教的几个平行班，「这周第几次课」的最大差 D，所有组取平均 D̄。"
        "几个班齐头并进 C 就是 1。</p>\n"
        "<p><span class=\"formula\">教师开窗 F = G + q(p_max) − q(p_min)</span><br>"
        "G 是课与课之间断开的次数，后半截是从第一节到最后一节的跨度（午休算一格）。"
        "课中间空着、从早拖到晚都会变大。</p>\n"
        "<p><span class=\"formula\">周内均衡 E = Σ|x_d − x̄| − D_min</span><br>"
        "x_d 是某位老师星期 d 的节数，减掉「怎么排都躲不掉」的不均 D_min = 2r(N−r)/N。体育不算。</p>\n"
        "<p><span class=\"formula\">平行班先后公平 B = ½ Σ|v − n/M|</span><br>"
        "同一位老师同一门课的一组平行班，各班当「试讲班」的次数与理想次数的偏差。</p>\n"
        "<p><span class=\"formula\">综合 S = F + E + B</span><br>开窗、周内均衡、平行班公平三项加起来，"
        "一个数看老师整体顺不顺。</p>\n");

    h += QStringLiteral("<footer>由 智排课系统 SmartScheduler 生成 · 数据来源：当前项目的教学计划与排课结果</footer>\n"
                        "</body></html>\n");
    return h;
}

bool AppController::exportQualityReportHtml(const QString &pathOrUrl)
{
    QString path = toLocalPath(pathOrUrl);
    if (!path.endsWith(QLatin1StringView(".html"), Qt::CaseInsensitive))
        path += QStringLiteral(".html");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_lastError = tr("无法写入文件：%1").arg(path);
        return false;
    }
    const QByteArray data = buildQualityReportHtml(m_school).toUtf8();
    if (f.write(data) != data.size()) {
        m_lastError = tr("写入不完整：%1").arg(path);
        return false;
    }
    f.close();
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    return true;
}



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

    // 课程（12 门，含主课/理科/平均分布/上下午；自习作为独立课程排满课表）
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
    const qint64 zixi = addCourse(QStringLiteral("自习"), false, false, sched::AmPmPolicy::Afternoon);

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
        plan(cls, zixi, {ywTeachers[i]}, 2.5); // 自习排满剩余容量（32.5+2.5=35），由语文任课老师看班
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
        // 示例计划已排满整周（零余量），提高搜索预算以保证稳定零硬违例
        runScheduler({{QStringLiteral("runs"), 3},
                      {QStringLiteral("maxIterations"), 40000},
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

// 本次探测能否据此定论：给出更新的版本随时可信；「不比当前新」只有实时通道说了才算数。
// 第三方反代 / CDN 会缓存 version.json（发版后仍返回旧版本），它们说「没更新」必须继续问下一个，
// 否则就会出现「明明发了新版，检查更新却说已是最新」。
bool AppController::updateProbeConclusive(const QString &tag, const QString &current, bool freshChannel)
{
    return freshChannel || compareVersions(tag, current) > 0;
}

void AppController::checkUpdate()
{
    checkUpdate(true); // 手动点击：显示「检查中」状态
}

void AppController::checkUpdateSilently()
{
    checkUpdate(false);
}

void AppController::checkUpdate(bool interactive)
{
    if (m_updateStatus == 1) // 防止重复触发（手动/定时会撞车）
        return;
    if (m_updateDlState == 1)
        return; // 正在下载：先让它跑完
    if (m_updateDlState == 2) {
        // 已下载好一版但还没安装：不重复检查（免得把已就绪的安装包作废），
        // 但手动点击必须给出反馈——否则按钮看起来「点了没反应」
        if (interactive) {
            m_updateStatus = 2;
            m_updateMessage = tr("新版本 %1 已下载完成，点击上方「安装更新」即可升级。")
                                  .arg(m_latestVersion);
            Q_EMIT updateStatusChanged();
        }
        return;
    }
    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);
    if (interactive)
        m_updateStatus = 1;
    m_updateMessage.clear();
    // 重新检查会让旧的下载结果失效
    m_updateDlState = 0;
    m_updateDlPercent = 0;
    m_updateDlMessage.clear();
    m_downloadedSetup.clear();
    m_latestSha256.clear();
    m_latestSize = 0;
    m_probeTag.clear();
    m_probeSetup.clear();
    m_probeSha256.clear();
    m_probeSize = 0;
    Q_EMIT updateStatusChanged();
    Q_EMIT updateDlStateChanged();

    // 通道分两类：
    //  · 实时通道（fresh）：自建中继（服务端直连 GitHub API）与 GitHub API 本身，拿到「没更新」即可定论；
    //  · 缓存通道：公益反代 / jsDelivr 会缓存 version.json（发版后几分钟仍返回旧版本），
    //    只能用来「尽早发现新版本」，说「已是最新」不算数，要继续问下一个通道。
    // 这样既能走国内可达的加速通道，又不会像以前那样被缓存钉死在「已是最新」（v1.0.37→v1.0.38 就踩了这个坑）。
    const QString bust = QStringLiteral("?t=%1").arg(QDateTime::currentSecsSinceEpoch());
    const QString ver = appVersion();
    // 手动点击时带 fresh=1：中继会强制回源 GitHub，避免它自己的 60s 缓存挡住刚发布的版本
    const QString relay = QStringLiteral("https://api.class.feimsg.cn/v1/version?app=%1%2&t=%3")
                              .arg(ver,
                                   interactive ? QStringLiteral("&fresh=1") : QString(),
                                   QString::number(QDateTime::currentSecsSinceEpoch()));
    m_updateChannels = {
        {relay, true},
        {QStringLiteral("https://ghfast.top/https://raw.githubusercontent.com/tangkris1101-alt/SmartScheduler-Releases/main/version.json") + bust, false},
        {QStringLiteral("https://gh-proxy.com/https://raw.githubusercontent.com/tangkris1101-alt/SmartScheduler-Releases/main/version.json") + bust, false},
        {QStringLiteral("https://ghproxy.net/https://raw.githubusercontent.com/tangkris1101-alt/SmartScheduler-Releases/main/version.json") + bust, false},
        {QStringLiteral("https://api.github.com/repos/tangkris1101-alt/SmartScheduler-Releases/releases/latest"), true},
        {QStringLiteral("https://cdn.jsdelivr.net/gh/tangkris1101-alt/SmartScheduler-Releases@main/version.json") + bust, false},
    };
    m_updateAttempt = 0;
    m_freshChannelAnswered = false;
    tryUpdateSource();
}

// 所有通道都问过一遍还没有实时通道给出「已是最新」时的收尾（例如实时通道全被墙/超时）
void AppController::finishUpdateProbe()
{
    if (!m_probeTag.isEmpty()) {
        m_updateStatus = 2;
        m_latestVersion = m_probeTag; // 记下见过的最高版本，供日志与下载链路自检使用
        m_latestSetup = m_probeSetup;
        m_latestSha256 = m_probeSha256;
        m_latestSize = m_probeSize;
        m_updateMessage = tr("✓ 当前已是最新版本（v%1）").arg(appVersion());
        if (!m_freshChannelAnswered)
            qWarning("检查更新: 实时通道均不可达，结论只依据缓存通道，可能滞后");
        Q_EMIT updateStatusChanged();
        return;
    }
    m_updateStatus = 4;
    m_updateMessage = tr("自动检查失败（已依次尝试自建加速、镜像反代、GitHub 直连与国内 CDN），"
                         "请检查网络后重试，或打开发布页查看。");
    Q_EMIT updateStatusChanged();
}


void AppController::tryUpdateSource()
{
    if (m_updateAttempt >= m_updateChannels.size()) {
        finishUpdateProbe();
        return;
    }
    const UpdateChannel channel = m_updateChannels.at(m_updateAttempt++);
    const QString url = channel.url;
    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "SmartScheduler-UpdateCheck");
    req.setTransferTimeout(8000); // 8s 超时，避免单一通道卡死
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, channel]() {
        reply->deleteLater();
        const QString url = channel.url;
        if (reply->error() != QNetworkReply::NoError) {
            qWarning("检查更新: 通道 %s 失败（%s），尝试下一通道", qPrintable(url),
                     qPrintable(reply->errorString()));
            tryUpdateSource();
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        // version.json（反代/CDN 通道）用 tag；GitHub API 与自建中继都用 tag/tag_name
        QString tag = obj.value(QStringLiteral("tag")).toString();
        if (tag.isEmpty())
            tag = obj.value(QStringLiteral("tag_name")).toString();
        if (tag.isEmpty()) {
            qWarning("检查更新: 通道 %s 返回内容无法解析，尝试下一通道", qPrintable(url));
            tryUpdateSource();
            return;
        }
        // 安装包文件名：version.json 直接给；GitHub API 通道按 tag 推导
        QString setup = obj.value(QStringLiteral("setup")).toString();
        if (setup.isEmpty())
            setup = QStringLiteral("SmartScheduler-%1-setup.exe").arg(QString(tag).mid(1));
        m_latestSetup = setup;
        // 完整性校验信息：version.json 带 sha256/size；GitHub API 在 asset.digest 里
        m_latestSha256 = obj.value(QStringLiteral("sha256")).toString();
        m_latestSize = obj.value(QStringLiteral("size")).toVariant().toLongLong();
        if (m_latestSha256.isEmpty()) {
            const auto assets = obj.value(QStringLiteral("assets")).toArray();
            for (const auto &a : assets) {
                const auto ao = a.toObject();
                if (ao.value(QStringLiteral("name")).toString() == setup) {
                    const QString digest = ao.value(QStringLiteral("digest")).toString();
                    if (digest.startsWith(QStringLiteral("sha256:")))
                        m_latestSha256 = digest.mid(7);
                    break;
                }
            }
        }
        const QString current = appVersion();
        const bool conclusive = updateProbeConclusive(tag, current, channel.fresh);
        qWarning("检查更新: 通道 %s 返回 tag=%s（当前 v%s，%s通道%s）", qPrintable(url),
                 qPrintable(tag), qPrintable(current),
                 channel.fresh ? "实时" : "缓存",
                 conclusive ? "可定论" : "不足以定论");
        if (!conclusive) {
            // 缓存通道说「不比当前新」：可能是反代/CDN 还没刷到新版本，继续问下一个通道；
            // 顺手记下见过的最高版本，供全部通道都没有更新版本时兜底展示。
            if (m_probeTag.isEmpty() || compareVersions(tag, m_probeTag) > 0) {
                m_probeTag = tag;
                m_probeSetup = m_latestSetup;
                m_probeSha256 = m_latestSha256;
                m_probeSize = m_latestSize;
            }
            tryUpdateSource();
            return;
        }
        if (compareVersions(tag, current) > 0) {

            m_updateStatus = 3;
            m_latestVersion = tag;
            m_updateMessage = tr("发现新版本 %1，正在自动下载…").arg(tag);
            Q_EMIT updateStatusChanged();
            // 立即下载（仅安装需要用户确认）；下载中/已就绪时不重复触发
            if (m_updateDlState != 1 && m_updateDlState != 2)
                downloadUpdate();
            return;
        }
        m_updateStatus = 2;
        m_latestVersion = tag;
        m_updateMessage = tr("✓ 当前已是最新版本（v%1）").arg(appVersion());
        if (channel.fresh)
            m_freshChannelAnswered = true;
        Q_EMIT updateStatusChanged();
    });
}

void AppController::openMirrorDownload()
{
    if (m_latestVersion.isEmpty()) {
        openReleasesPage();
        return;
    }
    QString setup = m_latestSetup;
    if (setup.isEmpty()) // 兼容无 setup 字段的旧 version.json / API 通道
        setup = QStringLiteral("SmartScheduler-%1-setup.exe").arg(QString(m_latestVersion).mid(1));
    QDesktopServices::openUrl(
        QUrl(QStringLiteral("https://ghfast.top/https://github.com/tangkris1101-alt/"
                            "SmartScheduler-Releases/releases/download/%1/%2")
                 .arg(m_latestVersion, setup)));
}

void AppController::downloadUpdate()
{
    if (m_updateDlState == 1) // 下载中，防重复点击
        return;
    if (m_latestVersion.isEmpty()) {
        m_updateDlState = 3;
        m_updateDlMessage = tr("请先点击「检查更新」获取版本信息。");
        Q_EMIT updateDlStateChanged();
        return;
    }
    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);
    QString setup = m_latestSetup;
    if (setup.isEmpty())
        setup = QStringLiteral("SmartScheduler-%1-setup.exe").arg(QString(m_latestVersion).mid(1));

    // 与检查更新同源的镜像链：反代优先，GitHub 直连兜底；任一通道失败/校验不过自动回退
    const QString dl = QStringLiteral(
        "https://github.com/tangkris1101-alt/SmartScheduler-Releases/releases/download/%1/%2")
                           .arg(m_latestVersion, setup);
    m_downloadUrls = {
        QStringLiteral("https://ghfast.top/") + dl,
        QStringLiteral("https://gh-proxy.com/") + dl,
        QStringLiteral("https://ghproxy.net/") + dl,
        dl,
    };
    m_dlAttempt = 0;
    m_updateDlState = 1;
    m_updateDlPercent = 0;
    m_updateDlMessage.clear();
    m_downloadedSetup.clear();
    Q_EMIT updateDlStateChanged();
    tryUpdateDownload();
}

void AppController::tryUpdateDownload()
{
    if (m_dlAttempt >= m_downloadUrls.size()) {
        m_updateDlState = 3;
        m_updateDlMessage = tr("自动下载失败（已依次尝试镜像加速与 GitHub 直连），"
                               "请检查网络后重试，或改用浏览器下载。");
        Q_EMIT updateDlStateChanged();
        return;
    }
    const QString url = m_downloadUrls.at(m_dlAttempt++);
    const QString setup = m_latestSetup;
    m_updateDlPercent = 0; // 换通道后进度重新计
    Q_EMIT updateDlStateChanged();
    QDir tmp = QDir::temp();
    tmp.mkpath(QStringLiteral("SmartScheduler-update"));
    const QString dst = tmp.filePath(QStringLiteral("SmartScheduler-update/") + setup);

    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("User-Agent", "SmartScheduler-Updater");
    req.setTransferTimeout(20000); // 20s 无传输即判失败，换下一通道
    QNetworkReply *reply = m_nam->get(req);
    auto *file = new QFile(dst);
    if (!file->open(QIODevice::WriteOnly)) {
        reply->abort();
        reply->deleteLater();
        delete file;
        m_updateDlState = 3;
        m_updateDlMessage = tr("无法写入临时目录：%1").arg(dst);
        Q_EMIT updateDlStateChanged();
        return;
    }
    connect(reply, &QNetworkReply::readyRead, this, [reply, file]() {
        file->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        const qint64 denom = total > 0 ? total : m_latestSize;
        if (denom > 0) {
            m_updateDlPercent = int(got * 100 / denom);
            Q_EMIT updateDlStateChanged();
        }
    });
    // 慢速通道早期熔断：8 秒仍不到 5%（约 200KB/s 以下）就换下一通道；
    // 末位候选（GitHub 直连）不限速，保证慢网用户最终仍能完整下载
    if (m_dlAttempt < m_downloadUrls.size()) {
        auto *timer = new QTimer(reply);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [this, reply, url]() {
            if (m_updateDlPercent >= 5)
                return;
            qWarning("自动更新: 通道 %s 速度过慢（8 秒内不足 5%%），改试下一通道", qPrintable(url));
            reply->abort(); // 触发 finished 的错误分支，自动换通道
        });
        timer->start(8000);
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, dst, url]() {
        reply->deleteLater();
        file->write(reply->readAll());
        file->close();
        const auto err = reply->error();
        delete file;
        if (err != QNetworkReply::NoError) {
            qWarning("自动更新: 通道 %s 下载失败（%s），尝试下一通道", qPrintable(url),
                     qPrintable(reply->errorString()));
            QFile::remove(dst);
            tryUpdateDownload();
            return;
        }
        // 安装包不可能小于 5MB：小于即视为镜像返回了错误页，换通道
        const qint64 fs = QFileInfo(dst).size();
        if (fs < 5 * 1024 * 1024) {
            qWarning("自动更新: 通道 %s 内容过小（%lld 字节），尝试下一通道", qPrintable(url),
                     (long long)fs);
            QFile::remove(dst);
            tryUpdateDownload();
            return;
        }
        // sha256 校验（version.json / GitHub API 提供时）
        if (!m_latestSha256.isEmpty()) {
            QFile f(dst);
            if (!f.open(QIODevice::ReadOnly)
                || QString::fromLatin1(QCryptographicHash::hash(f.readAll(),
                                                                QCryptographicHash::Sha256)
                                           .toHex())
                           .compare(m_latestSha256, Qt::CaseInsensitive)
                       != 0) {
                qWarning("自动更新: 通道 %s sha256 校验未通过，尝试下一通道", qPrintable(url));
                QFile::remove(dst);
                tryUpdateDownload();
                return;
            }
        }
        qWarning("自动更新: 通道 %s 下载完成（%lld 字节）", qPrintable(url), (long long)fs);
        m_downloadedSetup = dst;
        m_updateDlPercent = 100;
        m_updateDlState = 2;
        m_updateDlMessage = tr("下载完成，校验通过。");
        Q_EMIT updateDlStateChanged();
    });
}

void AppController::installUpdate()
{
    if (m_updateDlState != 2 || m_downloadedSetup.isEmpty())
        return;
    // 不能把带引号的复合命令直接塞给 cmd.exe /c：QProcess 会做一层 MSVC 式
    // 引号转义（\"），cmd 不识别这种转义，曾把 start 的参数错误解析成「\\」。
    // 改为把命令写进 .bat（内容所见即所得；按系统本地编码写入以兼容中文用户名路径），
    // 再启动这个批处理——路径作为无内嵌引号的单一参数，QProcess 加引号是安全的。
    const QString batPath =
        QFileInfo(m_downloadedSetup).dir().filePath(QStringLiteral("run-update.bat"));
    QFile f(batPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(updateBatchContent(m_downloadedSetup).toLocal8Bit());
        f.close();
        QProcess::startDetached(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), batPath});
    } else {
        // 兜底：批处理写失败则直接拉起安装器（无延迟，交给 Inno /CLOSEAPPLICATIONS 处理）
        qWarning("自动更新: 无法写入 %s，改为直接启动安装器", qPrintable(batPath));
        QProcess::startDetached(m_downloadedSetup,
                                {QStringLiteral("/VERYSILENT"), QStringLiteral("/NORESTART"),
                                 QStringLiteral("/CLOSEAPPLICATIONS")});
    }
    QMetaObject::invokeMethod(QCoreApplication::instance(), "quit", Qt::QueuedConnection);
}

QString AppController::updateBatchContent(const QString &setupPath)
{
    // ping 延迟约 2 秒等本程序退出释放文件锁；Inno 旗标：静默覆盖、不重启、兜底关残留进程
    return QStringLiteral("@echo off\r\n"
                          "ping 127.0.0.1 -n 3 >nul\r\n"
                          "\"%1\" /VERYSILENT /NORESTART /CLOSEAPPLICATIONS\r\n")
        .arg(setupPath);
}

void AppController::openReleasesPage()
{
    QDesktopServices::openUrl(
        QUrl(QStringLiteral("https://github.com/tangkris1101-alt/SmartScheduler-Releases/releases")));
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
