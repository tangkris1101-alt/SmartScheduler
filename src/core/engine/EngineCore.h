#pragma once

#include "EngineTypes.h"
#include "model/School.h"

#include <QHash>
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
};

struct Placement {
    int slot = -1; // -1 = 未排
    int half = 0;  // 0 整节，1 单周，2 双周
};

// 问题状态：展开后的会话、占用表、约束评估
class Problem
{
public:
    explicit Problem(const School &school);

    const School &school;
    QVector<Session> sessions;
    QVector<Placement> placements;
    QHash<int, QVector<int>> m_syncPeers; // 会话 → 同组同课次的其它会话
    void buildSyncPeers();

    // ---- 占用 ----
    void apply(int s, int slot, int half);
    void remove(int s);
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
    QVector<QSet<int>> m_classForbidden, m_teacherForbidden, m_courseForbidden, m_roomForbidden;

    // 占用计数 [实体][slot][parity]，parity 0=单周 1=双周；整节两者都 +1
    QVector<QVector<std::array<int, 2>>> m_classOcc, m_teacherOcc, m_roomOcc;
    // 课程-节次班级数 [课程][slot][parity]（evenSpread 评分用，随 apply/remove 增量维护）
    QVector<QVector<std::array<int, 2>>> m_courseClsOcc;
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

private:
    void placeFixed();
    void greedy();
    void minConflicts();
    double localCost(int s); // s 当前位置参与的 硬*权重+软
    void tryUpdateBest();

    Problem m_p;
    EngineOptions m_opt;
    QRandomGenerator m_rng;
    std::atomic<bool> *m_cancel;
    std::atomic<long> *m_iterCounter;

    int m_curHard = 0;
    double m_curSoft = 0.0;
    QVector<Placement> m_bestPlacements;
    int m_bestHard = kBestInit;
    double m_bestSoft = 0.0;
    static constexpr int kBestInit = 1 << 30;
};

} // namespace sched::engine
