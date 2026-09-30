#include "metrics/QualityMetrics.h"
#include "model/School.h"

#include <QtTest/QTest>

#include <cmath>

using namespace sched;

namespace {

// 5 天 × 7 节，上午 5 节（与质量报告算例一致）
void initTime(School &school)
{
    school.timeConfig.daysPerWeek = 5;
    school.timeConfig.periodsPerDay = 7;
    school.timeConfig.morningPeriods = 5;
    school.timeConfig.weeksPerTerm = 20;
}

qint64 addClass(School &school, const QString &name)
{
    ClassInfo c;
    c.name = name;
    return school.addClass(c);
}

qint64 addCourse(School &school, const QString &name, bool major)
{
    Course c;
    c.name = name;
    c.isMajor = major;
    return school.addCourse(c);
}

qint64 addTeacher(School &school, const QString &name)
{
    Teacher t;
    t.name = name;
    return school.addTeacher(t);
}

void plan(School &school, qint64 classId, qint64 courseId, const QVector<qint64> &teacherIds,
          double weeklyPeriods, int chainedCount = 0, int chainLength = 2)
{
    PlanEntry e;
    e.classId = classId;
    e.courseId = courseId;
    e.teacherIds = teacherIds;
    e.weeklyPeriods = weeklyPeriods;
    e.chainedCount = chainedCount;
    e.chainLength = chainLength;
    school.addPlanEntry(e);
}

void place(School &school, qint64 classId, int slot, qint64 courseId, int half = 0)
{
    school.schedule.setAssignment(classId, slot, half, courseId);
}

void placeSlots(School &school, qint64 classId, qint64 courseId, const QVector<int> &slots,
                int half = 0)
{
    for (int slot : slots)
        place(school, classId, slot, courseId, half);
}

void addForbidden(School &school, NoScheduleConstraint::EntityType type, qint64 entityId,
                  const QSet<int> &slots)
{
    NoScheduleConstraint c;
    c.entityType = type;
    c.entityId = entityId;
    c.slots = slots;
    school.addNoScheduleConstraint(c);
}

const QualityIndicator *indicator(const QualityMetricsResult &result, const QString &key)
{
    for (const QualityIndicator &i : result.indicators) {
        if (i.key == key)
            return &i;
    }
    return nullptr;
}

const TeacherQuality *teacherQuality(const QualityMetricsResult &result, qint64 teacherId)
{
    for (const TeacherQuality &t : result.teachers) {
        if (t.teacherId == teacherId)
            return &t;
    }
    return nullptr;
}

double indicatorValue(const QualityMetricsResult &result, const QString &key)
{
    const QualityIndicator *i = indicator(result, key);
    return i ? i->value : -1;
}

double indicatorRequired(const QualityMetricsResult &result, const QString &key)
{
    const QualityIndicator *i = indicator(result, key);
    return i ? i->required : -1;
}

bool near(double a, double b, double eps = 1e-9)
{
    return std::abs(a - b) <= eps;
}

QString num(double v)
{
    return QString::number(v, 'g', 12);
}

} // namespace

// 排课质量指标单元测试（复刻人工排课质量报告的算例）
class TestMetrics : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void indicatorCatalog();
    void placedPeriodsAndHalfWeek();
    void containerWindow();
    void weeklyBalance();
    void lessonContinuity();
    void parallelFairness();
    void chainInterpretation();
    void mixedSchoolSmoke();
    void hardIndicators();
    void teacherClashParity();
    void softIndicators();
    void classFilterScope();
    void teacherRanking();
};

