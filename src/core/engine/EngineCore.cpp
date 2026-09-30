#include "EngineCore.h"

#include <algorithm>
#include <cmath>

namespace sched::engine {

namespace {

// 软约束权重
constexpr double W_PROGRESS = 5.0; // 进度一致（同老师同课相邻节次）
constexpr double W_EVEN_SPREAD = 3.0; // 平均分布课程同节次班级数
constexpr double W_AMPM = 2.0; // 课程上下午倾向
constexpr double W_MAJOR_AM = 1.0; // 主课优先上午
constexpr double W_DAILY_MAX = 4.0; // 老师日课时超限
constexpr double W_DAILY_BALANCE = 0.25; // 老师日课时均匀
constexpr double W_CLASS_COURSE_SPREAD = 2.0; // 同班同课周内分布
constexpr double W_CHAIN_CROSS = 1.0; // 连课跨上午/下午边界
constexpr double W_PLAN_MORNING = 1.5; // 计划条目「上午节数」不足
constexpr double W_PLAN_EARLY = 1.5; // 计划条目落到「前 N 节」之外
constexpr double HARD_WEIGHT = 1000.0; // 搜索中硬违例权重

} // namespace

// ==================== Problem ====================

Problem::Problem(const School &s)
    : school(s)
{
    const int sc = slotCount();
    const std::array<int, 2> zero{0, 0};

    for (int i = 0; i < s.classes.size(); ++i)
        m_classIdx[s.classes[i].id] = i;
    for (int i = 0; i < s.teachers.size(); ++i)
        m_teacherIdx[s.teachers[i].id] = i;
    for (int i = 0; i < s.courses.size(); ++i)
        m_courseIdx[s.courses[i].id] = i;
    for (int i = 0; i < s.classrooms.size(); ++i)
        m_roomIdx[s.classrooms[i].id] = i;

    m_classForbidden.resize(s.classes.size());
    m_teacherForbidden.resize(s.teachers.size());
    m_courseForbidden.resize(s.courses.size());
    m_roomForbidden.resize(s.classrooms.size());
    for (const auto &c : s.noScheduleConstraints) {
        using ET = NoScheduleConstraint::EntityType;
        switch (c.entityType) {
        case ET::Class:
            if (m_classIdx.contains(c.entityId))
                m_classForbidden[m_classIdx[c.entityId]].unite(c.slots);
            break;
        case ET::Course:
            if (m_courseIdx.contains(c.entityId))
                m_courseForbidden[m_courseIdx[c.entityId]].unite(c.slots);
            break;
        case ET::Teacher:
            if (m_teacherIdx.contains(c.entityId))
                m_teacherForbidden[m_teacherIdx[c.entityId]].unite(c.slots);
            break;
        case ET::Classroom:
            if (m_roomIdx.contains(c.entityId))
                m_roomForbidden[m_roomIdx[c.entityId]].unite(c.slots);
            break;
        }
    }

    m_classOcc.assign(s.classes.size(), QVector<std::array<int, 2>>(sc, zero));
    m_teacherOcc.assign(s.teachers.size(), QVector<std::array<int, 2>>(sc, zero));
    m_roomOcc.assign(s.classrooms.size(), QVector<std::array<int, 2>>(sc, zero));
    m_courseClsOcc.assign(s.courses.size(), QVector<std::array<int, 2>>(sc, zero));

    // 老师活动时间预置为占用（两个 parity 都占）
    for (const auto &a : s.teacherActivities) {
        if (!m_teacherIdx.contains(a.teacherId) || !s.timeConfig.isValidSlot(a.slotIndex))
            continue;
        const int ti = m_teacherIdx[a.teacherId];
        m_teacherOcc[ti][a.slotIndex][0]++;
        m_teacherOcc[ti][a.slotIndex][1]++;
    }

    expandSessions();
    mergeCombined();
    applyPreAssignments();
    placements.resize(sessions.size());
}

Session Problem::makeSession(const PlanEntry &e, int length, bool half) const
{
    Session s;
    s.entryId = e.id;
    s.classIds = {e.classId};
    s.courseId = e.courseId;
    s.teacherIds = e.teacherIds;
    s.classroomId = e.classroomId;
    s.length = length;
    s.halfSession = half;
    s.progressGroup = e.progressGroup;
    return s;
}

void Problem::expandSessions()
{
    for (const PlanEntry &e : school.planEntries) {
        const int whole = static_cast<int>(e.weeklyPeriods + 1e-6);
        const bool hasHalf = (e.weeklyPeriods - whole) > 0.25; // 0.5 尾数 = 单双周
        const int chainLen = std::max(1, e.chainLength);
        int chains = e.chainedCount;
        if (chains * chainLen > whole)
            chains = chainLen > 0 ? whole / chainLen : 0; // 连课不超过总节数
        const int singles = whole - chains * chainLen;
        for (int i = 0; i < chains; ++i)
            sessions.append(makeSession(e, chainLen, false));
        for (int i = 0; i < singles; ++i)
            sessions.append(makeSession(e, 1, false));
        if (hasHalf)
            sessions.append(makeSession(e, 1, true));
    }
}

void Problem::mergeCombined()
{
    QVector<bool> consumed(sessions.size(), false);
    QVector<Session> merged;

    for (const CombinedGroup &cg : school.combinedGroups) {
        // 每个成员（班,课）的未消费会话索引，长的在前以便对齐
        QVector<QVector<int>> memberLists;
        for (const auto &m : cg.entries) {
            QVector<int> list;
            for (int i = 0; i < sessions.size(); ++i) {
                const Session &s = sessions[i];
                if (!consumed[i] && s.classIds.size() == 1 && s.classIds.first() == m.first
                    && s.courseId == m.second)
                    list.append(i);
            }
            std::sort(list.begin(), list.end(),
                      [&](int a, int b) { return sessions[a].length > sessions[b].length; });
            memberLists.append(list);
        }
        // 逐对合并：同组合班会话同 slot 同老师，只占老师一次
        while (true) {
            bool allNonEmpty = !memberLists.isEmpty();
            for (const auto &l : memberLists)
                allNonEmpty = allNonEmpty && !l.isEmpty();
            if (!allNonEmpty)
                break;
            Session m;
            m.entryId = -1;
            m.courseId = cg.entries.first().second;
            bool firstMember = true;
            for (auto &l : memberLists) {
                const int idx = l.takeFirst();
                consumed[idx] = true;
                const Session &s = sessions[idx];
                m.classIds.append(s.classIds.first());
                for (qint64 t : s.teacherIds) {
                    if (!m.teacherIds.contains(t))
                        m.teacherIds.append(t);
                }
                if (m.classroomId < 0)
                    m.classroomId = s.classroomId;
                m.length = std::max(m.length, s.length);
                m.halfSession = m.halfSession || s.halfSession;
                if (firstMember) {
                    m.progressGroup = s.progressGroup; // 合班会话取首个成员的进度组
                    firstMember = false;
                }
            }
            merged.append(m);
        }
    }

    QVector<Session> kept;
    kept.reserve(sessions.size() - merged.size());
    for (int i = 0; i < sessions.size(); ++i) {
        if (!consumed[i])
            kept.append(sessions[i]);
    }
    kept.append(merged);
    sessions = kept;
}

void Problem::applyPreAssignments()
{
    for (const PreAssignment &p : school.preAssignments) {
        int found = -1;
        for (int i = 0; i < sessions.size(); ++i) {
            const Session &s = sessions[i];
            if (!s.fixed && s.courseId == p.courseId && s.classIds.contains(p.classId)) {
                found = i;
                break;
            }
        }
        if (found >= 0) {
            sessions[found].fixed = true;
            sessions[found].fixedSlot = p.slotIndex;
            sessions[found].fixedHalf = p.half;
        } else {
            // 无对应计划的预排：生成临时固定会话
            Session s;
            s.classIds = {p.classId};
            s.courseId = p.courseId;
            for (const PlanEntry &e : school.planEntries) {
                if (e.classId == p.classId && e.courseId == p.courseId) {
                    s.teacherIds = e.teacherIds;
                    s.classroomId = e.classroomId;
                    break;
                }
            }
            s.fixed = true;
            s.fixedSlot = p.slotIndex;
            s.fixedHalf = p.half;
            if (p.half != 0)
                s.halfSession = true; // 只占半周（未配计划时的临时会话）
            sessions.append(s);
        }
    }
}

QVector<int> Problem::coveredSlots(int s, int startSlot) const
{
    QVector<int> out;
    out.reserve(sessions[s].length);
    for (int i = 0; i < sessions[s].length; ++i) {
        const int sl = startSlot + i;
        if (sl >= 0 && sl < slotCount())
            out.append(sl);
    }
    return out;
}

void Problem::apply(int s, int slot, int half)
{
    placements[s] = {slot, half};
    if (slot < 0)
        return;
    const Session &ss = sessions[s];
    const int p0 = (half == 2) ? 1 : 0;
    const int p1 = (half == 1) ? 0 : 1;
    const int nCls = ss.classIds.size();
    const bool hasCourse = m_courseIdx.contains(ss.courseId);
    for (int sl : coveredSlots(s, slot)) {
        for (int p = p0; p <= p1; ++p) {
            for (qint64 c : ss.classIds) {
                if (m_classIdx.contains(c))
                    m_classOcc[m_classIdx[c]][sl][p]++;
            }
            for (qint64 t : ss.teacherIds) {
                if (m_teacherIdx.contains(t))
                    m_teacherOcc[m_teacherIdx[t]][sl][p]++;
            }
            if (ss.classroomId >= 0 && m_roomIdx.contains(ss.classroomId))
                m_roomOcc[m_roomIdx[ss.classroomId]][sl][p]++;
            if (hasCourse)
                m_courseClsOcc[m_courseIdx[ss.courseId]][sl][p] += nCls;
        }
    }
}

void Problem::remove(int s)
{
    const Placement cur = placements[s];
    placements[s] = {-1, 0};
    if (cur.slot < 0)
        return;
    const Session &ss = sessions[s];
    const int p0 = (cur.half == 2) ? 1 : 0;
    const int p1 = (cur.half == 1) ? 0 : 1;
    const int nCls = ss.classIds.size();
    const bool hasCourse = m_courseIdx.contains(ss.courseId);
    for (int sl : coveredSlots(s, cur.slot)) {
        for (int p = p0; p <= p1; ++p) {
            for (qint64 c : ss.classIds) {
                if (m_classIdx.contains(c))
                    m_classOcc[m_classIdx[c]][sl][p]--;
            }
            for (qint64 t : ss.teacherIds) {
                if (m_teacherIdx.contains(t))
                    m_teacherOcc[m_teacherIdx[t]][sl][p]--;
            }
            if (ss.classroomId >= 0 && m_roomIdx.contains(ss.classroomId))
                m_roomOcc[m_roomIdx[ss.classroomId]][sl][p]--;
            if (hasCourse)
                m_courseClsOcc[m_courseIdx[ss.courseId]][sl][p] -= nCls;
        }
    }
}

int Problem::hardAt(int s, int slot, int half) const
{
    if (slot < 0 || slot >= slotCount())
        return kIllegal;
    const Session &ss = sessions[s];
    const int ppd = periodsPerDay();
    const int day = slot / ppd;
    const int p0 = (half == 2) ? 1 : 0;
    const int p1 = (half == 1) ? 0 : 1;

    int conflicts = 0;
    for (int i = 0; i < ss.length; ++i) {
        const int sl = slot + i;
        if (sl >= slotCount() || sl / ppd != day)
            return kIllegal; // 连课跨天，位置非法
        for (int p = p0; p <= p1; ++p) {
            for (qint64 c : ss.classIds) {
                if (!m_classIdx.contains(c))
                    continue;
                const int ci = m_classIdx[c];
                if (m_classForbidden[ci].contains(sl))
                    conflicts++;
                conflicts += m_classOcc[ci][sl][p];
            }
            if (m_courseIdx.contains(ss.courseId)) {
                if (m_courseForbidden[m_courseIdx[ss.courseId]].contains(sl))
                    conflicts++;
            }
            for (qint64 t : ss.teacherIds) {
                if (!m_teacherIdx.contains(t))
                    continue;
                const int ti = m_teacherIdx[t];
                if (m_teacherForbidden[ti].contains(sl))
                    conflicts++;
                conflicts += m_teacherOcc[ti][sl][p];
                // 互斥组：同组其他老师同 slot 也不得占用
                const qint64 mg = school.teachers[ti].mutualExclusionGroup;
                if (mg >= 0) {
                    for (int t2 = 0; t2 < school.teachers.size(); ++t2) {
                        if (t2 != ti && school.teachers[t2].mutualExclusionGroup == mg)
                            conflicts += m_teacherOcc[t2][sl][p];
                    }
                }
            }
            if (ss.classroomId >= 0 && m_roomIdx.contains(ss.classroomId)) {
                const int ri = m_roomIdx[ss.classroomId];
                if (m_roomForbidden[ri].contains(sl))
                    conflicts++;
                const int capacity = school.classrooms[ri].capacity;
                if (m_roomOcc[ri][sl][p] >= capacity)
                    conflicts++; // 放入后超容量
            }
        }
    }
    return conflicts;
}

int Problem::hardViolations() const
{
    int v = 0;
    const int sc = slotCount();
    for (int i = 0; i < school.classes.size(); ++i)
        for (int sl = 0; sl < sc; ++sl)
            for (int p = 0; p < 2; ++p)
                v += std::max(0, m_classOcc[i][sl][p] - 1);
    for (int i = 0; i < school.teachers.size(); ++i)
        for (int sl = 0; sl < sc; ++sl)
            for (int p = 0; p < 2; ++p)
                v += std::max(0, m_teacherOcc[i][sl][p] - 1);
    // 互斥组（按老师出现顺序枚举组，避免哈希序不确定）
    QVector<qint64> mutexGroups;
    for (const Teacher &t : school.teachers) {
        if (t.mutualExclusionGroup >= 0 && !mutexGroups.contains(t.mutualExclusionGroup))
            mutexGroups.append(t.mutualExclusionGroup);
    }
    for (qint64 mg : mutexGroups) {
        for (int sl = 0; sl < sc; ++sl) {
            for (int p = 0; p < 2; ++p) {
                int cnt = 0;
                for (int i = 0; i < school.teachers.size(); ++i) {
                    if (school.teachers[i].mutualExclusionGroup == mg
                        && m_teacherOcc[i][sl][p] > 0)
                        cnt++;
                }
                v += std::max(0, cnt - 1);
            }
        }
    }
    for (int i = 0; i < school.classrooms.size(); ++i)
        for (int sl = 0; sl < sc; ++sl)
            for (int p = 0; p < 2; ++p)
                v += std::max(0, m_roomOcc[i][sl][p] - school.classrooms[i].capacity);
    // 不排时间（老师活动已折算进占用表）
    for (int s = 0; s < sessions.size(); ++s) {
        if (!isPlaced(s))
            continue;
        const Session &ss = sessions[s];
        for (int sl : coveredSlots(s, placements[s].slot)) {
            for (qint64 c : ss.classIds) {
                if (m_classIdx.contains(c) && m_classForbidden[m_classIdx[c]].contains(sl))
                    v++;
            }
            if (m_courseIdx.contains(ss.courseId)
                && m_courseForbidden[m_courseIdx[ss.courseId]].contains(sl))
                v++;
            for (qint64 t : ss.teacherIds) {
                if (m_teacherIdx.contains(t) && m_teacherForbidden[m_teacherIdx[t]].contains(sl))
                    v++;
            }
            if (ss.classroomId >= 0 && m_roomIdx.contains(ss.classroomId)
                && m_roomForbidden[m_roomIdx[ss.classroomId]].contains(sl))
                v++;
        }
    }
    // 未排会话
    for (int s = 0; s < sessions.size(); ++s)
        if (!isPlaced(s))
            v++;
    return v;
}

// ---- 软约束分项 ----

double Problem::progressAlignCost() const
{
    // 同老师同课同进度组的各班会话，首 slot 尽量相邻（老软件"进度一致"核心指标）
    // progressGroup >= 0 的条目按组对齐；-1 未分组的自动归入同老师同课默认组
    double cost = 0;
    struct SeenKey {
        qint64 teacher;
        qint64 course;
        int group;
    };
    QVector<SeenKey> seen;
    for (int s = 0; s < sessions.size(); ++s) {
        for (qint64 t : sessions[s].teacherIds) {
            const SeenKey key{t, sessions[s].courseId, sessions[s].progressGroup};
            if (std::any_of(seen.begin(), seen.end(), [&](const SeenKey &k) {
                    return k.teacher == key.teacher && k.course == key.course && k.group == key.group;
                }))
                continue;
            seen.append(key);
            QVector<int> slots;
            for (int i = 0; i < sessions.size(); ++i) {
                if (isPlaced(i) && sessions[i].courseId == key.course
                    && sessions[i].progressGroup == key.group && sessions[i].teacherIds.contains(t))
                    slots.append(placements[i].slot);
            }
            std::sort(slots.begin(), slots.end());
            slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
            for (int i = 1; i < slots.size(); ++i)
                cost += std::max(0, slots[i] - slots[i - 1] - 1);
        }
    }
    return W_PROGRESS * cost;
}

double Problem::evenSpreadCost() const
{
    double cost = 0;
    for (int ci = 0; ci < school.courses.size(); ++ci) {
        if (!school.courses[ci].evenSpread)
            continue;
        for (const auto &perSlot : m_courseClsOcc[ci]) {
            for (int p = 0; p < 2; ++p) {
                const int n = perSlot[p];
                cost += double(n) * (n - 1) / 2.0;
            }
        }
    }
    return W_EVEN_SPREAD * cost;
}

double Problem::amPmCost() const
{
    double cost = 0;
    const int ppd = periodsPerDay();
    const int morning = school.timeConfig.morningPeriods;
    for (int s = 0; s < sessions.size(); ++s) {
        if (!isPlaced(s) || !m_courseIdx.contains(sessions[s].courseId))
            continue;
        const Course &c = school.courses[m_courseIdx[sessions[s].courseId]];
        for (int sl : coveredSlots(s, placements[s].slot)) {
            const int period = sl % ppd;
            if (c.amPmPolicy == AmPmPolicy::Morning && period >= morning)
                cost += 1;
            else if (c.amPmPolicy == AmPmPolicy::Afternoon && period < morning)
                cost += 1;
        }
    }
    return W_AMPM * cost;
}

double Problem::majorMorningCost() const
{
    double cost = 0;
    const int ppd = periodsPerDay();
    const int morning = school.timeConfig.morningPeriods;
    for (int s = 0; s < sessions.size(); ++s) {
        if (!isPlaced(s) || !m_courseIdx.contains(sessions[s].courseId))
            continue;
        const Course &c = school.courses[m_courseIdx[sessions[s].courseId]];
        if (!c.isMajor)
            continue;
        for (int sl : coveredSlots(s, placements[s].slot)) {
            if (sl % ppd >= morning)
                cost += 1;
        }
    }
    return W_MAJOR_AM * cost;
}

double Problem::teacherDailyCost() const
{
    double cost = 0;
    const int ppd = periodsPerDay();
    const int days = school.timeConfig.daysPerWeek;
    for (int ti = 0; ti < school.teachers.size(); ++ti) {
        QVector<int> perDay(days, 0);
        for (int s = 0; s < sessions.size(); ++s) {
            if (!isPlaced(s) || !sessions[s].teacherIds.contains(school.teachers[ti].id))
                continue;
            for (int sl : coveredSlots(s, placements[s].slot))
                perDay[sl / ppd]++;
        }
        const int maxPerDay = school.teachers[ti].maxPeriodsPerDay;
        double sq = 0;
        for (int d = 0; d < days; ++d) {
            if (maxPerDay >= 0 && perDay[d] > maxPerDay)
                cost += W_DAILY_MAX * (perDay[d] - maxPerDay);
            sq += double(perDay[d]) * perDay[d];
        }
        cost += W_DAILY_BALANCE * sq;
    }
    return cost;
}

double Problem::classCourseSpreadCost() const
{
    // 同班同课的不同会话不挤在同一天（连课自身算一次）
    double cost = 0;
    const int ppd = periodsPerDay();
    QVector<QPair<qint64, qint64>> seen;
    for (int s = 0; s < sessions.size(); ++s) {
        for (qint64 c : sessions[s].classIds) {
            const QPair<qint64, qint64> key{c, sessions[s].courseId};
            if (seen.contains(key))
                continue;
            seen.append(key);
            QHash<int, int> perDay;
            for (int i = 0; i < sessions.size(); ++i) {
                if (isPlaced(i) && sessions[i].courseId == key.second
                    && sessions[i].classIds.contains(c))
                    perDay[placements[i].slot / ppd]++;
            }
            for (auto it = perDay.begin(); it != perDay.end(); ++it)
                cost += double(it.value()) * (it.value() - 1) / 2.0;
        }
    }
    return W_CLASS_COURSE_SPREAD * cost;
}

double Problem::chainCrossCost() const
{
    double cost = 0;
    const int ppd = periodsPerDay();
    const int morning = school.timeConfig.morningPeriods;
    for (int s = 0; s < sessions.size(); ++s) {
        if (!isPlaced(s) || sessions[s].length < 2)
            continue;
        const int start = placements[s].slot % ppd;
        // 上午节数足够却仍跨过午界 → 罚
        if (start < morning && start + sessions[s].length - 1 >= morning
            && morning >= sessions[s].length)
            cost += 1;
    }
    return W_CHAIN_CROSS * cost;
}

double Problem::localSoft(int s) const
{
    // 与 s 相关的聚合键重算（移动单会话时这些键之外的分项不变，可作精确增量）
    double cost = 0;
    const Session &ss = sessions[s];

    // 进度一致：s 的每个 (老师,课程,进度组)
    const int ppd = periodsPerDay();
    for (qint64 t : ss.teacherIds) {
        QVector<int> slots;
        for (int i = 0; i < sessions.size(); ++i) {
            if (isPlaced(i) && sessions[i].courseId == ss.courseId
                && sessions[i].progressGroup == ss.progressGroup
                && sessions[i].teacherIds.contains(t))
                slots.append(placements[i].slot);
        }
        std::sort(slots.begin(), slots.end());
        slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
        double c = 0;
        for (int i = 1; i < slots.size(); ++i)
            c += std::max(0, slots[i] - slots[i - 1] - 1);
        cost += W_PROGRESS * c;
    }

    // 平均分布：s 的课程整体（读增量维护的课程-节次班级数表）
    if (m_courseIdx.contains(ss.courseId)
        && school.courses[m_courseIdx[ss.courseId]].evenSpread) {
        double c = 0;
        for (const auto &perSlot : m_courseClsOcc[m_courseIdx[ss.courseId]]) {
            for (int p = 0; p < 2; ++p) {
                const int n = perSlot[p];
                c += double(n) * (n - 1) / 2.0;
            }
        }
        cost += W_EVEN_SPREAD * c;
    }

    // 上下午 / 主课 / 连课跨界：仅 s 自身位置
    if (isPlaced(s) && m_courseIdx.contains(ss.courseId)) {
        const Course &c = school.courses[m_courseIdx[ss.courseId]];
        const int morning = school.timeConfig.morningPeriods;
        for (int sl : coveredSlots(s, placements[s].slot)) {
            const int period = sl % ppd;
            if (c.amPmPolicy == AmPmPolicy::Morning && period >= morning)
                cost += W_AMPM;
            else if (c.amPmPolicy == AmPmPolicy::Afternoon && period < morning)
                cost += W_AMPM;
            if (c.isMajor && period >= morning)
                cost += W_MAJOR_AM;
        }
        if (ss.length >= 2) {
            const int start = placements[s].slot % ppd;
            if (start < morning && start + ss.length - 1 >= morning && morning >= ss.length)
                cost += W_CHAIN_CROSS;
        }
    }

    // 老师日课时：s 的每个老师整体
    const int days = school.timeConfig.daysPerWeek;
    for (qint64 t : ss.teacherIds) {
        if (!m_teacherIdx.contains(t))
            continue;
        const int ti = m_teacherIdx[t];
        QVector<int> perDay(days, 0);
        for (int i = 0; i < sessions.size(); ++i) {
            if (isPlaced(i) && sessions[i].teacherIds.contains(t)) {
                for (int sl : coveredSlots(i, placements[i].slot))
                    perDay[sl / ppd]++;
            }
        }
        const int maxPerDay = school.teachers[ti].maxPeriodsPerDay;
        double sq = 0;
        for (int d = 0; d < days; ++d) {
            if (maxPerDay >= 0 && perDay[d] > maxPerDay)
                cost += W_DAILY_MAX * (perDay[d] - maxPerDay);
            sq += double(perDay[d]) * perDay[d];
        }
        cost += W_DAILY_BALANCE * sq;
    }

    // 同班同课分布：s 的每个 (班,课程)
    for (qint64 cls : ss.classIds) {
        QHash<int, int> perDay;
        for (int i = 0; i < sessions.size(); ++i) {
            if (isPlaced(i) && sessions[i].courseId == ss.courseId
                && sessions[i].classIds.contains(cls))
                perDay[placements[i].slot / ppd]++;
        }
        for (auto it = perDay.begin(); it != perDay.end(); ++it)
            cost += W_CLASS_COURSE_SPREAD * double(it.value()) * (it.value() - 1) / 2.0;
    }

    // 计划条目的时段要求（上午节数 / 前 N 节）：只与 s 所属条目相关
    if (isPlaced(s) && ss.entryId >= 0) {
        const PlanEntry *entry = nullptr;
        for (const auto &e : school.planEntries) {
            if (e.id == ss.entryId) {
                entry = &e;
                break;
            }
        }
        if (entry) {
            const int morningP = school.timeConfig.morningPeriods;
            if (entry->morningPeriods > 0) {
                int have = 0;
                for (int i = 0; i < sessions.size(); ++i) {
                    if (!isPlaced(i) || sessions[i].entryId != ss.entryId)
                        continue;
                    for (int sl : coveredSlots(i, placements[i].slot)) {
                        if (sl % ppd < morningP)
                            ++have;
                    }
                }
                cost += W_PLAN_MORNING * std::max(0, entry->morningPeriods - have);
            }
            if (entry->earlyPeriods > 0) {
                for (int sl : coveredSlots(s, placements[s].slot)) {
                    if (sl % ppd >= entry->earlyPeriods)
                        cost += W_PLAN_EARLY;
                }
            }
        }
    }

    return cost;
}

double Problem::softScore(QMap<QString, double> *breakdown) const
{
    const double progress = progressAlignCost();
    const double even = evenSpreadCost();
    const double ampm = amPmCost();
    const double major = majorMorningCost();
    const double daily = teacherDailyCost();
    const double spread = classCourseSpreadCost();
    const double chain = chainCrossCost();
    const double planTime = planTimePreferenceCost();
    if (breakdown) {
        breakdown->insert(QStringLiteral("planTimePreference"), planTime);
        breakdown->insert(QStringLiteral("progressAlign"), progress);
        breakdown->insert(QStringLiteral("evenSpread"), even);
        breakdown->insert(QStringLiteral("amPmPolicy"), ampm);
        breakdown->insert(QStringLiteral("majorMorning"), major);
        breakdown->insert(QStringLiteral("teacherDaily"), daily);
        breakdown->insert(QStringLiteral("classCourseSpread"), spread);
        breakdown->insert(QStringLiteral("chainAmPmCross"), chain);
    }
    return progress + even + ampm + major + daily + spread + chain + planTime;
}

// 计划条目的时段要求：morningPeriods = 要求排在上午的节数（数不足记罚分）；
// earlyPeriods = 尽量排在前 N 节内（超出即罚）
double Problem::planTimePreferenceCost() const
{
    const int ppd = periodsPerDay();
    const int morning = school.timeConfig.morningPeriods;
    QHash<qint64, QVector<int>> periodCount; // 条目 → 每个节次的节数
    for (int s = 0; s < sessions.size(); ++s) {
        const Placement &pl = placements[s];
        if (pl.slot < 0)
            continue;
        const Session &ss = sessions[s];
        if (ss.entryId < 0)
            continue; // 临时会话（无计划条目）
        QVector<int> &counts = periodCount[ss.entryId];
        if (counts.isEmpty())
            counts = QVector<int>(ppd, 0);
        const int startPeriod = pl.slot % ppd;
        for (int i = 0; i < std::max(1, ss.length) && startPeriod + i < ppd; ++i)
            counts[startPeriod + i] += 1;
    }
    double cost = 0;
    for (const PlanEntry &e : school.planEntries) {
        const QVector<int> counts = periodCount.value(e.id);
        if (counts.isEmpty())
            continue;
        if (e.morningPeriods > 0) {
            int have = 0;
            for (int period = 0; period < morning && period < counts.size(); ++period)
                have += counts[period];
            cost += W_PLAN_MORNING * std::max(0, e.morningPeriods - have);
        }
        if (e.earlyPeriods > 0) {
            int miss = 0;
            for (int period = e.earlyPeriods; period < counts.size(); ++period)
                miss += counts[period];
            cost += W_PLAN_EARLY * miss;
        }
    }
    return cost;
}

const Course *Problem::courseOf(qint64 courseId) const
{
    return m_courseIdx.contains(courseId) ? &school.courses[m_courseIdx[courseId]] : nullptr;
}

QVector<Placement> Problem::candidatePositions(int s) const
{
    QVector<Placement> out;
    const Session &ss = sessions[s];
    if (ss.fixed) {
        out.append({ss.fixedSlot, ss.fixedHalf});
        return out;
    }
    const int sc = slotCount();
    const int ppd = periodsPerDay();
    for (int slot = 0; slot < sc; ++slot) {
        if (ss.length > 1 && slot % ppd + ss.length > ppd)
            continue; // 连课不跨天
        if (ss.halfSession) {
            out.append({slot, 1});
            out.append({slot, 2});
        } else {
            out.append({slot, 0});
        }
    }
    return out;
}

QualityReport Problem::report() const
{
    QualityReport r;
    r.totalSessions = sessions.size();
    for (int s = 0; s < sessions.size(); ++s)
        if (isPlaced(s))
            r.placedSessions++;
    r.hardViolations = hardViolations();
    r.softScore = softScore(&r.softBreakdown);
    // 未排明细
    for (int s = 0; s < sessions.size(); ++s) {
        if (isPlaced(s))
            continue;
        const Session &ss = sessions[s];
        QStringList classNames;
        for (qint64 c : ss.classIds) {
            if (const ClassInfo *ci = school.findClass(c))
                classNames.append(ci->name);
        }
        const Course *co = school.findCourse(ss.courseId);
        QString text = QStringLiteral("%1·%2")
                           .arg(classNames.join(QStringLiteral("、")),
                                co ? co->name : QStringLiteral("?"));
        if (ss.length > 1)
            text += QStringLiteral("（连%1节）").arg(ss.length);
        if (ss.halfSession)
            text += QStringLiteral("（单双周）");
        r.unplaced.append(text);
    }
    return r;
}

Schedule Problem::toSchedule() const
{
    Schedule sch;
    for (int s = 0; s < sessions.size(); ++s) {
        if (!isPlaced(s))
            continue;
        const Session &ss = sessions[s];
        for (int sl : coveredSlots(s, placements[s].slot)) {
            for (qint64 c : ss.classIds)
                sch.setAssignment(c, sl, placements[s].half, ss.courseId);
        }
    }
    return sch;
}

// ==================== Solver ====================

Solver::Solver(const School &school, const EngineOptions &options, quint32 seed,
               std::atomic<bool> *cancelFlag, std::atomic<long> *iterCounter)
    : m_p(school)
    , m_opt(options)
    , m_rng(seed)
    , m_cancel(cancelFlag)
    , m_iterCounter(iterCounter)
{
}

void Solver::placeFixed()
{
    for (int s = 0; s < m_p.sessions.size(); ++s) {
        if (m_p.sessions[s].fixed)
            m_p.apply(s, m_p.sessions[s].fixedSlot, m_p.sessions[s].fixedHalf);
    }
}

void Solver::greedy()
{
    // 约束传播体现在：固定会话/不排时间/老师活动先行进入占用表后，再按优先级贪心
    QVector<int> order;
    for (int i = 0; i < m_p.sessions.size(); ++i) {
        if (!m_p.sessions[i].fixed)
            order.append(i);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const Session &sa = m_p.sessions[a];
        const Session &sb = m_p.sessions[b];
        if (sa.length != sb.length)
            return sa.length > sb.length; // 连课先排
        const Course *ca = m_p.courseOf(sa.courseId);
        const Course *cb = m_p.courseOf(sb.courseId);
        const bool ma = ca && ca->isMajor, mb = cb && cb->isMajor;
        if (ma != mb)
            return ma > mb; // 主课先排
        const int pa = ca ? ca->priority : 0, pb = cb ? cb->priority : 0;
        if (pa != pb)
            return pa > pb; // 优先次序高的先排
        return a < b;
    });

