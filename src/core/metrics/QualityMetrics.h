#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

namespace sched {

class School;

// 单条排课质量指标（十项）：value/required 构成分数，isRatio 为真时 value/required 有意义
struct QualityIndicator {
    QString key;               // 稳定英文 key
    QString name;              // 中文显示名
    bool hard = true;          // true=硬指标（不许破），false=软指标（舒适度）
    bool higherBetter = false; // true=越大越好（排上的节数/连上率这类）
    double value = 0;          // 实测值
    double required = 0;       // 分母/应有值；无分母时为 0
    QString unit;              // "节" / "处" / "次" / "人·天"
    bool isRatio = false;      // true 表示 value/required 是有意义的分数
};

// 单个有课老师的五项质量指标（除 weeklyPeriods/continuity 外均越低越好）
struct TeacherQuality {
    qint64 teacherId = -1;
    QString name;
    int weeklyPeriods = 0;  // 该老师一周节数
    double continuity = 0;  // 教案连续性，越接近 1 越好
    double window = 0;      // 教师开窗，越低越好
    double balance = 0;     // 周内均衡，越低越好
    double fairness = 0;    // 平行班先后公平，越低越好
    double composite = 0;   // 综合 = window + balance + fairness，越低越好
};

struct QualityMetricsResult {
    QVector<QualityIndicator> indicators; // 十项（硬指标在前）
    QVector<TeacherQuality> teachers;     // 每位有课老师（按 weeklyPeriods 降序）
    double avgContinuity = 0;             // 五项平均（有课老师）
    double avgWindow = 0;
    double avgBalance = 0;
    double avgFairness = 0;
    double avgComposite = 0;
};

// 计算排课质量指标：classFilter 为空表示全校，否则只统计这些班级（用于"按年级"）
QualityMetricsResult computeQualityMetrics(const School &school,
                                           const QVector<qint64> &classFilter = {});

} // namespace sched