// 十项指标的 key/名称/单位/硬软属性
void TestMetrics::indicatorCatalog()
{
    School school;
    initTime(school);

    const QualityMetricsResult empty = computeQualityMetrics(school);
    QCOMPARE(empty.indicators.size(), 10);
    QCOMPARE(empty.teachers.size(), 0);
    QVERIFY(near(empty.avgContinuity, 0.0));
    QVERIFY(near(empty.avgWindow, 0.0));
    QVERIFY(near(empty.avgBalance, 0.0));
    QVERIFY(near(empty.avgFairness, 0.0));
    QVERIFY(near(empty.avgComposite, 0.0));

    const QStringList keys = {
        QStringLiteral("placedPeriods"),
        QStringLiteral("teacherClash"),
        QStringLiteral("courseForbiddenSlot"),
        QStringLiteral("teacherForbiddenSlot"),
        QStringLiteral("chainedPlaced"),
        QStringLiteral("chainGoodSlot"),
        QStringLiteral("majorMorning"),
        QStringLiteral("sameCoursePerDay"),
        QStringLiteral("teacherDailyLoad"),
        QStringLiteral("teacherRunLength"),
    };
    const QStringList names = {
        QStringLiteral("排上的节数"),
        QStringLiteral("老师撞课"),
        QStringLiteral("学科排进了不排的时段"),
        QStringLiteral("老师排进了不排的时段"),
        QStringLiteral("该连堂的连上了"),
        QStringLiteral("连堂落在第 1-2 或 4-5 节"),
        QStringLiteral("语数英排在上午"),
        QStringLiteral("同一科一天上 3 节及以上"),
        QStringLiteral("老师一天上 5 节及以上"),
        QStringLiteral("老师连上 3 节及以上"),
    };
    for (int i = 0; i < keys.size(); ++i) {
        QCOMPARE(empty.indicators[i].key, keys[i]);
        QCOMPARE(empty.indicators[i].name, names[i]);
    }

    // 硬指标 1-5、软指标 6-10
    for (int i = 0; i < 5; ++i)
        QVERIFY(empty.indicators[i].hard);
    for (int i = 5; i < 10; ++i)
        QVERIFY(!empty.indicators[i].hard);

    QCOMPARE(empty.indicators[0].unit, QStringLiteral("节"));
    QCOMPARE(empty.indicators[1].unit, QStringLiteral("处"));
    QCOMPARE(empty.indicators[6].unit, QStringLiteral("节"));
    QCOMPARE(empty.indicators[7].unit, QStringLiteral("次"));
    QCOMPARE(empty.indicators[8].unit, QStringLiteral("人·天"));
    QCOMPARE(empty.indicators[9].unit, QStringLiteral("次"));

    // 只有"越多越好"的项带 higherBetter，且 1/5/6/7 是有意义的分数
    for (int i = 0; i < 10; ++i) {
        const bool expectHigher = (i == 0 || i == 4 || i == 5 || i == 6);
        QCOMPARE(empty.indicators[i].higherBetter, expectHigher);
        const bool expectRatio = (i == 0 || i == 4 || i == 5 || i == 6);
        QCOMPARE(empty.indicators[i].isRatio, expectRatio);
    }
}

// 指标 1：排上的节数（单双周各算半节）
void TestMetrics::placedPeriodsAndHalfWeek()
{
    School school;
    initTime(school);
    const qint64 c1 = addClass(school, QStringLiteral("1班"));
    const qint64 yingyu = addCourse(school, QStringLiteral("英语"), true);
    const qint64 li = addTeacher(school, QStringLiteral("李"));

    // 3 节：两个整节 + 一节单双周分排
    plan(school, c1, yingyu, {li}, 3);
    place(school, c1, 0, yingyu, 0);
    place(school, c1, 1, yingyu, 0);
    place(school, c1, 2, yingyu, 1); // 单周
    place(school, c1, 2, yingyu, 2); // 双周

    const QualityMetricsResult result = computeQualityMetrics(school);
    QVERIFY(near(indicatorValue(result, QStringLiteral("placedPeriods")), 3.0));
    QVERIFY(near(indicatorRequired(result, QStringLiteral("placedPeriods")), 3.0));
    QVERIFY(near(indicator(result, QStringLiteral("placedPeriods"))->value
                     / indicator(result, QStringLiteral("placedPeriods"))->required,
                 1.0));
    QCOMPARE(teacherQuality(result, li)->weeklyPeriods, 3);
}

// 指标口径：教师开窗 F = (块数-1) + q(pmax) - q(pmin)，周一 2、周二 9 → 平均 5.5
void TestMetrics::containerWindow()
{
    School school;
    initTime(school);
    const qint64 c1 = addClass(school, QStringLiteral("1班"));
    const qint64 yuwen = addCourse(school, QStringLiteral("语文"), true);
    const qint64 zhang = addTeacher(school, QStringLiteral("张"));

    plan(school, c1, yuwen, {zhang}, 6);
    placeSlots(school, c1, yuwen, {0, 1, 2});   // 周一第 1、2、3 节 → F = 2
    placeSlots(school, c1, yuwen, {7, 10, 13}); // 周二第 1、4、7 节 → F = 9

    const QualityMetricsResult result = computeQualityMetrics(school);
    const TeacherQuality *tq = teacherQuality(result, zhang);
    QVERIFY(tq);
    QCOMPARE(tq->weeklyPeriods, 6);
    QVERIFY2(near(tq->window, 5.5), qPrintable(num(tq->window)));
    QVERIFY2(near(result.avgWindow, 5.5), qPrintable(num(result.avgWindow)));
}