    for (int s : order) {
        const QVector<Placement> cands = m_p.candidatePositions(s);
        int bestIdx = -1;
        int bestHard = Problem::kIllegal;
        double bestSoft = 1e300;
        for (int i = 0; i < cands.size(); ++i) {
            const int h = m_p.hardAt(s, cands[i].slot, cands[i].half);
            if (h >= Problem::kIllegal)
                continue;
            m_p.apply(s, cands[i].slot, cands[i].half);
            const double soft = m_p.localSoft(s);
            m_p.remove(s);
            if (h < bestHard || (h == bestHard && soft < bestSoft - 1e-9)) {
                bestHard = h;
                bestSoft = soft;
                bestIdx = i;
            }
        }
        if (bestIdx < 0)
            continue; // 全部位置非法（如连课塞不进），记未排
        if (bestHard == 0 || m_opt.placeAll)
            m_p.apply(s, cands[bestIdx].slot, cands[bestIdx].half);
        // placeAll=false 且无可行位置 → 保持未排
    }
}

double Solver::localCost(int s)
{
    double hard = 0;
    if (m_p.isPlaced(s))
        hard = m_p.hardAt(s, m_p.placements[s].slot, m_p.placements[s].half);
    else
        hard = 1; // 未排计一次硬违例
    return HARD_WEIGHT * hard + m_p.localSoft(s);
}

