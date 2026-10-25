#include "EngineCore.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace sched::engine {

namespace {

using ProfileClock = std::chrono::steady_clock;

inline double profileMs(ProfileClock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(ProfileClock::now() - t0).count();
}

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

Problem::Problem(const School &s, SolverProfile *prof)
    : school(s)
{
    m_prof = prof;
    const ProfileClock::time_point tBuild = m_prof ? ProfileClock::now() : ProfileClock::time_point{};
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
    for (int i = 0; i < s.planEntries.size(); ++i)
        m_planIdx[s.planEntries[i].id] = i;
    for (int ti = 0; ti < s.teachers.size(); ++ti) {
        const qint64 mg = s.teachers[ti].mutualExclusionGroup;
        if (mg < 0)
            continue;
        if (!m_mutexTeachers.contains(mg))
            m_mutexGroups.append(mg);
        m_mutexTeachers[mg].append(ti);
    }
    for (qint64 mg : m_mutexGroups)
        m_mutexActive[mg].assign(sc, zero);

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

    m_days = s.timeConfig.daysPerWeek;
    m_teacherDayCount.assign(s.teachers.size(), QVector<int>(m_days, 0));
    m_teacherDailyCost.assign(s.teachers.size(), 0.0);
    m_entryMorningCount.assign(s.planEntries.size(), 0);

    // 老师活动时间预置为占用（两个 parity 都占）
    for (const auto &a : s.teacherActivities) {
        if (!m_teacherIdx.contains(a.teacherId) || !s.timeConfig.isValidSlot(a.slotIndex))
            continue;
        const int ti = m_teacherIdx[a.teacherId];
        m_teacherOcc[ti][a.slotIndex][0]++;
        m_teacherOcc[ti][a.slotIndex][1]++;
    }
    recomputeOccupancyHard(); // 老师活动造成的初始占用（正常为 0 违例）

    expandSessions();
    mergeCombined();
    applyPreAssignments();
    buildSyncPeers();
    placements.resize(sessions.size());
    if (m_prof)
        m_prof->buildMs += profileMs(tBuild);
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
    s.noLoad = e.noLoad;
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
            m.noLoad = true; // 所有成员都不计课时，合班后才算「不挂老师」
            bool firstMember = true;
            for (auto &l : memberLists) {
                const int idx = l.takeFirst();
                consumed[idx] = true;
                const Session &s = sessions[idx];
                m.classIds.append(s.classIds.first());
                if (!s.noLoad)
                    m.noLoad = false;
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

void Problem::updateSoftAggregates(int s, int slot, int delta)
{
    const int sc = slotCount();
    if (slot < 0 || slot >= sc)
        return;
    const Session &ss = sessions[s];
    const int ppd = periodsPerDay();
    const int morning = school.timeConfig.morningPeriods;

    // 进度一致：同老师 + 同课程 + 同进度组，按起始 slot 计
    for (qint64 t : ss.teacherIds) {
        const ProgressKey key{t, ss.courseId, ss.progressGroup};
        auto it = m_progressGroups.find(key);
        if (it == m_progressGroups.end()) {
            if (delta < 0)
                continue;
            it = m_progressGroups.insert(key, ProgressGroup(sc));
        }
        it->slotCount[slot] += delta;
        recomputeProgressCost(it.value());
        bool any = false;
        for (int c : it->slotCount) {
            if (c > 0) {
                any = true;
                break;
            }
        }
        if (!any)
            m_progressGroups.erase(it);
    }

    // 老师日课时：按覆盖的每一节计
    for (qint64 t : ss.teacherIds) {
        const auto it = m_teacherIdx.constFind(t);
        if (it == m_teacherIdx.constEnd())
            continue;
        const int ti = it.value();
        for (int k = 0; k < ss.length; ++k) {
            const int sl = slot + k;
            if (sl >= 0 && sl < sc)
                m_teacherDayCount[ti][sl / ppd] += delta;
        }
        recomputeTeacherDailyCost(ti);
    }

    // 同班同课分布：原逻辑只按起始 slot 所在天计一次
    const int day = slot / ppd;
    for (qint64 cls : ss.classIds) {
        const QPair<qint64, qint64> key{cls, ss.courseId};
        auto it = m_classCourseGroups.find(key);
        if (it == m_classCourseGroups.end()) {
            if (delta < 0)
                continue;
            it = m_classCourseGroups.insert(key, ClassCourseGroup(m_days));
        }
        it->perDay[day] += delta;
        recomputeClassCourseCost(it.value());
        bool any = false;
        for (int c : it->perDay) {
            if (c > 0) {
                any = true;
                break;
            }
        }
        if (!any)
            m_classCourseGroups.erase(it);
    }

    // 计划条目的上午节数：按覆盖节次中属于上午的节数计
    if (ss.entryId >= 0) {
        const auto it = m_planIdx.constFind(ss.entryId);
        if (it != m_planIdx.constEnd()) {
            int &have = m_entryMorningCount[it.value()];
            for (int k = 0; k < ss.length; ++k) {
                const int sl = slot + k;
                if (sl >= 0 && sl < sc && (sl % ppd) < morning)
                    have += delta;
            }
        }
    }
}

void Problem::resetSoftAggregates()
{
    m_progressGroups.clear();
    for (auto &v : m_teacherDayCount)
        std::fill(v.begin(), v.end(), 0);
    std::fill(m_teacherDailyCost.begin(), m_teacherDailyCost.end(), 0.0);
    m_classCourseGroups.clear();
    std::fill(m_entryMorningCount.begin(), m_entryMorningCount.end(), 0);
}

void Problem::recomputeProgressCost(ProgressGroup &g) const
{
    double c = 0;
    int prev = -1;
    for (int sl = 0; sl < g.slotCount.size(); ++sl) {
        if (g.slotCount[sl] <= 0)
            continue;
        if (prev >= 0)
            c += std::max(0, sl - prev - 1);
        prev = sl;
    }
    g.cost = W_PROGRESS * c;
}

void Problem::recomputeTeacherDailyCost(int ti)
{
    const int maxPerDay = school.teachers[ti].maxPeriodsPerDay;
    double penalty = 0;
    double sq = 0;
    const QVector<int> &perDay = m_teacherDayCount[ti];
    for (int d = 0; d < perDay.size(); ++d) {
        const int n = perDay[d];
        if (maxPerDay >= 0 && n > maxPerDay)
            penalty += W_DAILY_MAX * (n - maxPerDay);
        sq += double(n) * n;
    }
    m_teacherDailyCost[ti] = penalty + W_DAILY_BALANCE * sq;
}

void Problem::recomputeClassCourseCost(ClassCourseGroup &g) const
{
    double c = 0;
    for (int n : g.perDay)
        c += double(n) * (n - 1) / 2.0;
    g.cost = W_CLASS_COURSE_SPREAD * c;
}

void Problem::recomputeOccupancyHard()
{
    m_hardClass = 0;
    m_hardTeacher = 0;
    m_hardMutex = 0;
    m_hardRoom = 0;
    const int sc = slotCount();

    for (int i = 0; i < school.classes.size(); ++i)
        for (int sl = 0; sl < sc; ++sl)
            for (int p = 0; p < 2; ++p)
                m_hardClass += std::max(0, m_classOcc[i][sl][p] - 1);
    for (int i = 0; i < school.teachers.size(); ++i)
        for (int sl = 0; sl < sc; ++sl)
            for (int p = 0; p < 2; ++p)
                m_hardTeacher += std::max(0, m_teacherOcc[i][sl][p] - 1);

    for (auto &v : m_mutexActive)
        for (auto &a : v)
            a = {0, 0};
    for (qint64 mg : m_mutexGroups) {
        const auto git = m_mutexTeachers.constFind(mg);
        if (git == m_mutexTeachers.constEnd())
            continue;
        auto &active = m_mutexActive[mg];
        for (int sl = 0; sl < sc; ++sl) {
            for (int p = 0; p < 2; ++p) {
                int cnt = 0;
                for (int ti : git.value()) {
                    if (m_teacherOcc[ti][sl][p] > 0)
                        ++cnt;
                }
                active[sl][p] = cnt;
                m_hardMutex += std::max(0, cnt - 1);
            }
        }
    }

    for (int i = 0; i < school.classrooms.size(); ++i)
        for (int sl = 0; sl < sc; ++sl)
            for (int p = 0; p < 2; ++p)
                m_hardRoom += std::max(0, m_roomOcc[i][sl][p] - school.classrooms[i].capacity);
}

int Problem::forbiddenCountFor(int s, int slot) const
{
    const Session &ss = sessions[s];
    const int sc = slotCount();
    int f = 0;
    for (int k = 0; k < ss.length; ++k) {
        const int sl = slot + k;
        if (sl < 0 || sl >= sc)
            continue;
        for (qint64 c : ss.classIds) {
            if (m_classIdx.contains(c) && m_classForbidden[m_classIdx[c]].contains(sl))
                ++f;
        }
        if (m_courseIdx.contains(ss.courseId)
            && m_courseForbidden[m_courseIdx[ss.courseId]].contains(sl))
            ++f;
        for (qint64 t : ss.teacherIds) {
            if (ss.noLoad)
                continue;
            if (m_teacherIdx.contains(t) && m_teacherForbidden[m_teacherIdx[t]].contains(sl))
                ++f;
        }
        if (ss.classroomId >= 0 && m_roomIdx.contains(ss.classroomId)
            && m_roomForbidden[m_roomIdx[ss.classroomId]].contains(sl))
            ++f;
    }
    return f;
}

// 从 placements 重建占用表：局部搜索里的增量加减一旦漂移，
// 最终报告会出现「课表本身干净、引擎却说有违例」的幽灵数字
void Problem::rebuildOccupancy()
{
    resetSoftAggregates();
    m_hardForbidden = 0;
    for (auto &v : m_classOcc)
        for (auto &a : v)
            a = {0, 0};
    for (auto &v : m_teacherOcc)
        for (auto &a : v)
            a = {0, 0};
    for (auto &v : m_roomOcc)
        for (auto &a : v)
            a = {0, 0};
    for (auto &v : m_courseClsOcc)
        for (auto &a : v)
            a = {0, 0};
    // 老师活动（班主任会/教研）本来就折在老师占用里，重建时要补回去
    for (const TeacherActivity &a : school.teacherActivities) {
        if (!m_teacherIdx.contains(a.teacherId) || !school.timeConfig.isValidSlot(a.slotIndex))
            continue;
        const int ti = m_teacherIdx[a.teacherId];
        m_teacherOcc[ti][a.slotIndex][0]++;
        m_teacherOcc[ti][a.slotIndex][1]++;
    }
    recomputeOccupancyHard(); // 老师活动造成的初始占用
    for (int s = 0; s < sessions.size(); ++s) {
        const Placement cur = placements[s];
        if (cur.slot < 0)
            continue;
        apply(s, cur.slot, cur.half); // 占用已清零，这里只累加，placements 写回同一值
    }
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
    const int sc = slotCount();
    for (int k = 0; k < ss.length; ++k) {
        const int sl = slot + k;
        if (sl < 0 || sl >= sc)
            continue;
        for (int p = p0; p <= p1; ++p) {
            for (qint64 c : ss.classIds) {
                const auto ci = m_classIdx.constFind(c);
                if (ci == m_classIdx.constEnd())
                    continue;
                int &cell = m_classOcc[ci.value()][sl][p];
                const int old = cell;
                ++cell;
                if (old >= 1)
                    ++m_hardClass;
            }
            for (qint64 t : ss.teacherIds) {
                if (ss.noLoad)
                    continue;
                const auto tiIt = m_teacherIdx.constFind(t);
                if (tiIt == m_teacherIdx.constEnd())
                    continue;
                const int ti = tiIt.value();
                int &cell = m_teacherOcc[ti][sl][p];
                const int old = cell;
                ++cell;
                if (old >= 1)
                    ++m_hardTeacher;
                const qint64 mg = school.teachers[ti].mutualExclusionGroup;
                if (mg >= 0 && old == 0) {
                    auto &active = m_mutexActive[mg][sl][p];
                    const int oldActive = active;
                    ++active;
                    if (oldActive >= 1)
                        ++m_hardMutex;
                }
            }
            if (ss.classroomId >= 0) {
                const auto ri = m_roomIdx.constFind(ss.classroomId);
                if (ri != m_roomIdx.constEnd()) {
                    int &cell = m_roomOcc[ri.value()][sl][p];
                    const int old = cell;
                    ++cell;
                    if (old >= school.classrooms[ri.value()].capacity)
                        ++m_hardRoom;
                }
            }
            if (hasCourse)
                m_courseClsOcc[m_courseIdx[ss.courseId]][sl][p] += nCls;
        }
    }
    m_hardForbidden += forbiddenCountFor(s, slot);
    updateSoftAggregates(s, slot, +1);
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
    const int sc = slotCount();
    for (int k = 0; k < ss.length; ++k) {
        const int sl = cur.slot + k;
        if (sl < 0 || sl >= sc)
            continue;
        for (int p = p0; p <= p1; ++p) {
            for (qint64 c : ss.classIds) {
                const auto ci = m_classIdx.constFind(c);
                if (ci == m_classIdx.constEnd())
                    continue;
                int &cell = m_classOcc[ci.value()][sl][p];
                const int old = cell;
                --cell;
                if (old >= 2)
                    --m_hardClass;
            }
            for (qint64 t : ss.teacherIds) {
                if (ss.noLoad)
                    continue;
                const auto tiIt = m_teacherIdx.constFind(t);
                if (tiIt == m_teacherIdx.constEnd())
                    continue;
                const int ti = tiIt.value();
                int &cell = m_teacherOcc[ti][sl][p];
                const int old = cell;
                --cell;
                if (old >= 2)
                    --m_hardTeacher;
                const qint64 mg = school.teachers[ti].mutualExclusionGroup;
                if (mg >= 0 && old == 1) {
                    auto &active = m_mutexActive[mg][sl][p];
                    const int oldActive = active;
                    --active;
                    if (oldActive >= 2)
                        --m_hardMutex;
                }
            }
            if (ss.classroomId >= 0) {
                const auto ri = m_roomIdx.constFind(ss.classroomId);
                if (ri != m_roomIdx.constEnd()) {
                    int &cell = m_roomOcc[ri.value()][sl][p];
                    const int old = cell;
                    --cell;
                    if (old > school.classrooms[ri.value()].capacity)
                        --m_hardRoom;
                }
            }
            if (hasCourse)
                m_courseClsOcc[m_courseIdx[ss.courseId]][sl][p] -= nCls;
        }
    }
    m_hardForbidden -= forbiddenCountFor(s, cur.slot);
    updateSoftAggregates(s, cur.slot, -1);
}

// 建立「同时段组」的会话映射：组内每个成员的第 k 次课互相同步
void Problem::buildSyncPeers()
{
    m_syncPeers.clear();
    for (const TimeSyncGroup &g : school.timeSyncGroups) {
        QVector<QVector<int>> members; // 每个成员的会话列表（按会话序号）
        for (const auto &m : g.entries) {
            QVector<int> list;
            for (int i = 0; i < sessions.size(); ++i) {
                const Session &s = sessions[i];
                if (s.classIds.size() == 1 && s.classIds.first() == m.first
                    && s.courseId == m.second)
                    list.append(i);
            }
            members.append(list);
        }
        int maxK = 0;
        for (const auto &l : members)
            maxK = std::max(maxK, int(l.size()));
        for (int k = 0; k < maxK; ++k) {
            QVector<int> peers;
            for (const auto &l : members) {
                if (k < l.size())
                    peers.append(l[k]);
            }
            for (int a : peers) {
                for (int b : peers) {
                    if (a != b)
                        m_syncPeers[a].append(b);
                }
            }
        }
    }
}

// 含同时段组联动的硬约束检查：s 要与已落位的同组会话同节同半周，
// 并且（对未落位的同组会话而言）这个位置对它们也必须合法
int Problem::hardAt(int s, int slot, int half) const
{
    int v = hardAtNoSync(s, slot, half);
    if (v >= kIllegal)
        return v;
    const auto it = m_syncPeers.constFind(s);
    if (it == m_syncPeers.constEnd())
        return v;
    for (int peer : it.value()) {
        const Placement &pl = placements[peer];
        if (pl.slot >= 0) {
            if (pl.slot != slot || pl.half != half)
                return kIllegal; // 同组必须同节同半周
            continue;
        }
        const int pv = hardAtNoSync(peer, slot, half);
        if (pv >= kIllegal)
            return kIllegal; // 该位置放不下同组的另一个班
        v = std::max(v, pv);
    }
    return v;
}

// 落位：s 与它的同组会话一起放下去（已固定或已落位的不动）
void Problem::placeWithSync(int s, int slot, int half)
{
    apply(s, slot, half);
    const auto it = m_syncPeers.constFind(s);
    if (it == m_syncPeers.constEnd())
        return;
    for (int peer : it.value()) {
        if (!sessions[peer].fixed && placements[peer].slot < 0)
            apply(peer, slot, half);
    }
}

// 撤销：把 s 连同「与它同节落位的同组会话」一起撤掉
void Problem::removeWithSync(int s)
{
    const Placement cur = placements[s];
    const auto it = m_syncPeers.constFind(s);
    if (it != m_syncPeers.constEnd()) {
        for (int peer : it.value()) {
            if (sessions[peer].fixed)
                continue;
            if (cur.slot >= 0 && placements[peer].slot == cur.slot
                && placements[peer].half == cur.half)
                remove(peer);
        }
    }
    remove(s);
}

int Problem::hardAtNoSync(int s, int slot, int half) const
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
                if (ss.noLoad || !m_teacherIdx.contains(t))
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
    const ProfileClock::time_point t0 = m_prof ? ProfileClock::now() : ProfileClock::time_point{};
    int v = m_hardClass + m_hardTeacher + m_hardMutex + m_hardRoom + m_hardForbidden;
    // 未排会话
    for (int s = 0; s < sessions.size(); ++s)
        if (!isPlaced(s))
            ++v;
    // 同时段组落位不一致（例如同组两条固定预排被钉在不同节）
    for (auto it = m_syncPeers.constBegin(); it != m_syncPeers.constEnd(); ++it) {
        const Placement &a = placements[it.key()];
        if (a.slot < 0)
            continue;
        for (int peer : it.value()) {
            const Placement &b = placements[peer];
            if (b.slot < 0)
                continue;
            if (b.slot != a.slot || b.half != a.half)
                ++v;
        }
    }
    if (m_prof) {
        ++m_prof->hardViolationCalls;
        m_prof->hardViolationMs += profileMs(t0);
    }
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
    const ProfileClock::time_point t0 = m_prof ? ProfileClock::now() : ProfileClock::time_point{};
    ProfileClock::time_point tb = m_prof ? ProfileClock::now() : ProfileClock::time_point{};
    // 与 s 相关的聚合键重算（移动单会话时这些键之外的分项不变，可作精确增量）
    double cost = 0;
    const Session &ss = sessions[s];

    // 进度一致：s 的每个 (老师,课程,进度组)，直接查增量聚合
    const int ppd = periodsPerDay();
    for (qint64 t : ss.teacherIds) {
        const auto it = m_progressGroups.constFind(ProgressKey{t, ss.courseId, ss.progressGroup});
        if (it != m_progressGroups.constEnd())
            cost += it->cost;
    }
    if (m_prof) {
        m_prof->localSoftProgressMs += profileMs(tb);
        tb = ProfileClock::now();
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
    if (m_prof) {
        m_prof->localSoftEvenSpreadMs += profileMs(tb);
        tb = ProfileClock::now();
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
    if (m_prof) {
        m_prof->localSoftAmPmMs += profileMs(tb);
        tb = ProfileClock::now();
    }

    // 老师日课时：s 的每个老师整体，直接查增量聚合
    for (qint64 t : ss.teacherIds) {
        const auto it = m_teacherIdx.constFind(t);
        if (it != m_teacherIdx.constEnd())
            cost += m_teacherDailyCost[it.value()];
    }
    if (m_prof) {
        m_prof->localSoftTeacherDailyMs += profileMs(tb);
        tb = ProfileClock::now();
    }

    // 同班同课分布：s 的每个 (班,课程)，直接查增量聚合
    for (qint64 cls : ss.classIds) {
        const auto it = m_classCourseGroups.constFind(QPair<qint64, qint64>{cls, ss.courseId});
        if (it != m_classCourseGroups.constEnd())
            cost += it->cost;
    }
    if (m_prof) {
        m_prof->localSoftClassSpreadMs += profileMs(tb);
        tb = ProfileClock::now();
    }

    // 计划条目的时段要求（上午节数 / 前 N 节）：只与 s 所属条目相关
    if (isPlaced(s) && ss.entryId >= 0) {
        const int entryIdx = m_planIdx.value(ss.entryId, -1);
        if (entryIdx >= 0) {
            const PlanEntry &entry = school.planEntries[entryIdx];
            const int morningP = school.timeConfig.morningPeriods;
            if (entry.morningPeriods > 0) {
                const int have = m_entryMorningCount[entryIdx];
                cost += W_PLAN_MORNING * std::max(0, entry.morningPeriods - have);
            }
            if (entry.earlyPeriods > 0) {
                const int start = placements[s].slot;
                const int sc = slotCount();
                for (int k = 0; k < ss.length; ++k) {
                    const int sl = start + k;
                    if (sl >= 0 && sl < sc && sl % ppd >= entry.earlyPeriods)
                        cost += W_PLAN_EARLY;
                }
            }
        }
    }

    if (m_prof) {
        m_prof->localSoftPlanMs += profileMs(tb);
        ++m_prof->localSoftCalls;
        m_prof->localSoftMs += profileMs(t0);
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
    : m_profileOn(qEnvironmentVariableIsSet("SMARTSCHED_PROFILE"))
    , m_p(school, m_profileOn ? &m_prof : nullptr)
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
            m_p.placeWithSync(s, m_p.sessions[s].fixedSlot, m_p.sessions[s].fixedHalf);
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
            m_p.placeWithSync(s, cands[bestIdx].slot, cands[bestIdx].half);
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

void Solver::minConflicts(long budget)
{
    long done = 0;
    for (long it = 0; it < budget; ++it) {
        if ((it & 0x3F) == 0 && m_cancel && m_cancel->load())
            break;
        if (m_iterCounter)
            m_iterCounter->fetch_add(1);
        if (m_curHard == 0 && m_curSoft <= 1e-9)
            break; // 已完美

        ProfileClock::time_point t = m_profileOn ? ProfileClock::now() : ProfileClock::time_point{};

        // 选一个当前有代价的会话（采样失败则全扫）
        int s = -1;
        if (!m_movableBuilt) {
            m_movableBuilt = true;
            for (int i = 0; i < m_p.sessions.size(); ++i)
                if (!m_p.sessions[i].fixed)
                    m_movable.append(i);
        }
        if (m_movable.isEmpty()) {
            if (m_profileOn)
                m_prof.selectMs += profileMs(t);
            break;
        }
        for (int tries = 0; tries < 10 && s < 0; ++tries) {
            const int cand = m_movable[m_rng.bounded(m_movable.size())];
            if (localCost(cand) > 1e-9)
                s = cand;
        }
        if (s < 0) {
            for (int cand : m_movable) {
                if (localCost(cand) > 1e-9) {
                    s = cand;
                    break;
                }
            }
        }
        if (s < 0) {
            if (m_profileOn)
                m_prof.selectMs += profileMs(t);
            break; // 全部零代价
        }
        if (m_profileOn) {
            m_prof.selectMs += profileMs(t);
            t = ProfileClock::now();
        }

        // 评估所有候选位置，选代价最小者（平手随机，保持多样性）
        const Placement cur = m_p.placements[s];
        const double softBefore = m_p.localSoft(s); // s 在当前位置的软罚分
        m_p.removeWithSync(s);
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
        m_p.placeWithSync(s, best.slot, best.half);

        // 软罚分增量更新（只动了 s，聚合键之外的项不变）；硬违例全量重算（开销小且精确）
        m_curSoft += m_p.localSoft(s) - softBefore;
        if (m_profileOn) {
            m_prof.candidateEvalMs += profileMs(t);
            t = ProfileClock::now();
        }
        m_curHard = m_p.hardViolations();
        if (m_profileOn)
            t = ProfileClock::now();
        tryUpdateBest();
        if (m_profileOn) {
            m_prof.snapshotMs += profileMs(t);
            t = ProfileClock::now();
        }

        // 随机扰动防局部最优
        if (m_rng.generateDouble() < 0.02 && !m_movable.isEmpty()) {
            const int rs = m_movable[m_rng.bounded(m_movable.size())];
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
        if (m_profileOn)
            m_prof.perturbMs += profileMs(t);
        ++done;
    }
    if (m_profileOn)
        m_prof.iterations += done;
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
    const ProfileClock::time_point tRun = m_profileOn ? ProfileClock::now() : ProfileClock::time_point{};
    attempt(m_opt.maxIterationsPerRun);

    // m_bestHard 是搜索期的增量计数，可能因占用表漂移而“假阳性”。
    // 先按最优快照重建占用表复核一次；确实有硬违例才值得清空重跑，
    // 否则白付一次贪心 + 迭代的代价。
    bool restored = false;
    auto verifyBest = [this, &restored]() {
        restoreBest();
        m_curHard = m_p.hardViolations();
        m_bestHard = m_curHard;
        restored = true;
    };
    if (m_bestHard > 0)
        verifyBest();

    // 一轮跑完仍有硬违例（实测偶发：198/198 全排上、却留下 1 处撞课）时换随机起点再来，
    // 取违例最少者。只在复核后仍有违例时才付代价。
    for (int retry = 0; retry < std::max(0, m_opt.hardRetries) && m_bestHard > 0; ++retry) {
        if (m_cancel && m_cancel->load())
            break;
        clearPlacements();
        restored = false;
        attempt(std::max<long>(2000, m_opt.maxIterationsPerRun / 2));
        if (m_bestHard > 0)
            verifyBest();
    }
    const ProfileClock::time_point tRestore = m_profileOn ? ProfileClock::now() : ProfileClock::time_point{};
    if (!restored)
        restoreBest();
    if (m_profileOn) {
        m_prof.restoreMs += profileMs(tRestore);
        m_prof.totalMs += profileMs(tRun);
    }
}

void Solver::attempt(long budget)
{
    if (m_profileOn)
        ++m_prof.attempts;

    auto t = m_profileOn ? ProfileClock::now() : ProfileClock::time_point{};
    placeFixed();
    if (m_profileOn) {
        m_prof.fixedMs += profileMs(t);
        t = ProfileClock::now();
    }

    greedy();
    if (m_profileOn) {
        m_prof.greedyMs += profileMs(t);
        t = ProfileClock::now();
    }

    m_curHard = m_p.hardViolations();
    m_curSoft = m_p.softScore();
    tryUpdateBest();
    if (m_profileOn) {
        m_prof.initEvalMs += profileMs(t);
        t = ProfileClock::now();
    }

    minConflicts(budget);
    if (m_profileOn)
        m_prof.minConflictsMs += profileMs(t);
}

// 清空全部落位（固定预排的 fixed 标记保留，下一轮 placeFixed 会重新钉回去）
void Solver::clearPlacements()
{
    for (int s = 0; s < m_p.sessions.size(); ++s)
        m_p.remove(s);
}

void Solver::restoreBest()
{
    // 直接把 placements 指向最优快照，再重建占用/聚合表。
    // 不能逐会话 placeWithSync：同组会话会被按“当前”位置一起放，快照与占用对不上。
    m_p.placements = m_bestPlacements;
    m_p.rebuildOccupancy();
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