// 指标口径：周内均衡 E = Σ|x_d - x̄| - D_min
void TestMetrics::weeklyBalance()
{
    School school;
    initTime(school);
    const qint64 c1 = addClass(school, QStringLiteral("1班"));
    const qint64 c2 = addClass(school, QStringLiteral("2班"));
    const qint64 shuxue = addCourse(school, QStringLiteral("数学"), true);
    const qint64 yuwen = addCourse(school, QStringLiteral("语文"), true);
    const qint64 li = addTeacher(school, QStringLiteral("李"));
    const qint64 wang = addTeacher(school, QStringLiteral("王"));

    // 李：(3,3,2,2,2) → 无不可避免不均 → E = 0
    plan(school, c1, shuxue, {li}, 12);
    placeSlots(school, c1, shuxue, {0, 1, 2, 7, 8, 9, 14, 15, 21, 22, 28, 29});
    // 王：(5,1,2,2,2) → L=12、r=2、D_min=2.4 → E = 5.2 - 2.4 = 2.8
    plan(school, c2, yuwen, {wang}, 12);
    placeSlots(school, c2, yuwen, {0, 1, 2, 3, 4, 7, 14, 15, 21, 22, 28, 29});

    const QualityMetricsResult result = computeQualityMetrics(school);
    const TeacherQuality *liQ = teacherQuality(result, li);
    const TeacherQuality *wangQ = teacherQuality(result, wang);
    QVERIFY(liQ && wangQ);
    QVERIFY2(near(liQ->balance, 0.0), qPrintable(num(liQ->balance)));
    QVERIFY2(near(wangQ->balance, 2.8), qPrintable(num(wangQ->balance)));
    QVERIFY(near(result.avgBalance, 1.4));
}

// 指标口径：教案连续性 C = 1 / (1 + D̄)
void TestMetrics::lessonContinuity()
{
    School school;
    initTime(school);
    const qint64 c1 = addClass(school, QStringLiteral("1班"));
    const qint64 c2 = addClass(school, QStringLiteral("2班"));
    const qint64 yuwen = addCourse(school, QStringLiteral("语文"), true);
    const qint64 shuxue = addCourse(school, QStringLiteral("数学"), true);
    const qint64 zhang = addTeacher(school, QStringLiteral("张"));
    const qint64 li = addTeacher(school, QStringLiteral("李"));

    // 张：1班语文序号 1、2（周一第 1、2 节）与 3（周二第 5 节）
    // → 周一 D = 1、周二 D = 0 → D̄ = 0.5 → C = 1/1.5 ≈ 0.667
    plan(school, c1, yuwen, {zhang}, 3);
    placeSlots(school, c1, yuwen, {0, 1, 11});
    // 李：2班数学每天 1 节（序号 1、2、3）→ 每组 D = 0 → C = 1
    plan(school, c2, shuxue, {li}, 3);
    placeSlots(school, c2, shuxue, {0, 7, 14});

    const QualityMetricsResult result = computeQualityMetrics(school);
    const TeacherQuality *zhangQ = teacherQuality(result, zhang);
    const TeacherQuality *liQ = teacherQuality(result, li);
    QVERIFY(zhangQ && liQ);
    QVERIFY2(qFuzzyCompare(zhangQ->continuity, 1.0 / 1.5), qPrintable(num(zhangQ->continuity)));
    QVERIFY2(near(liQ->continuity, 1.0), qPrintable(num(liQ->continuity)));
}

// 指标口径：平行班先后公平 B = ½·Σ_p |v[班][p] - n_班/M|
void TestMetrics::parallelFairness()
{
    // (a) 2 班总在 1 班之后（固定"试讲班"）→ B = 2
    {
        School school;
        initTime(school);
        const qint64 c1 = addClass(school, QStringLiteral("1班"));
        const qint64 c2 = addClass(school, QStringLiteral("2班"));
        const qint64 shuxue = addCourse(school, QStringLiteral("数学"), true);
        const qint64 zhang = addTeacher(school, QStringLiteral("张"));

        plan(school, c1, shuxue, {zhang}, 4);
        plan(school, c2, shuxue, {zhang}, 4);
        placeSlots(school, c1, shuxue, {0, 7, 14, 21});
        placeSlots(school, c2, shuxue, {1, 8, 15, 22});

        const QualityMetricsResult result = computeQualityMetrics(school);
        const TeacherQuality *tq = teacherQuality(result, zhang);
        QVERIFY(tq);
        QVERIFY2(near(tq->fairness, 2.0), qPrintable(num(tq->fairness)));
        QVERIFY(near(tq->composite, tq->window + tq->balance + tq->fairness));
    }

    // (b) 两班轮流在先 → B = 0
    {
        School school;
        initTime(school);
        const qint64 c1 = addClass(school, QStringLiteral("1班"));
        const qint64 c2 = addClass(school, QStringLiteral("2班"));
        const qint64 shuxue = addCourse(school, QStringLiteral("数学"), true);
        const qint64 zhang = addTeacher(school, QStringLiteral("张"));

        plan(school, c1, shuxue, {zhang}, 4);
        plan(school, c2, shuxue, {zhang}, 4);
        placeSlots(school, c1, shuxue, {0, 8, 14, 22});
        placeSlots(school, c2, shuxue, {1, 7, 15, 21});

        const QualityMetricsResult result = computeQualityMetrics(school);
        const TeacherQuality *tq = teacherQuality(result, zhang);
        QVERIFY(tq);
        QVERIFY2(near(tq->fairness, 0.0), qPrintable(num(tq->fairness)));
    }
}

