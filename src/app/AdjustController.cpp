#include "AdjustController.h"

#include "AppController.h"

#include <QFile>
#include <QPdfWriter>
#include <QPainter>
#include <QTextStream>
#include <QUrl>
#include <algorithm>

namespace {

// 快照式撤销命令：redo/undo 直接整体替换 assignments（单班课表数据量小）
class ScheduleCommand : public QUndoCommand
{
public:
    ScheduleCommand(AppController *app, const QString &text,
                    QVector<sched::ScheduleAssignment> before,
                    QVector<sched::ScheduleAssignment> after, bool temporary)
        : QUndoCommand(text)
        , m_app(app)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_temporary(temporary)
    {
    }

    void redo() override { apply(m_after); }
    void undo() override { apply(m_before); }

private:
    void apply(const QVector<sched::ScheduleAssignment> &v)
    {
        m_app->school()->schedule.assignments = v;
        if (!m_temporary)
            m_app->markDirty(); // 临时调课退出即丢弃，不置脏
        m_app->notifyScheduleChanged();
    }

    AppController *m_app;
    QVector<sched::ScheduleAssignment> m_before, m_after;
    bool m_temporary;
};

} // namespace

AdjustController::AdjustController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    connect(&m_undo, &QUndoStack::canUndoChanged, this, &AdjustController::undoStackChanged);
    connect(&m_undo, &QUndoStack::canRedoChanged, this, &AdjustController::undoStackChanged);
    connect(&m_undo, &QUndoStack::undoTextChanged, this, &AdjustController::undoStackChanged);
    connect(&m_undo, &QUndoStack::redoTextChanged, this, &AdjustController::undoStackChanged);
    connect(m_app, &AppController::projectChanged, this, [this] {
        m_undo.clear();
        m_buffer = {};
        Q_EMIT bufferChanged();
        if (m_temporary) {
            m_temporary = false;
            Q_EMIT temporaryModeChanged();
        }
        Q_EMIT undoStackChanged();
    });
}

void AdjustController::setIssueHintsEnabled(bool on)
{
    if (m_issueHints == on)
        return;
    m_issueHints = on;
    Q_EMIT issueHintsEnabledChanged();
}

// ==================== 基础查询 ====================

AdjustController::CellContent AdjustController::cellAt(qint64 classId, int slot, int half) const
{
    CellContent out;
    for (const auto &a : m_app->school()->schedule.assignments) {
        if (a.classId != classId || a.slotIndex != slot)
            continue;
        if (halfOverlap(a.half, half)) {
            // 同 parity 优先取同 half，整节格取整节
            if (a.half == half || half == 0 || out.courseId < 0) {
                out.classId = a.classId;
                out.courseId = a.courseId;
                out.half = a.half;
                out.found = true;
                if (a.half == half)
                    break;
            }
        }
    }
    return out;
}

QVector<QPair<qint64, qint64>> AdjustController::combinedMembers(qint64 classId, qint64 courseId) const
{
    const sched::School *s = m_app->school();
    for (const auto &g : s->combinedGroups) {
        if (g.entries.contains({classId, courseId}))
            return g.entries;
    }
    return {{classId, courseId}};
}

double AdjustController::placedPeriods(qint64 classId, qint64 courseId) const
{
    double n = 0;
    for (const auto &a : m_app->school()->schedule.assignments) {
        if (a.classId == classId && a.courseId == courseId)
            n += (a.half == 0 ? 1.0 : 0.5);
    }
    return n;
}

// ==================== 冲突校验 ====================

QString AdjustController::moveLockError(qint64 classId, int fromSlot, qint64 courseId) const
{
    // 预排课程固定在预排节次，不可移走
    for (const auto &p : m_app->school()->preAssignments) {
        if (p.classId == classId && p.courseId == courseId && p.slotIndex == fromSlot)
            return QStringLiteral("预排课程不可移动（先在「限制条件」页取消预排）");
    }
    return {};
}

