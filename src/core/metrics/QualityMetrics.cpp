#include "QualityMetrics.h"

#include "model/School.h"

#include <algorithm>
#include <cmath>

namespace sched {

// 内部键：两个 id/序号（班级-课程、老师-课程、课程-天等）
struct QualityPairKey {
    qint64 a = -1;
    qint64 b = -1;

    bool operator==(const QualityPairKey &) const = default;
};

inline size_t qHash(const QualityPairKey &key, size_t seed = 0)
{
    return ::qHash(key.b, ::qHash(key.a, seed));
}

namespace {

constexpr double kEps = 1e-9;

// half: 0=整节，1=仅单周，2=仅双周；单双周各算半节
double halfWeight(int half)
{
    return half == 0 ? 1.0 : 0.5;
}

// 一条"授课发生"：某条排课结果落到某位任课老师身上
struct Occurrence {
    qint64 teacherId = -1;
    qint64 classId = -1;
    qint64 courseId = -1;
    int slot = -1;
    int day = 0;
    int half = 0;
};

// 同一天内 slot 连续的段
struct Run {
    int day = 0;
    int startSlot = -1;
    int startPeriod = 0; // 0 基
    int slotCount = 0;
    double periods = 0;  // 段节数（整节 1、单双周 0.5，单 slot 上限 1）
};

// 某位老师在某一节的占用（用于撞课判定）
struct SlotOccupancy {
    qint64 classId = -1;
    qint64 courseId = -1;
    int half = 0;
};

// 把 slot -> 节数 映射切成"同一天内连续"的段（跨天不算连续）
QVector<Run> splitRuns(const QHash<int, double> &slotPeriods, const TimeConfig &tc)
{
    QVector<int> slots;
    slots.reserve(slotPeriods.size());
    for (auto it = slotPeriods.constBegin(); it != slotPeriods.constEnd(); ++it)
        slots.append(it.key());
    std::sort(slots.begin(), slots.end());

    QVector<Run> runs;
    for (int slot : slots) {
        const int day = tc.slotDay(slot);
        if (!runs.isEmpty() && runs.last().day == day
            && runs.last().startSlot + runs.last().slotCount == slot) {
            Run &last = runs.last();
            last.slotCount++;
            last.periods += slotPeriods.value(slot);
            continue;
        }
        Run run;
        run.day = day;
        run.startSlot = slot;
        run.startPeriod = tc.slotPeriod(slot);
        run.slotCount = 1;
        run.periods = slotPeriods.value(slot);
        runs.append(run);
    }
    return runs;
}

// 连堂好位置：第 1-2 节（段起点 period 0）、第 4-5 节（段起点 period == morningPeriods-2），
// 或完全落在下午（段起点 period 不小于 morningPeriods，不跨午休）
bool isGoodChainStart(int startPeriod, int morningPeriods)
{
    return startPeriod == 0
        || startPeriod == morningPeriods - 2
        || startPeriod >= morningPeriods;
}

// 课程名含"体育"即视为体育课（开窗、周内均衡两项排除）
bool isPeCourse(const Course *course)
{
    return course && course->name.contains(QStringLiteral("体育"));
}

} // namespace

QualityMetricsResult computeQualityMetrics(const School &school, const QVector<qint64> &classFilter)
{
    const TimeConfig &tc = school.timeConfig;
    const int morning = tc.morningPeriods;
    const int daysPerWeek = std::max(1, tc.daysPerWeek);

    QSet<qint64> scope;
    for (qint64 id : classFilter)
        scope.insert(id);
    const bool wholeSchool = scope.isEmpty();
    const auto inScope = [&](qint64 classId) { return wholeSchool || scope.contains(classId); };

    // ---- 教学计划：(班级,课程) -> 任课老师；连堂计划 ----
    QHash<QualityPairKey, QVector<qint64>> planTeachers;
    QSet<QualityPairKey> noLoadPairs; // 不挂老师的格子（如第 9 节体育活动）
    for (const PlanEntry &e : school.planEntries) {
        if (e.noLoad)
            noLoadPairs.insert(QualityPairKey{e.classId, e.courseId});
        QVector<qint64> &teachers = planTeachers[QualityPairKey{e.classId, e.courseId}];
        for (qint64 t : e.teacherIds) {
            if (!teachers.contains(t))
                teachers.append(t);
        }
    }

    struct ChainPlan {
        int chainedCount = 0;
        int chainLength = 0;
    };
    QHash<QualityPairKey, ChainPlan> chainPlans;
    double plannedPeriods = 0; // 指标 1 分母
    for (const PlanEntry &e : school.planEntries) {
        if (!inScope(e.classId))
            continue;
        plannedPeriods += e.weeklyPeriods;
        if (e.chainedCount <= 0)
            continue;
        ChainPlan &cp = chainPlans[QualityPairKey{e.classId, e.courseId}];
        cp.chainedCount += e.chainedCount;
        cp.chainLength = std::max(cp.chainLength, std::max(1, e.chainLength));
    }

    // ---- 不排课限制 ----
    QHash<qint64, QSet<int>> courseBlocked;
    QHash<qint64, QSet<int>> teacherBlocked;
    for (const NoScheduleConstraint &c : school.noScheduleConstraints) {
        if (c.entityType == NoScheduleConstraint::EntityType::Course)
            courseBlocked[c.entityId].unite(c.slots);
        else if (c.entityType == NoScheduleConstraint::EntityType::Teacher)
            teacherBlocked[c.entityId].unite(c.slots);
    }

    // ---- 遍历范围内的排课结果 ----
    double placedPeriods = 0;
    int courseForbiddenPeriods = 0;
    int teacherForbiddenPeriods = 0;
    int majorTotal = 0;
    int majorMorning = 0;

    // (班级,课程) -> slot -> 节数 / slot -> 序号 / 天 -> slot 集合
    QHash<QualityPairKey, QHash<int, double>> ccSlotPeriods;
    QHash<QualityPairKey, QHash<int, int>> ccSlotOrdinal;
    QHash<QualityPairKey, QHash<int, QSet<int>>> ccDaySlots;
    // 老师 -> slot -> 节数（同一 slot 最多 1 节）；acad 版排除体育
    QHash<qint64, QHash<int, double>> teacherSlotPeriods;
    QHash<qint64, QHash<int, double>> teacherAcadPeriods;
    // 老师 -> (课程,班级) -> 该班该课的授课发生
    QHash<qint64, QHash<QualityPairKey, QVector<Occurrence>>> fairGroups;
    // 老师 -> slot -> 该老师在该节的占用（半周区分单双周；合班同节只算一次会话）
    QHash<qint64, QHash<int, QVector<SlotOccupancy>>> teacherSlotHalves;
    // 合班组内「同课程、两个班同节上课」的合法组合（如体育合班），撞课判定要豁免
    QSet<QString> combinedPairs;
    for (const CombinedGroup &cg : school.combinedGroups) {
        for (int i = 0; i < cg.entries.size(); ++i) {
            for (int j = i + 1; j < cg.entries.size(); ++j) {
                if (cg.entries[i].second != cg.entries[j].second)
                    continue; // 不同课程不算同一次上课
                qint64 a = cg.entries[i].first, b = cg.entries[j].first;
                if (a > b)
                    std::swap(a, b);
                combinedPairs.insert(QStringLiteral("%1|%2|%3").arg(a).arg(b).arg(cg.entries[i].second));
            }
        }
    }

    for (const ScheduleAssignment &a : school.schedule.assignments) {
        if (!inScope(a.classId))
            continue;

        const double weight = halfWeight(a.half);
        placedPeriods += weight;

        const QVector<qint64> teachers = planTeachers.value(QualityPairKey{a.classId, a.courseId});
        const Course *course = school.findCourse(a.courseId);
        const bool pe = isPeCourse(course);

        if (course && course->isMajor) {
            majorTotal++;
            if (tc.slotPeriod(a.slotIndex) < morning)
                majorMorning++;
        }
        if (courseBlocked.value(a.courseId).contains(a.slotIndex))
            courseForbiddenPeriods++;
        for (qint64 t : teachers) {
            if (teacherBlocked.value(t).contains(a.slotIndex)) {
                teacherForbiddenPeriods++;
                break; // 同一节多个老师命中只计 1 节
            }
        }

        const QualityPairKey ccKey{a.classId, a.courseId};
        QHash<int, double> &cc = ccSlotPeriods[ccKey];
        cc[a.slotIndex] = std::min(1.0, cc.value(a.slotIndex) + weight);
        const int day = tc.slotDay(a.slotIndex);
        ccDaySlots[ccKey][day].insert(a.slotIndex);

        // 不挂老师的格子：老师课时、撞课、公平性都不计它
        if (!noLoadPairs.contains(QualityPairKey{a.classId, a.courseId})) {
            for (qint64 t : teachers) {
                QHash<int, double> &all = teacherSlotPeriods[t];
                all[a.slotIndex] = std::min(1.0, all.value(a.slotIndex) + weight);
                if (!pe) {
                    QHash<int, double> &acad = teacherAcadPeriods[t];
                    acad[a.slotIndex] = std::min(1.0, acad.value(a.slotIndex) + weight);
                }
                teacherSlotHalves[t][a.slotIndex].append(SlotOccupancy{a.classId, a.courseId, a.half});
                fairGroups[t][QualityPairKey{a.courseId, a.classId}].append(
                    Occurrence{t, a.classId, a.courseId, a.slotIndex, day, a.half});
            }
        }
    }

    // ---- 指标 2：老师撞课（同一老师同一节次的冲突对数；合班同节豁免）----
    double clashPairs = 0;
    for (auto tit = teacherSlotHalves.constBegin(); tit != teacherSlotHalves.constEnd(); ++tit) {
        for (auto sit = tit.value().constBegin(); sit != tit.value().constEnd(); ++sit) {
            const QVector<SlotOccupancy> &occ = sit.value();
            for (int i = 0; i < occ.size(); ++i) {
                for (int j = i + 1; j < occ.size(); ++j) {
                    // 单周与双周不同周，不算冲突；其余组合（含整节与任意、单周与单周）都算
                    const bool paritySplit = (occ[i].half == 1 && occ[j].half == 2)
                                          || (occ[i].half == 2 && occ[j].half == 1);
                    if (paritySplit)
                        continue;
                    // 合班组内同课程的几个班同节上课是约定安排，不算撞课
                    if (occ[i].courseId == occ[j].courseId) {
                        qint64 a = occ[i].classId, b = occ[j].classId;
                        if (a > b)
                            std::swap(a, b);
                        if (combinedPairs.contains(
                                QStringLiteral("%1|%2|%3").arg(a).arg(b).arg(occ[i].courseId)))
                            continue;
                    }
                    clashPairs++;
                }
            }
        }
    }

    // ---- 指标 8：同一科一天上 3 节及以上 ----
    double sameCoursePerDay = 0;
    for (auto it = ccDaySlots.constBegin(); it != ccDaySlots.constEnd(); ++it) {
        for (auto dit = it.value().constBegin(); dit != it.value().constEnd(); ++dit) {
            if (dit.value().size() >= 3)
                sameCoursePerDay++;
        }
    }

    // ---- 指标 5/6：连堂计划 ----
    // 班级-课程的一周序号表（按全部 slot 升序，1 基）
    for (auto it = ccDaySlots.constBegin(); it != ccDaySlots.constEnd(); ++it) {
        QVector<int> slots;
        for (auto dit = it.value().constBegin(); dit != it.value().constEnd(); ++dit) {
            for (int slot : dit.value())
                slots.append(slot);
        }
        std::sort(slots.begin(), slots.end());
        slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
        QHash<int, int> &ordinals = ccSlotOrdinal[it.key()];
        for (int i = 0; i < slots.size(); ++i)
            ordinals[slots[i]] = i + 1;
    }

    double chainSatisfied = 0;    // 指标 5 分子
    double chainRequired = 0;     // 指标 5 分母
    double chainGoodSatisfied = 0; // 指标 6 分子
    double chainGoodRequired = 0;  // 指标 6 分母
    for (auto it = chainPlans.constBegin(); it != chainPlans.constEnd(); ++it) {
        const ChainPlan &cp = it.value();
        const QVector<Run> runs = splitRuns(ccSlotPeriods.value(it.key()), tc);

        int segments = 0;
        for (const Run &run : runs) {
            if (run.periods >= cp.chainLength - kEps)
                segments++;
        }
        chainSatisfied += std::min<double>(segments, cp.chainedCount);
        chainRequired += cp.chainedCount;

        if (cp.chainLength != 2)
            continue;
        for (const Run &run : runs) {
            const int windows = int(std::floor(run.periods + kEps)) / 2;
            for (int k = 0; k < windows; ++k) {
                chainGoodRequired++;
                if (isGoodChainStart(run.startPeriod + 2 * k, morning))
                    chainGoodSatisfied++;
            }
        }
    }

    // ---- 指标 9/10：老师日均节数与连上节数 ----
    double teacherFiveADay = 0;
    double teacherThreeInRow = 0;
    for (auto tit = teacherSlotPeriods.constBegin(); tit != teacherSlotPeriods.constEnd(); ++tit) {
        const QHash<int, double> &slotPeriods = tit.value();
        QHash<int, QVector<int>> byDay;
        for (auto sit = slotPeriods.constBegin(); sit != slotPeriods.constEnd(); ++sit)
            byDay[tc.slotDay(sit.key())].append(sit.key());
        for (auto dit = byDay.constBegin(); dit != byDay.constEnd(); ++dit) {
            QVector<int> slots = dit.value();
            std::sort(slots.begin(), slots.end());

            double dayPeriods = 0;
            for (int slot : slots)
                dayPeriods += slotPeriods.value(slot);
            if (dayPeriods >= 5.0 - kEps)
                teacherFiveADay++;

            // 午休隔开的不算连续：上午块与下午块分别找连续段
            double runPeriods = 0;
            for (int i = 0; i < slots.size(); ++i) {
                bool continues = false;
                if (i > 0 && slots[i] == slots[i - 1] + 1) {
                    const bool prevMorning = tc.slotPeriod(slots[i - 1]) < morning;
                    const bool curMorning = tc.slotPeriod(slots[i]) < morning;
                    continues = (prevMorning == curMorning);
                }
                if (!continues && runPeriods > 0) {
                    if (runPeriods >= 3.0 - kEps)
                        teacherThreeInRow++;
                    runPeriods = 0;
                }
                runPeriods += slotPeriods.value(slots[i]);
            }
            if (runPeriods >= 3.0 - kEps)
                teacherThreeInRow++;
        }
    }

    // ---- 老师五项：教案连续性 / 教师开窗 / 周内均衡 / 平行班先后公平 ----
    // (老师,课程) -> 天 -> 该组课的序号集合
    QHash<qint64, QHash<QualityPairKey, QVector<int>>> contGroups;
    for (auto tit = fairGroups.constBegin(); tit != fairGroups.constEnd(); ++tit) {
        for (auto git = tit.value().constBegin(); git != tit.value().constEnd(); ++git) {
            const qint64 courseId = git.key().a;
            const qint64 classId = git.key().b;
            for (const Occurrence &o : git.value()) {
                contGroups[tit.key()][QualityPairKey{courseId, o.day}].append(
                    ccSlotOrdinal.value(QualityPairKey{classId, courseId}).value(o.slot, 1));
            }
        }
    }

    // 教案连续性：组内序号极差的平均 D̄ → C = 1 / (1 + D̄)
    const auto continuityOf = [&](qint64 teacherId) {
        const auto it = contGroups.constFind(teacherId);
        if (it == contGroups.constEnd())
            return 1.0;
        double spreadSum = 0;
        int groupCount = 0;
        for (auto git = it.value().constBegin(); git != it.value().constEnd(); ++git) {
            const QVector<int> &ordinals = git.value();
            if (ordinals.isEmpty())
                continue;
            const auto mm = std::minmax_element(ordinals.constBegin(), ordinals.constEnd());
            spreadSum += double(*mm.second - *mm.first);
            groupCount++;
        }
        return groupCount > 0 ? 1.0 / (1.0 + spreadSum / groupCount) : 1.0;
    };

    // 教师开窗：F = (连续块数 - 1) + q(pmax) - q(pmin)，按该老师有课的天平均
    const auto windowOf = [&](qint64 teacherId) {
        const auto it = teacherAcadPeriods.constFind(teacherId);
        if (it == teacherAcadPeriods.constEnd())
            return 0.0;
        QHash<int, QVector<int>> byDay;
        for (auto sit = it.value().constBegin(); sit != it.value().constEnd(); ++sit)
            byDay[tc.slotDay(sit.key())].append(tc.slotPeriod(sit.key()) + 1); // 1 基节次
        if (byDay.isEmpty())
            return 0.0;

        const auto q = [&](int period) { return period <= morning ? period : period + 1; };
        double sum = 0;
        for (auto dit = byDay.constBegin(); dit != byDay.constEnd(); ++dit) {
            QVector<int> periods = dit.value();
            std::sort(periods.begin(), periods.end());
            int blocks = 0;
            for (int i = 0; i < periods.size(); ++i) {
                if (i == 0 || periods[i] != periods[i - 1] + 1)
                    blocks++;
            }
            sum += (blocks - 1) + (q(periods.last()) - q(periods.first()));
        }
        return sum / byDay.size();
    };

    // 周内均衡：E = Σ|x_d - x̄| - D_min，D_min = 2·r·(N-r)/N
    const auto balanceOf = [&](qint64 teacherId) {
        const auto it = teacherAcadPeriods.constFind(teacherId);
        if (it == teacherAcadPeriods.constEnd())
            return 0.0;
        QHash<int, double> byDay;
        double total = 0;
        for (auto sit = it.value().constBegin(); sit != it.value().constEnd(); ++sit) {
            byDay[tc.slotDay(sit.key())] += sit.value();
            total += sit.value();
        }
        const double avg = total / daysPerWeek;
        double deviation = 0;
        for (int day = 0; day < tc.daysPerWeek; ++day)
            deviation += std::abs(byDay.value(day, 0.0) - avg);

        const int n = daysPerWeek;
        const int l = int(std::lround(total));
        const int r = ((l % n) + n) % n;
        const double unavoidable = 2.0 * r * (n - r) / n;
        return std::max(0.0, deviation - unavoidable);
    };

    // 平行班先后公平：按"位次"分布计算 B = ½·Σ_p |v[班][p] - n_班/M|，按 (老师,课程,班) 平均
    const auto fairnessOf = [&](qint64 teacherId) {
        const auto it = fairGroups.constFind(teacherId);
        if (it == fairGroups.constEnd())
            return 0.0;

        QHash<qint64, QHash<qint64, QVector<Occurrence>>> byCourse;
        for (auto git = it.value().constBegin(); git != it.value().constEnd(); ++git)
            byCourse[git.key().a][git.key().b] = git.value();

        double fairnessSum = 0;
        int classCount = 0;
        for (auto cit = byCourse.constBegin(); cit != byCourse.constEnd(); ++cit) {
            const QHash<qint64, QVector<Occurrence>> &byClass = cit.value();
            const int m = int(byClass.size());
            if (m < 2)
                continue; // 单班无"先后"可言

            // 天 -> (班级 -> 当天该课第一个 slot)
            QHash<int, QHash<qint64, int>> firstByDay;
            for (auto bit = byClass.constBegin(); bit != byClass.constEnd(); ++bit) {
                for (const Occurrence &o : bit.value()) {
                    QHash<qint64, int> &perDay = firstByDay[o.day];
                    const auto found = perDay.constFind(bit.key());
                    if (found == perDay.constEnd())
                        perDay.insert(bit.key(), o.slot);
                    else if (o.slot < *found)
                        perDay[bit.key()] = o.slot;
                }
            }

            // 班级 -> 位次 -> 出现次数（同一起始 slot 并列同名次）
            QHash<qint64, QHash<int, int>> ranks;
            for (auto dit = firstByDay.constBegin(); dit != firstByDay.constEnd(); ++dit) {
                const QHash<qint64, int> &firsts = dit.value();
                for (auto f1 = firsts.constBegin(); f1 != firsts.constEnd(); ++f1) {
                    int rank = 1;
                    for (auto f2 = firsts.constBegin(); f2 != firsts.constEnd(); ++f2) {
                        if (f2.value() < f1.value())
                            rank++;
                    }
                    ranks[f1.key()][rank]++;
                }
            }

            for (auto bit = byClass.constBegin(); bit != byClass.constEnd(); ++bit) {
                double sessions = 0;
                for (const Occurrence &o : bit.value())
                    sessions += halfWeight(o.half);
                const double ideal = sessions / m;
                const QHash<int, int> &v = ranks.value(bit.key());
                double deviation = 0;
                for (int p = 1; p <= m; ++p)
                    deviation += std::abs(double(v.value(p, 0)) - ideal);
                fairnessSum += 0.5 * deviation;
                classCount++;
            }
        }
        return classCount > 0 ? fairnessSum / classCount : 0.0;
    };

    QualityMetricsResult result;
    for (auto tit = teacherSlotPeriods.constBegin(); tit != teacherSlotPeriods.constEnd(); ++tit) {
        const qint64 teacherId = tit.key();
        TeacherQuality quality;
        quality.teacherId = teacherId;
        const Teacher *teacher = school.findTeacher(teacherId);
        quality.name = teacher ? teacher->name : QString();

        double load = 0;
        for (auto sit = tit.value().constBegin(); sit != tit.value().constEnd(); ++sit)
            load += sit.value();
        quality.weeklyPeriods = int(std::lround(load));
        quality.continuity = continuityOf(teacherId);
        quality.window = windowOf(teacherId);
        quality.balance = balanceOf(teacherId);
        quality.fairness = fairnessOf(teacherId);
        quality.composite = quality.window + quality.balance + quality.fairness;
        result.teachers.append(quality);
    }
    std::sort(result.teachers.begin(), result.teachers.end(),
              [](const TeacherQuality &a, const TeacherQuality &b) {
                  if (a.weeklyPeriods != b.weeklyPeriods)
                      return a.weeklyPeriods > b.weeklyPeriods;
                  if (a.name != b.name)
                      return a.name < b.name;
                  return a.teacherId < b.teacherId;
              });

    if (!result.teachers.isEmpty()) {
        const double n = result.teachers.size();
        for (const TeacherQuality &t : result.teachers) {
            result.avgContinuity += t.continuity;
            result.avgWindow += t.window;
            result.avgBalance += t.balance;
            result.avgFairness += t.fairness;
            result.avgComposite += t.composite;
        }
        result.avgContinuity /= n;
        result.avgWindow /= n;
        result.avgBalance /= n;
        result.avgFairness /= n;
        result.avgComposite /= n;
    }

    const auto addIndicator = [&result](const QString &key, const QString &name, bool hard,
                                        bool higherBetter, double value, double required,
                                        const QString &unit, bool isRatio) {
        QualityIndicator indicator;
        indicator.key = key;
        indicator.name = name;
        indicator.hard = hard;
        indicator.higherBetter = higherBetter;
        indicator.value = value;
        indicator.required = required;
        indicator.unit = unit;
        indicator.isRatio = isRatio;
        result.indicators.append(indicator);
    };

    // ---- 十项指标 ----
    addIndicator(QStringLiteral("placedPeriods"), QStringLiteral("排上的节数"), true, true,
                 placedPeriods, plannedPeriods, QStringLiteral("节"), true);
    addIndicator(QStringLiteral("teacherClash"), QStringLiteral("老师撞课"), true, false,
                 clashPairs, 0, QStringLiteral("处"), false);
    addIndicator(QStringLiteral("courseForbiddenSlot"), QStringLiteral("学科排进了不排的时段"), true,
                 false, courseForbiddenPeriods, 0, QStringLiteral("节"), false);
    addIndicator(QStringLiteral("teacherForbiddenSlot"), QStringLiteral("老师排进了不排的时段"), true,
                 false, teacherForbiddenPeriods, 0, QStringLiteral("节"), false);
    addIndicator(QStringLiteral("chainedPlaced"), QStringLiteral("该连堂的连上了"), true, true,
                 chainSatisfied, chainRequired, QStringLiteral("节"), true);
    addIndicator(QStringLiteral("chainGoodSlot"), QStringLiteral("连堂落在第 1-2 或 4-5 节"), false,
                 true, chainGoodSatisfied, chainGoodRequired, QStringLiteral("节"), true);
    addIndicator(QStringLiteral("majorMorning"), QStringLiteral("语数英排在上午"), false, true,
                 majorMorning, majorTotal, QStringLiteral("节"), true);
    addIndicator(QStringLiteral("sameCoursePerDay"), QStringLiteral("同一科一天上 3 节及以上"),
                 false, false, sameCoursePerDay, 0, QStringLiteral("次"), false);
    addIndicator(QStringLiteral("teacherDailyLoad"), QStringLiteral("老师一天上 5 节及以上"), false,
                 false, teacherFiveADay, 0, QStringLiteral("人·天"), false);
    addIndicator(QStringLiteral("teacherRunLength"), QStringLiteral("老师连上 3 节及以上"), false,
                 false, teacherThreeInRow, 0, QStringLiteral("次"), false);

    return result;
}

} // namespace sched