// 连堂口径：4 节连成一段时，硬指标按"段长 ≥ chainLength 记 1 段"，软指标按 2 节窗口计数
void TestMetrics::chainInterpretation()
{
    // 一段 4 节：硬指标 1 段（< 计划的 2 次连堂）；软指标 2 个窗口，只有第 1 个落在第 1-2 节
    {
        School school;
        initTime(school);
        const qint64 c1 = addClass(school, QStringLiteral("1班"));
        const qint64 yuwen = addCourse(school, QStringLiteral("语文"), true);
        const qint64 zhang = addTeacher(school, QStringLiteral("张"));

        plan(school, c1, yuwen, {zhang}, 4, 2, 2);
        placeSlots(school, c1, yuwen, {0, 1, 2, 3}); // 周一第 1-4 节连成一段

        const QualityMetricsResult result = computeQualityMetrics(school);
        QVERIFY(near(indicatorValue(result, QStringLiteral("chainedPlaced")), 1.0));
        QVERIFY(near(indicatorRequired(result, QStringLiteral("chainedPlaced")), 2.0));
        QVERIFY(near(indicatorValue(result, QStringLiteral("chainGoodSlot")), 1.0));
        QVERIFY(near(indicatorRequired(result, QStringLiteral("chainGoodSlot")), 2.0));
        // 一天同科 4 节 → 1 次
        QVERIFY(near(indicatorValue(result, QStringLiteral("sameCoursePerDay")), 1.0));
    }

    // 完全落在下午的连堂也算好位置（不跨午休）
    {
        School school;
        initTime(school);
        const qint64 c1 = addClass(school, QStringLiteral("1班"));
        const qint64 yuwen = addCourse(school, QStringLiteral("语文"), true);
        const qint64 zhang = addTeacher(school, QStringLiteral("张"));

        plan(school, c1, yuwen, {zhang}, 2, 1, 2);
        placeSlots(school, c1, yuwen, {5, 6}); // 周一第 6-7 节（下午）

        const QualityMetricsResult result = computeQualityMetrics(school);
        QVERIFY(near(indicatorValue(result, QStringLiteral("chainedPlaced")), 1.0));
        QVERIFY(near(indicatorRequired(result, QStringLiteral("chainedPlaced")), 1.0));
        QVERIFY(near(indicatorValue(result, QStringLiteral("chainGoodSlot")), 1.0));
        QVERIFY(near(indicatorRequired(result, QStringLiteral("chainGoodSlot")), 1.0));
    }

    // 跨午休的两节（上午末节 + 下午首节）：按口径算一段连堂，但不算好位置
    {
        School school;
        initTime(school);
        const qint64 c1 = addClass(school, QStringLiteral("1班"));
        const qint64 yuwen = addCourse(school, QStringLiteral("语文"), true);
        const qint64 zhang = addTeacher(school, QStringLiteral("张"));

        plan(school, c1, yuwen, {zhang}, 2, 1, 2);
        placeSlots(school, c1, yuwen, {4, 5}); // 周一第 5 节 + 第 6 节

        const QualityMetricsResult result = computeQualityMetrics(school);
        QVERIFY(near(indicatorValue(result, QStringLiteral("chainedPlaced")), 1.0));
        QVERIFY(near(indicatorRequired(result, QStringLiteral("chainedPlaced")), 1.0));
        QVERIFY(near(indicatorValue(result, QStringLiteral("chainGoodSlot")), 0.0));
        QVERIFY(near(indicatorRequired(result, QStringLiteral("chainGoodSlot")), 1.0));
    }
}

