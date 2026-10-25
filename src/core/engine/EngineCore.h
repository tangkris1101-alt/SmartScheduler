#pragma once

#include "EngineTypes.h"
#include "model/School.h"

#include <QHash>
#include <QPair>
#include <QRandomGenerator>
#include <QSet>
#include <array>
#include <atomic>

// 排课引擎内部求解器（纯计算，无 QObject，可在线程池中并行）
namespace sched::engine {

// 待排的"节次会话"：一个连课占 length 个连续 slot，单双周会话 halfSession=true
struct Session {
    qint64 entryId = -1;          // 来源 PlanEntry（预排临时会话为 -1）
    QVector<qint64> classIds;     // 合班合并后有多个班
    qint64 courseId = -1;
    QVector<qint64> teacherIds;   // 合班后取并集
    qint64 classroomId = -1;      // -1 表示不占公共教室
    int length = 1;               // 连课长度（1=单节）
    bool halfSession = false;     // 单双周会话（只占一个 half）
    int progressGroup = -1;       // 进度一致组号（-1 未分组，自动归入同老师同课默认组）
    bool fixed = false;           // 预排固定，不可移动
    int fixedSlot = -1;           // 预排的起始 slot
    int fixedHalf = 0;            // 预排占用的半周（0 整节 / 1 单周 / 2 双周）
    // 不计课时、不挂老师（如第 9 节体育活动）：不占老师、不产生老师撞课，
    // 也不受老师「不排时间」与互斥组约束；班级维度的占用照旧
    bool noLoad = false;
};

struct Placement {
    int slot = -1; // -1 = 未排
    int half = 0;  // 0 整节，1 单周，2 双周
};

// localSoft 的增量聚合键：同老师、同课程、同进度组
struct ProgressKey {
    qint64 teacher = -1;
    qint64 course = -1;
    int group = -1;
    bool operator==(const ProgressKey &) const = default;
};

inline size_t qHash(const ProgressKey &k, size_t seed = 0) noexcept
{
    return qHashMulti(seed, k.teacher, k.course, k.group);
}

struct ProgressGroup {
    QVector<int> slotCount; // [slot] → 该组在此 slot 起始的会话数
    double cost = 0.0;
    explicit ProgressGroup(int slots = 0)
        : slotCount(slots, 0)
    {
    }
};

struct ClassCourseGroup {
    QVector<int> perDay; // [day] → 该班该课在此天起始的会话数
    double cost = 0.0;
    explicit ClassCourseGroup(int days = 0)
        : perDay(days, 0)
    {
    }
};

// 问题状态：展开后的会话、占用表、约束评估
class Problem
{
public:
    explicit Problem(const School &school, SolverProfile *prof = nullptr);

    const School &school;
    QVector<Session> sessions;
    QVector<Placement> placements;
    QHash<int, QVector<int>> m_syncPeers; // 会话 → 同组同课次的其它会话
    void buildSyncPeers();

    // ---- 占用 ----
    void apply(int s, int slot, int half);
    void remove(int s);
    void rebuildOccupancy();  // 从 placements 重建占用表（消除搜索期的增量账目漂移）
    bool isPlaced(int s) const { return placements[s].slot >= 0; }

    // 会话 s 的 length 个连续 slot 覆盖（起始 slot 起）
    QVector<int> coveredSlots(int s, int startSlot) const;

    // ---- 硬约束 ----
    static constexpr int kIllegal = 1000000; // 非法位置（如连课跨天）
    int hardAt(int s, int slot, int half) const;   // 含同时段组联动检查
    int hardAtNoSync(int s, int slot, int half) const; // 仅本会话自身约束
    // 同时段组（走班）：把 s 与它的同组同课次会话一起落位 / 一起撤销
    void placeWithSync(int s, int slot, int half);
    void removeWithSync(int s);
    QVector<int> syncPeers(int s) const { return m_syncPeers.value(s); } // s 放入该位置后参与的硬冲突数
    bool placeable(int s, int slot, int half) const { return hardAt(s, slot, half) == 0; }
    int hardViolations() const; // 全局硬违例（含未排会话）

    // ---- 软约束 ----
    // 与 s 相关的软罚分（s 当前状态下）；配合 apply/remove 可得增量
    double localSoft(int s) const;
    double softScore(QMap<QString, double> *breakdown = nullptr) const;

    QualityReport report() const;
    Schedule toSchedule() const;

    int slotCount() const { return school.timeConfig.slotCount(); }
    int periodsPerDay() const { return school.timeConfig.periodsPerDay; }

    // 会话 s 的全部候选位置
    QVector<Placement> candidatePositions(int s) const;