QString AdjustController::placementError(const QVector<sched::ScheduleAssignment> &work,
                                         qint64 classId, qint64 courseId, int half, int slot) const
{
    const sched::School *s = m_app->school();
    if (!s->timeConfig.isValidSlot(slot))
        return QStringLiteral("节次超出范围");

    // 目标格已预排其他课程 → 禁止
    for (const auto &p : s->preAssignments) {
        if (p.classId == classId && p.slotIndex == slot && p.courseId != courseId)
            return QStringLiteral("该节次已预排其他课程");
    }

    // 合班联动：所有成员班一起检查
    const auto members = combinedMembers(classId, courseId);
    const bool isCombined = members.size() > 1;

    for (const auto &m : members) {
        const qint64 mc = m.first, mco = m.second;
        // 班级占用
        for (const auto &a : work) {
            if (a.classId == mc && a.slotIndex == slot && halfOverlap(a.half, half))
                return QStringLiteral("目标节次已有课程（%1）").arg(s->findClass(mc)->name);
        }
        // 不排时间：班级/课程
        for (const auto &c : s->noScheduleConstraints) {
            using ET = sched::NoScheduleConstraint::EntityType;
            if (!c.slots.contains(slot))
                continue;
            if (c.entityType == ET::Class && c.entityId == mc)
                return QStringLiteral("%1 该节次设为不排").arg(s->findClass(mc)->name);
            if (c.entityType == ET::Course && c.entityId == mco)
                return QStringLiteral("该课程此节次设为不排");
        }
        // 老师：冲突 / 活动 / 互斥组 / 不排
        QVector<qint64> tids;
        for (const auto &e : s->planEntries) {
            if (e.classId == mc && e.courseId == mco) {
                tids = e.teacherIds;
                break;
            }
        }
        for (qint64 tid : tids) {
            const sched::Teacher *t = s->findTeacher(tid);
            const QString tname = t ? t->name : QStringLiteral("?");
            for (const auto &a : work) {
                if (a.slotIndex != slot || !halfOverlap(a.half, half))
                    continue;
                // 同合班组的课共享老师，不算冲突
                if (isCombined) {
                    bool sameSession = false;
                    for (const auto &g : s->combinedGroups) {
                        if (g.entries.contains({classId, courseId})
                            && g.entries.contains({a.classId, a.courseId})) {
                            sameSession = true;
                            break;
                        }
                    }
                    if (sameSession)
                        continue;
                }
                for (const auto &e : s->planEntries) {
                    if (e.classId == a.classId && e.courseId == a.courseId
                        && e.teacherIds.contains(tid))
                        return QStringLiteral("%1 该节次已有课").arg(tname);
                }
            }
            for (const auto &act : s->teacherActivities) {
                if (act.teacherId == tid && act.slotIndex == slot)
                    return QStringLiteral("%1 该节次有活动安排").arg(tname);
            }
            if (t && t->mutualExclusionGroup >= 0) {
                for (const auto &a : work) {
                    if (a.slotIndex != slot || !halfOverlap(a.half, half))
                        continue;
                    for (const auto &e : s->planEntries) {
                        if (e.classId != a.classId || e.courseId != a.courseId)
                            continue;
                        for (qint64 t2 : e.teacherIds) {
                            const sched::Teacher *other = s->findTeacher(t2);
                            if (other && t2 != tid
                                && other->mutualExclusionGroup == t->mutualExclusionGroup)
                                return QStringLiteral("互斥组老师 %1 该节次已有课").arg(other->name);
                        }
                    }
                }
            }
            for (const auto &c : s->noScheduleConstraints) {
                if (c.entityType == sched::NoScheduleConstraint::EntityType::Teacher
                    && c.entityId == tid && c.slots.contains(slot))
                    return QStringLiteral("%1 该节次设为不排").arg(tname);
            }
        }
        // 教室容量与教室不排
        qint64 roomId = -1;
        for (const auto &e : s->planEntries) {
            if (e.classId == mc && e.courseId == mco) {
                roomId = e.classroomId;
                break;
            }
        }
        if (roomId >= 0) {
            const sched::Classroom *room = s->findClassroom(roomId);
            int used = 0;
            for (const auto &a : work) {
                if (a.slotIndex != slot || !halfOverlap(a.half, half))
                    continue;
                if (isCombined) {
                    // 同组成员同室同课，只计一次座位由下方成员数统一加
                    bool sameSession = false;
                    for (const auto &g : s->combinedGroups) {
                        if (g.entries.contains({classId, courseId})
                            && g.entries.contains({a.classId, a.courseId})) {
                            sameSession = true;
                            break;
                        }
                    }
                    if (sameSession)
                        continue;
                }
                for (const auto &e : s->planEntries) {
                    if (e.classId == a.classId && e.courseId == a.courseId
                        && e.classroomId == roomId) {
                        used++;
                        break;
                    }
                }
            }
            const int seats = isCombined ? int(members.size()) : 1;
            if (room && used + seats > room->capacity)
                return QStringLiteral("教室「%1」容量不足").arg(room->name);
            for (const auto &c : s->noScheduleConstraints) {
                if (c.entityType == sched::NoScheduleConstraint::EntityType::Classroom
                    && c.entityId == roomId && c.slots.contains(slot))
                    return QStringLiteral("教室「%1」该节次设为不排")
                        .arg(room ? room->name : QStringLiteral("?"));
            }
        }
    }
    return {};
}