// 混合场景冒烟：合班同节、连堂、单双周、多个班同时带入
void TestMetrics::mixedSchoolSmoke()
{
    School school;
    initTime(school);
    const qint64 c1 = addClass(school, QStringLiteral("1班"));
    const qint64 c2 = addClass(school, QStringLiteral("2班"));
    const qint64 c3 = addClass(school, QStringLiteral("3班"));
    const qint64 yuwen = addCourse(school, QStringLiteral("语文"), true);
    const qint64 shuxue = addCourse(school, QStringLiteral("数学"), true);
    const qint64 tiyu = addCourse(school, QStringLiteral("体育"), false);
    const qint64 zhang = addTeacher(school, QStringLiteral("张"));
    const qint64 wang = addTeacher(school, QStringLiteral("王"));
    const qint64 chen = addTeacher(school, QStringLiteral("陈"));

    plan(school, c1, yuwen, {zhang}, 5, 1, 2);
    plan(school, c2, yuwen, {zhang}, 5, 1, 2);
    plan(school, c3, yuwen, {zhang}, 4);
    plan(school, c1, shuxue, {wang}, 5);
    plan(school, c2, shuxue, {wang}, 5);
    plan(school, c3, shuxue, {wang}, 6);
    plan(school, c1, tiyu, {chen}, 2); // 合班：1、2 班同节体育
    plan(school, c2, tiyu, {chen}, 2);

    placeSlots(school, c1, yuwen, {0, 1, 7, 14, 21}); // 含周一第 1-2 节连堂
    placeSlots(school, c2, yuwen, {0, 1, 8, 15, 22});
    placeSlots(school, c3, yuwen, {2, 9, 16, 23});
    placeSlots(school, c1, shuxue, {2, 3, 9, 16, 30});
    placeSlots(school, c2, shuxue, {2, 3, 10, 17, 31});
    placeSlots(school, c3, shuxue, {3, 10, 17, 24, 31});
    placeSlots(school, c1, tiyu, {4, 5});
    placeSlots(school, c2, tiyu, {4, 5});
    place(school, c3, 25, shuxue, 1); // 单周加半节
    place(school, c3, 25, shuxue, 2); // 双周加半节

    const QualityMetricsResult result = computeQualityMetrics(school);
    QCOMPARE(result.indicators.size(), 10);
    for (const QualityIndicator &i : result.indicators) {
        QVERIFY2(std::isfinite(i.value), qPrintable(i.key));
        QVERIFY2(std::isfinite(i.required), qPrintable(i.key));
        QVERIFY2(i.value >= -1e-9, qPrintable(i.key));
        QVERIFY2(i.required >= -1e-9, qPrintable(i.key));
    }
    // 34 节计划全部排出（3班数学单双周各半节合成一节）
    QVERIFY(near(indicatorValue(result, QStringLiteral("placedPeriods")), 34.0));
    QVERIFY(near(indicatorRequired(result, QStringLiteral("placedPeriods")), 34.0));
    QCOMPARE(result.teachers.size(), 3);
    for (const TeacherQuality &tq : result.teachers)
        QVERIFY(near(tq.composite, tq.window + tq.balance + tq.fairness));
    // 张：14 课时但 1、2 班同节重合，按实际占用节次计 12 节
    QCOMPARE(teacherQuality(result, zhang)->weeklyPeriods, 12);
    // 陈：合班体育 4 课时、2 个实际节次；体育不计入开窗与周内均衡
    const TeacherQuality *chenQ = teacherQuality(result, chen);
    QCOMPARE(chenQ->weeklyPeriods, 2);
    QVERIFY(near(chenQ->window, 0.0));
    QVERIFY(near(chenQ->balance, 0.0));
}