void Solver::minConflicts()
{
    for (long it = 0; it < m_opt.maxIterationsPerRun; ++it) {
        if ((it & 0x3F) == 0 && m_cancel && m_cancel->load())
            break;
        if (m_iterCounter)
            m_iterCounter->fetch_add(1);
        if (m_curHard == 0 && m_curSoft <= 1e-9)
            break; // 已完美

        // 选一个当前有代价的会话（采样失败则全扫）
        int s = -1;
        QVector<int> movable;
        for (int i = 0; i < m_p.sessions.size(); ++i)
            if (!m_p.sessions[i].fixed)
                movable.append(i);
        if (movable.isEmpty())
            break;
        for (int tries = 0; tries < 10 && s < 0; ++tries) {
            const int cand = movable[m_rng.bounded(movable.size())];
            if (localCost(cand) > 1e-9)
                s = cand;
        }
        if (s < 0) {
            for (int cand : movable) {
                if (localCost(cand) > 1e-9) {
                    s = cand;
                    break;
                }
            }
        }
        if (s < 0)
            break; // 全部零代价

        // 评估所有候选位置，选代价最小者（平手随机，保持多样性）
        const Placement cur = m_p.placements[s];
        const double softBefore = m_p.localSoft(s); // s 在当前位置的软罚分
        m_p.remove(s);
        const QVector<Placement> cands = m_p.candidatePositions(s);
        Placement best = cur;
        double bestKey = 1e300;
        int tieCount = 0;
        for (const Placement &cand : cands) {
            const int h = m_p.hardAt(s, cand.slot, cand.half);
            if (h >= Problem::kIllegal)
                continue;
            m_p.apply(s, cand.slot, cand.half);
            const double key = HARD_WEIGHT * h + m_p.localSoft(s);
            m_p.remove(s);
            if (key < bestKey - 1e-9) {
                bestKey = key;
                best = cand;
                tieCount = 1;
            } else if (key < bestKey + 1e-9) {
                tieCount++;
                if (m_rng.bounded(tieCount) == 0)
                    best = cand; // reservoir 抽样保证平手等概率
            }
        }
        m_p.apply(s, best.slot, best.half);

        // 软罚分增量更新（只动了 s，聚合键之外的项不变）；硬违例全量重算（开销小且精确）
        m_curSoft += m_p.localSoft(s) - softBefore;
        m_curHard = m_p.hardViolations();
        tryUpdateBest();

        // 随机扰动防局部最优
        if (m_rng.generateDouble() < 0.02 && !movable.isEmpty()) {
            const int rs = movable[m_rng.bounded(movable.size())];
            if (!m_p.sessions[rs].fixed) {
                const QVector<Placement> rc = m_p.candidatePositions(rs);
                if (!rc.isEmpty()) {
                    const Placement rp = rc[m_rng.bounded(rc.size())];
                    const double rSoftBefore = m_p.localSoft(rs);
                    m_p.remove(rs);
                    m_p.apply(rs, rp.slot, rp.half);
                    m_curSoft += m_p.localSoft(rs) - rSoftBefore;
                    m_curHard = m_p.hardViolations();
                    tryUpdateBest();
                }
            }
        }
    }
}

void Solver::tryUpdateBest()
{
    if (m_curHard < m_bestHard
        || (m_curHard == m_bestHard && m_curSoft < m_bestSoft - 1e-9)) {
        m_bestHard = m_curHard;
        m_bestSoft = m_curSoft;
        m_bestPlacements = m_p.placements;
    }
}

void Solver::run()
{
    placeFixed();
    greedy();
    m_curHard = m_p.hardViolations();
    m_curSoft = m_p.softScore();
    tryUpdateBest();
    minConflicts();
    // 还原最优快照
    for (int s = 0; s < m_p.sessions.size(); ++s)
        m_p.remove(s);
    for (int s = 0; s < m_p.sessions.size(); ++s) {
        if (s < m_bestPlacements.size() && m_bestPlacements[s].slot >= 0)
            m_p.apply(s, m_bestPlacements[s].slot, m_bestPlacements[s].half);
    }
}

Schedule Solver::bestSchedule() const
{
    return m_p.toSchedule();
}

QualityReport Solver::bestReport() const
{
    return m_p.report();
}

} // namespace sched::engine