// ==================== 目标计算与操作 ====================

QVariantList AdjustController::targetsFor(qint64 classId, int fromSlot, int fromHalf) const
{
    QVariantList out;
    const sched::School *s = m_app->school();
    const int sc = s->timeConfig.slotCount();
    const CellContent src = cellAt(classId, fromSlot, fromHalf);
    if (!src.found) {
        for (int i = 0; i < sc; ++i)
            out.append(0);
        return out;
    }
    if (!moveLockError(classId, fromSlot, src.courseId).isEmpty()) {
        for (int i = 0; i < sc; ++i)
            out.append(0); // 预排锁定：全部禁止
        return out;
    }
    // 合班课只能移到空格
    const bool isCombined = combinedMembers(src.classId, src.courseId).size() > 1;

    for (int slot = 0; slot < sc; ++slot) {
        if (slot == fromSlot) {
            out.append(0);
            continue;
        }
        // 工作表：移除源（含合班成员）
        QVector<sched::ScheduleAssignment> work = s->schedule.assignments;
        const auto members = combinedMembers(src.classId, src.courseId);
        work.erase(std::remove_if(work.begin(), work.end(), [&](const sched::ScheduleAssignment &a) {
                       if (a.slotIndex != fromSlot || !halfOverlap(a.half, src.half))
                           return false;
                       for (const auto &m : members) {
                           if (a.classId == m.first && a.courseId == m.second)
                               return true;
                       }
                       return false;
                   }),
                   work.end());

        const CellContent dst = cellAt(classId, slot, src.half);
        if (!dst.found) {
            out.append(placementError(work, classId, src.courseId, src.half, slot).isEmpty() ? 1 : 0);
        } else {
            // 对调：half 必须相同（整节↔整节 或 同 parity half）
            if (isCombined || dst.half != src.half) {
                out.append(0);
                continue;
            }
            if (!moveLockError(classId, slot, dst.courseId).isEmpty()) {
                out.append(0); // 目标是预排课
                continue;
            }
            // 模拟：B 也移除后，A 落 B 位、B 落 A 位
            QVector<sched::ScheduleAssignment> work2 = work;
            work2.erase(std::remove_if(work2.begin(), work2.end(),
                                       [&](const sched::ScheduleAssignment &a) {
                                           return a.classId == classId && a.slotIndex == slot
                                                  && halfOverlap(a.half, src.half);
                                       }),
                        work2.end());
            if (!placementError(work2, classId, src.courseId, src.half, slot).isEmpty()) {
                out.append(0);
                continue;
            }
            work2.append({classId, slot, src.half, src.courseId});
            out.append(placementError(work2, classId, dst.courseId, dst.half, fromSlot).isEmpty()
                           ? 2
                           : 0);
        }
    }
    return out;
}