// 硬指标：撞课 / 不排时段 / 连堂
void TestMetrics::hardIndicators()
{
    School school;
    initTime(school);
    const qint64 c1 = addClass(school, QStringLiteral("1班"));
    const qint64 c2 = addClass(school, QStringLiteral("2班"));
    const qint64 yuwen = addCourse(school, QStringLiteral("语文"), true);
    const qint64 shuxue = addCourse(school, QStringLiteral("数学"), true);
    const qint64 zhang = addTeacher(school, QStringLiteral("张"));
    const qint64 li = addTeacher(school, QStringLiteral("李"));
    const qint64 wang = addTeacher(school, QStringLiteral("王"));

    // 1班语文（张+李）4 节、2 次两节连堂，排成周一第 1-2 节、第 4-5 节
    plan(school, c1, yuwen, {zhang, li}, 4, 2, 2);
    placeSlots(school, c1, yuwen, {0, 1, 3, 4});
    // 2班语文（张）2 节：周一第 1 节与 1 班同节 → 1 处撞课
    plan(school, c2, yuwen, {zhang}, 2);
    placeSlots(school, c2, yuwen, {0, 7});
    // 1班数学（王）1 节，排在数学的不排时段（周一第 7 节）
    plan(school, c1, shuxue, {wang}, 1);
    placeSlots(school, c1, shuxue, {6});

    addForbidden(school, NoScheduleConstraint::EntityType::Course, shuxue, {6});
    addForbidden(school, NoScheduleConstraint::EntityType::Teacher, zhang, {0});
    addForbidden(school, NoScheduleConstraint::EntityType::Teacher, li, {0, 3});
    addForbidden(school, NoScheduleConstraint::EntityType::Teacher, wang, {6});

    const QualityMetricsResult result = computeQualityMetrics(school);

    // 1 排上的节数：4 + 2 + 1 = 7 / 计划 7
    QVERIFY(near(indicatorValue(result, QStringLiteral("placedPeriods")), 7.0));
    QVERIFY(near(indicatorRequired(result, QStringLiteral("placedPeriods")), 7.0));

    // 2 老师撞课：张周一第 1 节同时在 1 班与 2 班 → 1 处
    QVERIFY(near(indicatorValue(result, QStringLiteral("teacherClash")), 1.0));

    // 3 学科排进了不排的时段：数学周一第 7 节 → 1 节
    QVERIFY(near(indicatorValue(result, QStringLiteral("courseForbiddenSlot")), 1.0));

    // 4 老师排进了不排的时段：1班语文@周一第 1 节（张、李同时命中只计 1）、
    //   1班语文@周一第 4 节（李）、2班语文@周一第 1 节（张）、数学@周一第 7 节（王）→ 4 节
    QVERIFY(near(indicatorValue(result, QStringLiteral("teacherForbiddenSlot")), 4.0));

    // 5 该连堂的连上了：2 次连堂计划都连上 → 2 / 2
    QVERIFY(near(indicatorValue(result, QStringLiteral("chainedPlaced")), 2.0));
    QVERIFY(near(indicatorRequired(result, QStringLiteral("chainedPlaced")), 2.0));

    // 6 连堂落在第 1-2 或 4-5 节：两段都在好位置（起点第 1 节与第 4 节）→ 2 / 2
    QVERIFY(near(indicatorValue(result, QStringLiteral("chainGoodSlot")), 2.0));
    QVERIFY(near(indicatorRequired(result, QStringLiteral("chainGoodSlot")), 2.0));

    // 8 同一科一天上 3 节及以上：1班周一语文 4 节 → 1 次
    QVERIFY(near(indicatorValue(result, QStringLiteral("sameCoursePerDay")), 1.0));

    // 9 老师一天上 5 节及以上：张周一只有 {第1,2,4,5} 节（同节两班只算一节）→ 0
    QVERIFY(near(indicatorValue(result, QStringLiteral("teacherDailyLoad")), 0.0));
    // 10 老师连上 3 节及以上：张周一 [1,2] 与 [4,5] 两段都不足 3 节 → 0
    QVERIFY(near(indicatorValue(result, QStringLiteral("teacherRunLength")), 0.0));

    // 老师周节数：同节跨班只计一节（张 5 节、李 4 节、王 1 节）
    QCOMPARE(teacherQuality(result, zhang)->weeklyPeriods, 5);
    QCOMPARE(teacherQuality(result, li)->weeklyPeriods, 4);
    QCOMPARE(teacherQuality(result, wang)->weeklyPeriods, 1);
}

// 指标口径：单双周冲突规则（单周与双周不算撞课）
void TestMetrics::teacherClashParity()
{
    const auto clashWithHalves = [](const QVector<int> &halves) {
        School school;
        initTime(school);
        const qint64 shuxue = addCourse(school, QStringLiteral("数学"), true);
        const qint64 zhang = addTeacher(school, QStringLiteral("张"));
        for (int i = 0; i < halves.size(); ++i) {
            const qint64 c = addClass(school, QStringLiteral("%1班").arg(i + 1));
            plan(school, c, shuxue, {zhang}, 1);
            place(school, c, 0, shuxue, halves[i]);
        }
        return int(indicatorValue(computeQualityMetrics(school), QStringLiteral("teacherClash")));
    };

    QCOMPARE(clashWithHalves({1, 2}), 0);    // 单周 + 双周：不同周，不冲突
    QCOMPARE(clashWithHalves({1, 1}), 1);    // 同为单周 → 1 处
    QCOMPARE(clashWithHalves({0, 1}), 1);    // 整节 + 单周 → 1 处
    QCOMPARE(clashWithHalves({0, 0, 0}), 3); // 3 个整节 → C(3,2) = 3 处

    // 合班豁免：约定合班的两个班同节同课不算撞课
    const auto clashWithCombined = [](bool combined) {
        School school;
        initTime(school);
        const qint64 tiyu = addCourse(school, QStringLiteral("体育"), false);
        const qint64 he = addTeacher(school, QStringLiteral("何"));
        const qint64 c1 = addClass(school, QStringLiteral("1班"));
        const qint64 c2 = addClass(school, QStringLiteral("2班"));
        plan(school, c1, tiyu, {he}, 1);
        plan(school, c2, tiyu, {he}, 1);
        place(school, c1, 0, tiyu);
        place(school, c2, 0, tiyu);
        if (combined) {
            CombinedGroup cg;
            cg.name = QStringLiteral("体育合班 1-2");
            cg.entries = {{c1, tiyu}, {c2, tiyu}};
            school.addCombinedGroup(cg);
        }
        return int(indicatorValue(computeQualityMetrics(school), QStringLiteral("teacherClash")));
    };
    QCOMPARE(clashWithCombined(true), 0);  // 合班组内同节 → 豁免
    QCOMPARE(clashWithCombined(false), 1); // 未登记合班 → 仍算 1 处撞课
}

