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