QVariantMap AdjustController::moveCell(qint64 classId, int fromSlot, int fromHalf, int toSlot,
                                       int toHalf)
{
    auto fail = [](const QString &reason) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("reason"), reason}};
    };
    const CellContent src = cellAt(classId, fromSlot, fromHalf);
    if (!src.found)
        return fail(QStringLiteral("源节次没有课程"));
    if (fromSlot == toSlot && fromHalf == toHalf)
        return fail(QStringLiteral("未移动"));
    if (const QString err = moveLockError(classId, fromSlot, src.courseId); !err.isEmpty())
        return fail(err);
    // half 匹配：保持源 half 落到目标（toHalf 由 UI 传入，默认与源相同）
    const CellContent dst = cellAt(classId, toSlot, toHalf);
    if (dst.found && dst.half != src.half)
        return fail(QStringLiteral("整节与单双周不能对调"));

    const bool isCombined = combinedMembers(src.classId, src.courseId).size() > 1;
    if (dst.found && isCombined)
        return fail(QStringLiteral("合班课只能移动到空节次（不支持对调）"));
    if (dst.found) {
        if (const QString err = moveLockError(classId, toSlot, dst.courseId); !err.isEmpty())
            return fail(QStringLiteral("目标是预排课程，不可对调"));
    }

    // 构造 after 表：移除源（含合班成员）→ 校验 → 写入目标
    const auto members = combinedMembers(src.classId, src.courseId);
    QVector<sched::ScheduleAssignment> after = m_app->school()->schedule.assignments;
    after.erase(std::remove_if(after.begin(), after.end(), [&](const sched::ScheduleAssignment &a) {
                    if (a.slotIndex != fromSlot || !halfOverlap(a.half, src.half))
                        return false;
                    for (const auto &m : members) {
                        if (a.classId == m.first && a.courseId == m.second)
                            return true;
                    }
                    return false;
                }),
                after.end());

    if (dst.found) {
        // 对调：移除目标 B，校验双方
        QVector<sched::ScheduleAssignment> work = after;
        work.erase(std::remove_if(work.begin(), work.end(), [&](const sched::ScheduleAssignment &a) {
                       return a.classId == classId && a.slotIndex == toSlot
                              && halfOverlap(a.half, src.half);
                   }),
                   work.end());
        if (const QString err = placementError(work, classId, src.courseId, src.half, toSlot);
            !err.isEmpty())
            return fail(err);
        work.append({classId, toSlot, src.half, src.courseId});
        if (const QString err = placementError(work, classId, dst.courseId, dst.half, fromSlot);
            !err.isEmpty())
            return fail(err);
        // 执行对调
        after.erase(std::remove_if(after.begin(), after.end(),
                                   [&](const sched::ScheduleAssignment &a) {
                                       return a.classId == classId && a.slotIndex == toSlot
                                              && halfOverlap(a.half, src.half);
                                   }),
                    after.end());
        for (const auto &m : members)
            after.append({m.first, toSlot, src.half, m.second});
        after.append({classId, fromSlot, src.half, dst.courseId});
        pushCommand(QStringLiteral("对调课程"), after);
        return {{QStringLiteral("ok"), true}, {QStringLiteral("reason"), QString()}};
    }

    if (const QString err = placementError(after, classId, src.courseId, src.half, toSlot);
        !err.isEmpty())
        return fail(err);
    for (const auto &m : members)
        after.append({m.first, toSlot, src.half, m.second});
    pushCommand(QStringLiteral("移动课程"), after);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("reason"), QString()}};
}

QVariantMap AdjustController::unscheduleCell(qint64 classId, int slot, int half)
{
    const CellContent src = cellAt(classId, slot, half);
    if (!src.found)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("reason"), QStringLiteral("该格没有课程")}};
    if (const QString err = moveLockError(classId, slot, src.courseId); !err.isEmpty())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("reason"), err}};

    const auto members = combinedMembers(src.classId, src.courseId);
    QVector<sched::ScheduleAssignment> after = m_app->school()->schedule.assignments;
    after.erase(std::remove_if(after.begin(), after.end(), [&](const sched::ScheduleAssignment &a) {
                    if (a.slotIndex != slot || !halfOverlap(a.half, half))
                        return false;
                    for (const auto &m : members) {
                        if (a.classId == m.first && a.courseId == m.second)
                            return true;
                    }
                    return false;
                }),
                after.end());
    pushCommand(QStringLiteral("清除课程"), after);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("reason"), QString()}};
}

QVariantList AdjustController::unplacedCourses(qint64 classId) const
{
    QVariantList out;
    const sched::School *s = m_app->school();
    for (const auto &e : s->planEntries) {
        if (e.classId != classId)
            continue;
        // 合班课：以组内任一班的已排节数为准（同组同 slot）
        qint64 countClass = classId;
        const auto members = combinedMembers(classId, e.courseId);
        if (members.size() > 1)
            countClass = members.first().first;
        const double missing = e.weeklyPeriods - placedPeriods(countClass, e.courseId);
        if (missing > 1e-6) {
            const sched::Course *co = s->findCourse(e.courseId);
            out.append(QVariantMap{{QStringLiteral("courseId"), e.courseId},
                                   {QStringLiteral("name"),
                                    co ? co->name : QStringLiteral("?")},
                                   {QStringLiteral("missing"), missing}});
        }
    }
    return out;
}