// 软指标：主课上午率 / 一天同科 3 节 / 老师一天 5 节 / 连上 3 节（含午休分隔与体育排除）
void TestMetrics::softIndicators()
{
    School school;
    initTime(school);
    const qint64 c1 = addClass(school, QStringLiteral("1班"));
    const qint64 c2 = addClass(school, QStringLiteral("2班"));
    const qint64 yuwen = addCourse(school, QStringLiteral("语文"), true);
    const qint64 shuxue = addCourse(school, QStringLiteral("数学"), true);
    const qint64 tiyu = addCourse(school, QStringLiteral("体育"), false);
    const qint64 zhang = addTeacher(school, QStringLiteral("张"));
    const qint64 wang = addTeacher(school, QStringLiteral("王"));
    const qint64 chen = addTeacher(school, QStringLiteral("陈"));

    plan(school, c1, yuwen, {zhang}, 3);
    placeSlots(school, c1, yuwen, {0, 1, 2}); // 周一上午连上 3 节
    plan(school, c1, shuxue, {wang}, 3);
    placeSlots(school, c1, shuxue, {3, 4, 5}); // 周一第 4、5 节 + 第 6 节（午休隔开）
    plan(school, c2, tiyu, {chen}, 5);
    placeSlots(school, c2, tiyu, {0, 1, 2, 3, 4}); // 周一 5 节体育

    const QualityMetricsResult result = computeQualityMetrics(school);

    // 7 语数英排在上午：主课 6 节，其中 5 节在上午（数学第 6 节在下午）
    QVERIFY(near(indicatorValue(result, QStringLiteral("majorMorning")), 5.0));
    QVERIFY(near(indicatorRequired(result, QStringLiteral("majorMorning")), 6.0));

    // 8 同一科一天上 3 节及以上：1班周一语文（4 节）、1班周一数学（3 节）、2班周一体育（5 节）
    QVERIFY(near(indicatorValue(result, QStringLiteral("sameCoursePerDay")), 3.0));

    // 9 老师一天上 5 节及以上：陈周一 5 节 → 1 人·天
    QVERIFY(near(indicatorValue(result, QStringLiteral("teacherDailyLoad")), 1.0));

    // 10 老师连上 3 节及以上：张 [1-3]、陈 [1-5] 各 1 次；王的 3 节被午休隔开 → 2 次
    QVERIFY2(near(indicatorValue(result, QStringLiteral("teacherRunLength")), 2.0),
             qPrintable(num(indicatorValue(result, QStringLiteral("teacherRunLength")))));

    // 体育：计入周节数，但不参与开窗/周内均衡
    const TeacherQuality *chenQ = teacherQuality(result, chen);
    QVERIFY(chenQ);
    QCOMPARE(chenQ->weeklyPeriods, 5);
    QVERIFY(near(chenQ->window, 0.0));
    QVERIFY(near(chenQ->balance, 0.0));
}