    // 课程查找（无此课程返回 nullptr）
    const Course *courseOf(qint64 courseId) const;

private:
    void expandSessions();
    Session makeSession(const PlanEntry &e, int length, bool half) const;
    void mergeCombined();
    void applyPreAssignments();

    // 软约束增量聚合：apply/remove 时维护，localSoft 直接查
    void updateSoftAggregates(int s, int slot, int delta);
    void resetSoftAggregates();
    void recomputeProgressCost(ProgressGroup &g) const;
    void recomputeTeacherDailyCost(int ti);
    void recomputeClassCourseCost(ClassCourseGroup &g) const;

    // 硬违例增量缓存：apply/remove 维护，hardViolations 直接相加
    void recomputeOccupancyHard();
    int forbiddenCountFor(int s, int slot) const;

    // 软约束分项（按会话/键聚合）
    double progressAlignCost() const;
    double evenSpreadCost() const;
    double amPmCost() const;
    double majorMorningCost() const;
    // 计划条目的时段要求（软）：上午节数不足、以及「前 N 节」外的节次
    double planTimePreferenceCost() const;
    double teacherDailyCost() const;
    double classCourseSpreadCost() const;
    double chainCrossCost() const;

    QHash<qint64, int> m_classIdx, m_teacherIdx, m_courseIdx, m_roomIdx;
    QHash<qint64, int> m_planIdx; // planEntry id → 下标（localSoft 避免线性查找）
    QVector<QSet<int>> m_classForbidden, m_teacherForbidden, m_courseForbidden, m_roomForbidden;

    // 互斥老师分组预计算，避免 hardViolations 每次重扫全部老师
    QVector<qint64> m_mutexGroups;
    QHash<qint64, QVector<int>> m_mutexTeachers;

    // localSoft 的增量聚合表
    QHash<ProgressKey, ProgressGroup> m_progressGroups;
    QVector<QVector<int>> m_teacherDayCount; // [teacher][day]
    QVector<double> m_teacherDailyCost;
    QHash<QPair<qint64, qint64>, ClassCourseGroup> m_classCourseGroups;
    QVector<int> m_entryMorningCount; // 每个计划条目已排在上午的节数
    int m_days = 0;

    // 硬违例增量缓存
    int m_hardClass = 0;
    int m_hardTeacher = 0;
    int m_hardMutex = 0;
    int m_hardRoom = 0;
    int m_hardForbidden = 0;
    QHash<qint64, QVector<std::array<int, 2>>> m_mutexActive; // 互斥组内每个 slot/parity 有课的老师数

    // 占用计数 [实体][slot][parity]，parity 0=单周 1=双周；整节两者都 +1
    QVector<QVector<std::array<int, 2>>> m_classOcc, m_teacherOcc, m_roomOcc;
    // 课程-节次班级数 [课程][slot][parity]（evenSpread 评分用，随 apply/remove 增量维护）
    QVector<QVector<std::array<int, 2>>> m_courseClsOcc;

    // 非空时才计时；由 Solver 在开启 SMARTSCHED_PROFILE 时传入
    SolverProfile *m_prof = nullptr;
};

// 单轮求解：贪心构造 + min-conflicts 局部搜索
class Solver
{
public:
    Solver(const School &school, const EngineOptions &options, quint32 seed,
           std::atomic<bool> *cancelFlag, std::atomic<long> *iterCounter);

    void run();

    Schedule bestSchedule() const;
    QualityReport bestReport() const;

    bool profileEnabled() const { return m_profileOn; }
    SolverProfile &profile() { return m_prof; }
    const SolverProfile &profile() const { return m_prof; }

private:
    void placeFixed();
    void greedy();
    void minConflicts(long budget);
    void attempt(long budget); // 一轮完整求解：固定预排 + 贪心 + 最小冲突
    void clearPlacements();
    void restoreBest();
    double localCost(int s); // s 当前位置参与的 硬*权重+软
    void tryUpdateBest();

    SolverProfile m_prof;
    bool m_profileOn = false;
    Problem m_p;
    EngineOptions m_opt;
    QRandomGenerator m_rng;
    std::atomic<bool> *m_cancel;
    std::atomic<long> *m_iterCounter;
    QVector<int> m_movable; // 非固定会话列表；fixed 标记在求解期间不变，只需建一次
    bool m_movableBuilt = false;

    int m_curHard = 0;
    double m_curSoft = 0.0;
    QVector<Placement> m_bestPlacements;
    int m_bestHard = kBestInit;
    double m_bestSoft = 0.0;
    static constexpr int kBestInit = 1 << 30;
};

} // namespace sched::engine