QVariantMap AdjustController::placeUnplaced(qint64 classId, qint64 courseId, int slot, int half)
{
    auto fail = [](const QString &reason) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("reason"), reason}};
    };
    if (!m_temporary) {
        // 计划节数限制：剩余课时必须够
        double weekly = 0;
        bool hasEntry = false;
        for (const auto &e : m_app->school()->planEntries) {
            if (e.classId == classId && e.courseId == courseId) {
                weekly = e.weeklyPeriods;
                hasEntry = true;
                break;
            }
        }
        if (!hasEntry)
            return fail(QStringLiteral("该班计划中没有这门课"));
        const double missing = weekly - placedPeriods(classId, courseId);
        if (missing < (half == 0 ? 1.0 : 0.5) - 1e-6)
            return fail(QStringLiteral("该课程节数已排满"));
    }
    QVector<sched::ScheduleAssignment> after = m_app->school()->schedule.assignments;
    if (const QString err = placementError(after, classId, courseId, half, slot); !err.isEmpty())
        return fail(err);
    const auto members = combinedMembers(classId, courseId);
    for (const auto &m : members)
        after.append({m.first, slot, half, m.second});
    pushCommand(m_temporary ? QStringLiteral("临时调课安排") : QStringLiteral("安排未排课"), after);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("reason"), QString()}};
}

QVariantList AdjustController::placeTargetsFor(qint64 classId, qint64 courseId, int half) const
{
    QVariantList out;
    const sched::School *s = m_app->school();
    const int sc = s->timeConfig.slotCount();
    for (int slot = 0; slot < sc; ++slot) {
        out.append(placementError(s->schedule.assignments, classId, courseId, half, slot).isEmpty()
                       ? 1
                       : 0);
    }
    return out;
}

QVariantList AdjustController::cellIssues(qint64 classId) const
{
    QVariantList out;
    const sched::School *s = m_app->school();
    const int ppd = s->timeConfig.periodsPerDay;
    auto warn = [&out](const QString &text) {
        out.append(QVariantMap{{QStringLiteral("text"), text}});
    };
    for (const auto &e : s->planEntries) {
        if (e.classId != classId)
            continue;
        const sched::Course *co = s->findCourse(e.courseId);
        const QString cname = co ? co->name : QStringLiteral("?");
        // 连课不连：应连课的课程在同天无 chainLength 个连续节
        if (e.chainedCount > 0) {
            QVector<int> slots;
            for (const auto &a : s->schedule.assignments) {
                if (a.classId == classId && a.courseId == e.courseId)
                    slots.append(a.slotIndex);
            }
            std::sort(slots.begin(), slots.end());
            bool hasChain = false;
            for (int i = 0; i + e.chainLength - 1 < slots.size() && !hasChain; ++i) {
                bool ok = true;
                for (int k = 1; k < e.chainLength; ++k) {
                    if (slots[i + k] != slots[i] + k) {
                        ok = false;
                        break;
                    }
                }
                if (ok)
                    hasChain = true;
            }
            if (!slots.isEmpty() && !hasChain)
                warn(QStringLiteral("%1：连课未连排").arg(cname));
        }
        // 课时不匀：同一天 2 节以上同课（连课自身除外也算提示）
        QHash<int, int> perDay;
        for (const auto &a : s->schedule.assignments) {
            if (a.classId == classId && a.courseId == e.courseId)
                perDay[a.slotIndex / ppd]++;
        }
        for (auto it = perDay.begin(); it != perDay.end(); ++it) {
            if (it.value() > e.chainLength && it.value() >= 2)
                warn(QStringLiteral("%1：同一天 %2 节，分布不匀").arg(cname).arg(it.value()));
        }
        // 落入不排节次
        for (const auto &c : s->noScheduleConstraints) {
            if (c.entityType == sched::NoScheduleConstraint::EntityType::Class
                && c.entityId == classId) {
                for (const auto &a : s->schedule.assignments) {
                    if (a.classId == classId && c.slots.contains(a.slotIndex)) {
                        if (const sched::Course *c2 = s->findCourse(a.courseId))
                            warn(QStringLiteral("%1 排在班级不排节次（%2）")
                                     .arg(c2->name)
                                     .arg(a.slotIndex));
                    }
                }
            }
        }
    }
    return out;
}

