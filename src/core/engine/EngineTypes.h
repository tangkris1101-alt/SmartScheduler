#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace sched {

// 排课引擎参数
struct EngineOptions {
    int runs = 4;                 // best-of-N：不同随机种子的独立求解轮数
    int maxIterationsPerRun = 20000; // 每轮 min-conflicts 迭代上限
    quint32 seed = 0;             // 随机种子；相同 输入+seed 结果可复现
    bool randomSeed = false;      // true 时忽略 seed，每次运行随机取种（多次实验用）
    bool placeAll = true;         // 无可行位置的会话也以最小冲突放入（true），否则记为未排
    // 一轮跑完仍有硬违例时的额外重试轮数：换随机起点重跑，取违例最少者。
    // 只在这一轮没清零时才付出代价，正常情况零开销。
    int hardRetries = 2;
};

// 单轮求解的分阶段耗时。仅当 SMARTSCHED_PROFILE=1 时收集，
// 关闭时 Problem 的 profile 指针为空，热路径上只多一次判空。
struct SolverProfile {
    double totalMs = 0.0;        // Solver::run() 整体
    double buildMs = 0.0;        // Problem 构造（展开会话 / 合班 / 预排 / 同时段组）
    double fixedMs = 0.0;        // placeFixed
    double greedyMs = 0.0;       // greedy
    double initEvalMs = 0.0;     // 贪心后的第一次 hardViolations + softScore
    double minConflictsMs = 0.0; // minConflicts
    double restoreMs = 0.0;      // restoreBest + 占用表重建
    double reportMs = 0.0;       // bestReport
    double scheduleMs = 0.0;     // bestSchedule

    // minConflicts 内部（与 minConflictsMs 为子集关系）
    double selectMs = 0.0;          // 构建 movable + 采样一个有代价的会话
    double candidateEvalMs = 0.0;   // 枚举全部候选位置（含 localSoft）
    double localSoftMs = 0.0;       // localSoft 内部（candidateEvalMs 的子集）
    double hardViolationMs = 0.0;   // hardViolations 内部
    double perturbMs = 0.0;         // 2% 随机扰动
    double snapshotMs = 0.0;        // tryUpdateBest 的最优快照

    // localSoft 内部各分项（与 localSoftMs 为子集关系）
    double localSoftProgressMs = 0.0;
    double localSoftEvenSpreadMs = 0.0;
    double localSoftAmPmMs = 0.0;
    double localSoftTeacherDailyMs = 0.0;
    double localSoftClassSpreadMs = 0.0;
    double localSoftPlanMs = 0.0;

    long iterations = 0;            // minConflicts 实际迭代次数
    int attempts = 0;               // attempt() 次数（含硬违例重试）
    long localSoftCalls = 0;
    long hardViolationCalls = 0;
};

// 排课质量报告
struct QualityReport {
    int totalSessions = 0;   // 节次会话总数（连课计 1，合班合并后计 1）
    int placedSessions = 0;  // 已放入课表的会话数
    int hardViolations = 0;  // 硬约束违例数（含未排会话，0 表示可行）
    double softScore = 0.0;  // 软约束总罚分（越低越好）
    QMap<QString, double> softBreakdown; // 软约束分项罚分
    QStringList unplaced;    // 未排出会话的明细（"1班·语文（连2节）"）

    bool operator==(const QualityReport &) const = default;
};

} // namespace sched