// 范围过滤：classFilter 为空=全校，否则只统计指定班级（按年级）
void TestMetrics::classFilterScope()
{
    School school;
    initTime(school);
    const qint64 c1 = addClass(school, QStringLiteral("1班"));
    const qint64 c2 = addClass(school, QStringLiteral("2班"));
    const qint64 c3 = addClass(school, QStringLiteral("3班"));
    const qint64 yuwen = addCourse(school, QStringLiteral("语文"), true);
    const qint64 zhang = addTeacher(school, QStringLiteral("张"));
    const qint64 wang = addTeacher(school, QStringLiteral("王"));

    Group gradeA;
    gradeA.type = Group::Type::Grade;
    gradeA.name = QStringLiteral("一年级");
    gradeA.memberIds = {c1, c2};
    const qint64 gradeAId = school.addGroup(gradeA);
    Group gradeB;
    gradeB.type = Group::Type::Grade;
    gradeB.name = QStringLiteral("二年级");
    gradeB.memberIds = {c3};
    const qint64 gradeBId = school.addGroup(gradeB);
    school.findClass(c1)->gradeId = gradeAId;
    school.findClass(c2)->gradeId = gradeAId;
    school.findClass(c3)->gradeId = gradeBId;

    plan(school, c1, yuwen, {zhang}, 2);
    placeSlots(school, c1, yuwen, {0, 1});
    plan(school, c2, yuwen, {zhang}, 2);
    placeSlots(school, c2, yuwen, {7, 8});
    plan(school, c3, yuwen, {wang}, 3);
    placeSlots(school, c3, yuwen, {14, 15, 16}); // 14 = 周三第 1 节
    addForbidden(school, NoScheduleConstraint::EntityType::Course, yuwen, {14});

    // 全校
    const QualityMetricsResult all = computeQualityMetrics(school);
    QVERIFY(near(indicatorValue(all, QStringLiteral("placedPeriods")), 7.0));
    QVERIFY(near(indicatorRequired(all, QStringLiteral("placedPeriods")), 7.0));
    QVERIFY(near(indicatorValue(all, QStringLiteral("courseForbiddenSlot")), 1.0));
    QCOMPARE(all.teachers.size(), 2);

    // 只统计一年级（1、2 班）
    const QualityMetricsResult one = computeQualityMetrics(school, school.findGroup(gradeAId)->memberIds);
    QVERIFY(near(indicatorValue(one, QStringLiteral("placedPeriods")), 4.0));
    QVERIFY(near(indicatorRequired(one, QStringLiteral("placedPeriods")), 4.0));
    QVERIFY(near(indicatorValue(one, QStringLiteral("courseForbiddenSlot")), 0.0));
    QCOMPARE(one.teachers.size(), 1);
    QCOMPARE(one.teachers.first().teacherId, zhang);
    QCOMPARE(one.teachers.first().weeklyPeriods, 4);

    // 只统计二年级（3 班）
    const QualityMetricsResult two = computeQualityMetrics(school, {c3});
    QVERIFY(near(indicatorValue(two, QStringLiteral("placedPeriods")), 3.0));
    QVERIFY(near(indicatorRequired(two, QStringLiteral("placedPeriods")), 3.0));
    QVERIFY(near(indicatorValue(two, QStringLiteral("courseForbiddenSlot")), 1.0));
    QCOMPARE(two.teachers.size(), 1);
    QCOMPARE(two.teachers.first().teacherId, wang);

    // 范围内无任何班级 → 空结果（不崩溃）
    const QualityMetricsResult none = computeQualityMetrics(school, {9999});
    QVERIFY(near(indicatorValue(none, QStringLiteral("placedPeriods")), 0.0));
    QVERIFY(near(indicatorRequired(none, QStringLiteral("placedPeriods")), 0.0));
    QCOMPARE(none.teachers.size(), 0);
}

// 老师排序（按周节数降序）与五项平均
void TestMetrics::teacherRanking()
{
    School school;
    initTime(school);
    const qint64 c1 = addClass(school, QStringLiteral("1班"));
    const qint64 c2 = addClass(school, QStringLiteral("2班"));
    const qint64 c3 = addClass(school, QStringLiteral("3班"));
    const qint64 yuwen = addCourse(school, QStringLiteral("语文"), true);
    const qint64 shuxue = addCourse(school, QStringLiteral("数学"), true);
    const qint64 yingyu = addCourse(school, QStringLiteral("英语"), false);
    const qint64 zhang = addTeacher(school, QStringLiteral("张"));
    const qint64 wang = addTeacher(school, QStringLiteral("王"));
    const qint64 li = addTeacher(school, QStringLiteral("李"));

    plan(school, c1, yuwen, {zhang}, 6);
    placeSlots(school, c1, yuwen, {0, 1, 2, 3, 4, 5});
    plan(school, c2, shuxue, {wang}, 4);
    placeSlots(school, c2, shuxue, {0, 1, 2, 3});
    plan(school, c3, yingyu, {li}, 2);
    placeSlots(school, c3, yingyu, {0, 1});

    const QualityMetricsResult result = computeQualityMetrics(school);
    QCOMPARE(result.teachers.size(), 3);
    QCOMPARE(result.teachers[0].teacherId, zhang);
    QCOMPARE(result.teachers[0].weeklyPeriods, 6);
    QCOMPARE(result.teachers[1].teacherId, wang);
    QCOMPARE(result.teachers[1].weeklyPeriods, 4);
    QCOMPARE(result.teachers[2].teacherId, li);
    QCOMPARE(result.teachers[2].weeklyPeriods, 2);

    double continuity = 0;
    double window = 0;
    double balance = 0;
    double fairness = 0;
    double composite = 0;
    for (const TeacherQuality &tq : result.teachers) {
        QVERIFY(tq.weeklyPeriods >= 0);
        QVERIFY(near(tq.composite, tq.window + tq.balance + tq.fairness));
        continuity += tq.continuity;
        window += tq.window;
        balance += tq.balance;
        fairness += tq.fairness;
        composite += tq.composite;
    }
    const double n = result.teachers.size();
    QVERIFY(near(result.avgContinuity, continuity / n));
    QVERIFY(near(result.avgWindow, window / n));
    QVERIFY(near(result.avgBalance, balance / n));
    QVERIFY(near(result.avgFairness, fairness / n));
    QVERIFY(near(result.avgComposite, composite / n));
}

QTEST_GUILESS_MAIN(TestMetrics)

#include "tst_metrics.moc"