QStringList AdjustController::teacherBusyText(qint64 classId, int slot) const
{
    QStringList out;
    const sched::School *s = m_app->school();
    const CellContent cell = cellAt(classId, slot, 0);
    if (!cell.found)
        return out;
    QVector<qint64> tids;
    for (const auto &e : s->planEntries) {
        if (e.classId == classId && e.courseId == cell.courseId) {
            tids = e.teacherIds;
            break;
        }
    }
    const int ppd = s->timeConfig.periodsPerDay;
    const QStringList dayNames = {QStringLiteral("周一"), QStringLiteral("周二"), QStringLiteral("周三"),
                                  QStringLiteral("周四"), QStringLiteral("周五"), QStringLiteral("周六"),
                                  QStringLiteral("周日")};
    for (qint64 tid : tids) {
        const sched::Teacher *t = s->findTeacher(tid);
        if (!t)
            continue;
        QStringList parts;
        for (const auto &a : s->schedule.assignments) {
            if (a.slotIndex == slot)
                continue; // 不含当前格
            for (const auto &e : s->planEntries) {
                if (e.classId == a.classId && e.courseId == a.courseId
                    && e.teacherIds.contains(tid)) {
                    parts.append(QStringLiteral("%1第%2节")
                                     .arg(dayNames[a.slotIndex / ppd])
                                     .arg(a.slotIndex % ppd + 1));
                    break;
                }
            }
        }
        for (const auto &act : s->teacherActivities) {
            if (act.teacherId == tid)
                parts.append(QStringLiteral("%1第%2节(活动)")
                                 .arg(dayNames[act.slotIndex / ppd])
                                 .arg(act.slotIndex % ppd + 1));
        }
        out.append(parts.isEmpty() ? QStringLiteral("%1：当周暂无其他课").arg(t->name)
                                   : QStringLiteral("%1：%2").arg(t->name, parts.join(QStringLiteral("、"))));
    }
    return out;
}

// ==================== 缓冲节次 ====================

QVariantMap AdjustController::bufferStore(qint64 classId, int slot, int half)
{
    if (!m_buffer.isEmpty())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("reason"), QStringLiteral("暂存区已有课程，请先取出")}};
    const CellContent src = cellAt(classId, slot, half);
    if (!src.found)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("reason"), QStringLiteral("该格没有课程")}};
    if (combinedMembers(src.classId, src.courseId).size() > 1)
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("reason"), QStringLiteral("合班课不能放入暂存区")}};
    const QVariantMap res = unscheduleCell(classId, slot, half);
    if (!res.value(QStringLiteral("ok")).toBool())
        return res;
    const sched::Course *co = m_app->school()->findCourse(src.courseId);
    m_buffer = {{QStringLiteral("classId"), src.classId},
                {QStringLiteral("courseId"), src.courseId},
                {QStringLiteral("half"), src.half},
                {QStringLiteral("text"), co ? co->name : QStringLiteral("?")}};
    Q_EMIT bufferChanged();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("reason"), QString()}};
}

QVariantMap AdjustController::bufferPlace(int toSlot, int toHalf)
{
    if (m_buffer.isEmpty())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("reason"), QStringLiteral("暂存区是空的")}};
    const qint64 classId = m_buffer.value(QStringLiteral("classId")).toLongLong();
    const qint64 courseId = m_buffer.value(QStringLiteral("courseId")).toLongLong();
    int half = m_buffer.value(QStringLiteral("half")).toInt();
    if (toHalf != 0) // 允许显式指定
        half = toHalf;
    // 暂存放置走 placementError，但不受计划节数限制（它本来就是已排的课）
    QVector<sched::ScheduleAssignment> after = m_app->school()->schedule.assignments;
    if (const QString err = placementError(after, classId, courseId, half, toSlot); !err.isEmpty())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("reason"), err}};
    after.append({classId, toSlot, half, courseId});
    pushCommand(QStringLiteral("从暂存区取出"), after);
    m_buffer = {};
    Q_EMIT bufferChanged();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("reason"), QString()}};
}

void AdjustController::bufferClear()
{
    m_buffer = {};
    Q_EMIT bufferChanged();
}

// ==================== 临时调课 ====================

void AdjustController::enterTemporary()
{
    if (m_temporary)
        return;
    m_tempSnapshot = m_app->school()->schedule.assignments;
    m_undo.clear(); // 模式边界清空撤销栈
    m_temporary = true;
    Q_EMIT temporaryModeChanged();
    Q_EMIT undoStackChanged();
}

void AdjustController::exitTemporary()
{
    if (!m_temporary)
        return;
    m_app->school()->schedule.assignments = m_tempSnapshot; // 丢弃全部临时改动
    m_tempSnapshot.clear();
    m_undo.clear();
    m_temporary = false;
    m_buffer = {};
    Q_EMIT bufferChanged();
    Q_EMIT temporaryModeChanged();
    Q_EMIT undoStackChanged();
    m_app->notifyScheduleChanged();
}

QString AdjustController::tempNoticeText() const
{
    if (!m_temporary)
        return {};
    const sched::School *s = m_app->school();
    const auto &cur = s->schedule.assignments;
    const auto &old = m_tempSnapshot;
    const int ppd = s->timeConfig.periodsPerDay;
    const QStringList dayNames = {QStringLiteral("周一"), QStringLiteral("周二"), QStringLiteral("周三"),
                                  QStringLiteral("周四"), QStringLiteral("周五"), QStringLiteral("周六"),
                                  QStringLiteral("周日")};
    auto slotName = [&](int slot) {
        return QStringLiteral("%1第%2节").arg(dayNames[slot / ppd]).arg(slot % ppd + 1);
    };
    auto in = [](const QVector<sched::ScheduleAssignment> &v, const sched::ScheduleAssignment &a) {
        return v.contains(a);
    };

    // 按班聚合变化
    QStringList lines;
    lines.append(QStringLiteral("【调课通知】%1").arg(s->name));
    for (const auto &c : s->classes) {
        QStringList added, removed;
        for (const auto &a : cur) {
            if (a.classId == c.id && !in(old, a)) {
                const sched::Course *co = s->findCourse(a.courseId);
                added.append(QStringLiteral("%1 %2").arg(slotName(a.slotIndex),
                                                         co ? co->name : QStringLiteral("?")));
            }
        }
        for (const auto &a : old) {
            if (a.classId == c.id && !in(cur, a)) {
                const sched::Course *co = s->findCourse(a.courseId);
                removed.append(QStringLiteral("%1 %2").arg(slotName(a.slotIndex),
                                                           co ? co->name : QStringLiteral("?")));
            }
        }
        if (added.isEmpty() && removed.isEmpty())
            continue;
        lines.append(QStringLiteral("■ %1").arg(c.name));
        for (const auto &r : removed)
            lines.append(QStringLiteral("  取消：%1").arg(r));
        for (const auto &a : added)
            lines.append(QStringLiteral("  调整为：%1").arg(a));
    }
    if (lines.size() == 1)
        lines.append(QStringLiteral("（本次临时调课无变化）"));
    return lines.join(QStringLiteral("\n"));
}

bool AdjustController::exportTempNotice(const QString &pathOrUrl)
{
    QString path = pathOrUrl;
    if (path.startsWith(QLatin1StringView("file:")))
        path = QUrl(path).toLocalFile();
    const QString text = tempNoticeText();
    if (text.isEmpty()) {
        m_exportError = QStringLiteral("当前不在临时调课模式");
        return false;
    }
    if (path.endsWith(QLatin1StringView(".pdf"), Qt::CaseInsensitive)) {
        QPdfWriter pdf(path);
        pdf.setPageSize(QPageSize(QPageSize::A4));
        QPainter painter(&pdf);
        if (!painter.isActive()) {
            m_exportError = QStringLiteral("无法创建 PDF 文件");
            return false;
        }
        QFont font = painter.font();
        font.setPointSize(12);
        painter.setFont(font);
        int y = 60;
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            painter.drawText(60, y, line);
            y += 32;
            if (y > pdf.height() - 60) {
                pdf.newPage();
                y = 60;
            }
        }
        return true;
    }
    if (!path.endsWith(QLatin1StringView(".txt"), Qt::CaseInsensitive))
        path += QStringLiteral(".txt");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_exportError = f.errorString();
        return false;
    }
    QTextStream ts(&f);
    ts << text;
    return true;
}

// ==================== 撤销栈 ====================

void AdjustController::pushCommand(const QString &text,
                                   const QVector<sched::ScheduleAssignment> &after)
{
    m_undo.push(new ScheduleCommand(m_app, text, m_app->school()->schedule.assignments, after,
                                    m_temporary));
}

void AdjustController::undo() { m_undo.undo(); }
void AdjustController::redo() { m_undo.redo(); }
