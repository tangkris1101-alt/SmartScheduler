#include "EngineCore.h"
#include "SchedulerEngine.h"
#include "model/School.h"

#include <QSignalSpy>
#include <QTimer>
#include <QtTest/QTest>

using namespace sched;

// 排课引擎单元测试
class TestEngine : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void solveSampleSchool();
    void reproducible();
    void cancelStops();
    void progressGroupScoring();
    void randomSeedRuns();
    void unplacedDetails();
    void preassignHalfSharesSlot();
    void planTimePreferenceScoring();
    void timeSyncGroupSameSlot();
};

// 测试校例的实体 id 记录
struct SampleIds {
    qint64 class1, class2, class3;
    qint64 yuwen, shuxue, yingyu, tiyu;
    qint64 tZhang, tWang, tLiu, tChen, tZhao;
    qint64 room;
};

// 3 班，含主课/普通课、合班、连课、单双周、不排时间、预排、教室容量
static School makeEngineSchool(SampleIds *ids)
{
    School school;
    school.name = QStringLiteral("引擎测试校");
    school.timeConfig.daysPerWeek = 5;
    school.timeConfig.periodsPerDay = 7;
    school.timeConfig.morningPeriods = 4;
    school.timeConfig.weeksPerTerm = 20;

    ClassInfo c1; c1.name = QStringLiteral("1班");
    ClassInfo c2; c2.name = QStringLiteral("2班");
    ClassInfo c3; c3.name = QStringLiteral("3班");
    ids->class1 = school.addClass(c1);
    ids->class2 = school.addClass(c2);
    ids->class3 = school.addClass(c3);

    Course yuwen; yuwen.name = QStringLiteral("语文"); yuwen.isMajor = true;
    Course shuxue; shuxue.name = QStringLiteral("数学"); shuxue.isMajor = true; shuxue.evenSpread = true;
    Course yingyu; yingyu.name = QStringLiteral("英语");
    Course tiyu; tiyu.name = QStringLiteral("体育");
    ids->yuwen = school.addCourse(yuwen);
    ids->shuxue = school.addCourse(shuxue);
    ids->yingyu = school.addCourse(yingyu);
    ids->tiyu = school.addCourse(tiyu);

    Teacher tz; tz.name = QStringLiteral("张"); tz.maxPeriodsPerDay = 5;
    Teacher tw; tw.name = QStringLiteral("王");
    Teacher tl; tl.name = QStringLiteral("刘");
    Teacher tc; tc.name = QStringLiteral("陈");
    Teacher tzh; tzh.name = QStringLiteral("赵");
    ids->tZhang = school.addTeacher(tz);
    ids->tWang = school.addTeacher(tw);
    ids->tLiu = school.addTeacher(tl);
    ids->tChen = school.addTeacher(tc);
    ids->tZhao = school.addTeacher(tzh);

    Classroom room; room.name = QStringLiteral("多媒体教室"); room.capacity = 1;
    ids->room = school.addClassroom(room);

    auto plan = [&](qint64 cls, qint64 course, QVector<qint64> teachers, double periods,
                    int chained = 0, int chainLen = 2, bool combined = false, qint64 roomId = -1) {
        PlanEntry e;
        e.classId = cls;
        e.courseId = course;
        e.teacherIds = teachers;
        e.weeklyPeriods = periods;
        e.chainedCount = chained;
        e.chainLength = chainLen;
        e.isCombined = combined;
        e.classroomId = roomId;
        school.addPlanEntry(e);
    };

    // 语文：1/2 班各 5 节含 1 次两节连课，3 班 4 节 —— 张老师一人任教（进度一致看点）
    plan(ids->class1, ids->yuwen, {ids->tZhang}, 5, 1, 2);
    plan(ids->class2, ids->yuwen, {ids->tZhang}, 5, 1, 2);
    plan(ids->class3, ids->yuwen, {ids->tZhang}, 4);
    // 数学：王教 1/2 班，刘教 3 班
    plan(ids->class1, ids->shuxue, {ids->tWang}, 5);
    plan(ids->class2, ids->shuxue, {ids->tWang}, 5);
    plan(ids->class3, ids->shuxue, {ids->tLiu}, 5);
    // 体育：1/2 班合班，陈老师，用多媒体教室
    plan(ids->class1, ids->tiyu, {ids->tChen}, 2, 0, 2, true, ids->room);
    plan(ids->class2, ids->tiyu, {ids->tChen}, 2, 0, 2, true, ids->room);
    // 英语：3 班 2.5 节（含单双周），赵老师
    plan(ids->class3, ids->yingyu, {ids->tZhao}, 2.5);

    // 合班组
    CombinedGroup cg;
    cg.name = QStringLiteral("体育合班");
    cg.entries = {{ids->class1, ids->tiyu}, {ids->class2, ids->tiyu}};
    school.addCombinedGroup(cg);

    // 不排时间：张老师 周一第1节(slot 0)；1班 周五第7节(slot 34)
    NoScheduleConstraint ns1;
    ns1.entityType = NoScheduleConstraint::EntityType::Teacher;
    ns1.entityId = ids->tZhang;
    ns1.slots = {0};
    school.addNoScheduleConstraint(ns1);
    NoScheduleConstraint ns2;
    ns2.entityType = NoScheduleConstraint::EntityType::Class;
    ns2.entityId = ids->class1;
    ns2.slots = {34};
    school.addNoScheduleConstraint(ns2);

    // 固定预排：3班数学 周二第4节(slot 10)
    PreAssignment pre;
    pre.classId = ids->class3;
    pre.courseId = ids->shuxue;
    pre.slotIndex = 10;
    school.addPreAssignment(pre);

    // 老师活动：赵老师 周三第6节(slot 19) 教研活动
    TeacherActivity act;
    act.teacherId = ids->tZhao;
    act.slotIndex = 19;
    act.activityCourseId = ids->yuwen;
    school.addTeacherActivity(act);

    return school;
}

// 收集某班某课程的所有 slot
static QSet<int> slotsOf(const Schedule &s, qint64 classId, qint64 courseId)
{
    QSet<int> out;
    for (const auto &a : s.assignments) {
        if (a.classId == classId && a.courseId == courseId)
            out.insert(a.slotIndex);
    }
    return out;
}

// 可复现比较用：把课表序列化为有序字符串表
static QStringList serialize(const Schedule &s)
{
    QStringList out;
    for (const auto &a : s.assignments) {
        out << QStringLiteral("%1@%2h%3=%4")
                   .arg(a.classId)
                   .arg(a.slotIndex)
                   .arg(a.half)
                   .arg(a.courseId);
    }
    out.sort();
    return out;
}

// 统计老师在某 slot 的教学单元数（合班组内多班同课合并为一个单元；老师活动也算占用）
static int teacherUnits(const Schedule &sch, const School &school, qint64 teacherId, int slot)
{
    QSet<QString> units;
    for (const PlanEntry &e : school.planEntries) {
        if (!e.teacherIds.contains(teacherId))
            continue;
        for (const auto &a : sch.assignments) {
            if (a.classId != e.classId || a.courseId != e.courseId || a.slotIndex != slot)
                continue;
            QString key;
            for (int gi = 0; gi < school.combinedGroups.size(); ++gi) {
                if (school.combinedGroups[gi].entries.contains({e.classId, e.courseId})) {
                    key = QStringLiteral("cg%1").arg(gi); // 合班归并
                    break;
                }
            }
            if (key.isEmpty())
                key = QStringLiteral("%1/%2").arg(e.classId).arg(e.courseId);
            units.insert(key);
        }
    }
    int count = units.size();
    for (const TeacherActivity &ta : school.teacherActivities) {
        if (ta.teacherId == teacherId && ta.slotIndex == slot)
            count++;
    }
    return count;
}

static void verifySolution(const Schedule &sch, const QualityReport &report, const SampleIds &ids,
                           const School &school)
{
    // 全部排出且无硬冲突
    QCOMPARE(report.hardViolations, 0);
    QCOMPARE(report.placedSessions, report.totalSessions);
    QVERIFY(report.totalSessions > 0);

    // 预排未被移动：3班数学 必须在 slot 10
    const auto pre = sch.courseAt(ids.class3, 10, 0);
    QVERIFY(pre.has_value());
    QCOMPARE(pre.value(), ids.shuxue);

    // 合班同 slot：1班体育 slot 集合 == 2班体育 slot 集合，且非空
    const QSet<int> ty1 = slotsOf(sch, ids.class1, ids.tiyu);
    const QSet<int> ty2 = slotsOf(sch, ids.class2, ids.tiyu);
    QCOMPARE(ty1.size(), 2);
    QCOMPARE(ty1, ty2);

    // 连课连续：1班语文存在同一天相邻两节
    const QSet<int> yw1 = slotsOf(sch, ids.class1, ids.yuwen);
    bool hasChain = false;
    for (int sl : yw1) {
        if (sl % 7 != 6 && yw1.contains(sl + 1)) { // 同一天内相邻
            hasChain = true;
            break;
        }
    }
    QVERIFY(hasChain);
    QCOMPARE(yw1.size(), 5); // 周 5 节

    // 不排时间被尊重
    QVERIFY(!slotsOf(sch, ids.class1, ids.yuwen).contains(0)
            && !slotsOf(sch, ids.class2, ids.yuwen).contains(0)
            && !slotsOf(sch, ids.class3, ids.yuwen).contains(0)); // 张老师 slot 0
    // 1班 slot 34 无任何课
    for (const auto &a : sch.assignments)
        QVERIFY(!(a.classId == ids.class1 && a.slotIndex == 34));

    // 单双周：3班英语存在 half!=0 的条目
    bool hasHalf = false;
    for (const auto &a : sch.assignments) {
        if (a.classId == ids.class3 && a.courseId == ids.yingyu && a.half != 0)
            hasHalf = true;
    }
    QVERIFY(hasHalf);

    // 同老师同 slot 无重课（含老师活动时间）
    const int sc = school.timeConfig.slotCount();
    const QVector<qint64> teachers = {ids.tZhang, ids.tWang, ids.tLiu, ids.tChen, ids.tZhao};
    for (qint64 t : teachers) {
        for (int sl = 0; sl < sc; ++sl)
            QVERIFY2(teacherUnits(sch, school, t, sl) <= 1,
                     qPrintable(QStringLiteral("老师 %1 在 slot %2 重课").arg(t).arg(sl)));
    }

    // 软约束分项完整
    QVERIFY(report.softBreakdown.contains(QStringLiteral("progressAlign")));
    QVERIFY(report.softBreakdown.contains(QStringLiteral("evenSpread")));
}

void TestEngine::solveSampleSchool()
{
    SampleIds ids;
    const School school = makeEngineSchool(&ids);

    EngineOptions opt;
    opt.runs = 4;
    opt.maxIterationsPerRun = 20000;
    opt.seed = 42;
    SchedulerEngine engine(school, opt);

    QSignalSpy spyFinished(&engine, &SchedulerEngine::finished);
    QSignalSpy spyProgress(&engine, &SchedulerEngine::progress);
    engine.start();
    QVERIFY2(spyFinished.wait(30000), "引擎未在 30s 内完成");
    QVERIFY(spyProgress.count() > 0);

    const Schedule best = spyFinished.at(0).at(0).value<Schedule>();
    const QualityReport report = spyFinished.at(0).at(1).value<QualityReport>();
    verifySolution(best, report, ids, school);

    qInfo() << "质量报告: hardViolations=" << report.hardViolations
            << "softScore=" << report.softScore << "breakdown=" << report.softBreakdown;
}

void TestEngine::reproducible()
{
    SampleIds ids;
    const School school = makeEngineSchool(&ids);

    EngineOptions opt;
    opt.runs = 3;
    opt.maxIterationsPerRun = 5000;
    opt.seed = 1234;

    QStringList first, second;
    for (int round = 0; round < 2; ++round) {
        SchedulerEngine engine(school, opt);
        QSignalSpy spy(&engine, &SchedulerEngine::finished);
        engine.start();
        QVERIFY2(spy.wait(30000), "引擎未完成");
        const Schedule best = spy.at(0).at(0).value<Schedule>();
        (round == 0 ? first : second) = serialize(best);
    }
    QCOMPARE(first, second); // 相同 seed 两次运行结果一致
    QVERIFY(!first.isEmpty());
}

void TestEngine::cancelStops()
{
    SampleIds ids;
    const School school = makeEngineSchool(&ids);

    EngineOptions opt;
    opt.runs = 2;
    opt.maxIterationsPerRun = 200000000; // 足够大，保证跑不完
    opt.seed = 7;
    SchedulerEngine engine(school, opt);

    QSignalSpy spyFinished(&engine, &SchedulerEngine::finished);
    QSignalSpy spyProgress(&engine, &SchedulerEngine::progress);
    engine.start();
    QVERIFY(engine.isRunning());
    QTimer::singleShot(300, &engine, [&engine] { engine.cancel(); });
    QVERIFY2(spyFinished.wait(15000), "取消后未收到 finished");

    // finished 之后不再有进度推进
    const int progressCount = spyProgress.count();
    QTest::qWait(400);
    QCOMPARE(spyProgress.count(), progressCount);
    QVERIFY(!engine.isRunning());
}

// progressGroup 进入进度一致软约束（白盒：直接操作 Problem）
void TestEngine::progressGroupScoring()
{
    // 张老师教 1/2 班语文各 2 节
    School school;
    school.timeConfig.daysPerWeek = 5;
    school.timeConfig.periodsPerDay = 7;
    school.timeConfig.morningPeriods = 4;
    ClassInfo c1; c1.name = QStringLiteral("1班");
    ClassInfo c2; c2.name = QStringLiteral("2班");
    const qint64 class1 = school.addClass(c1);
    const qint64 class2 = school.addClass(c2);
    Course yw; yw.name = QStringLiteral("语文");
    const qint64 yuwen = school.addCourse(yw);
    Teacher tz; tz.name = QStringLiteral("张");
    const qint64 teacher = school.addTeacher(tz);

    PlanEntry e1; e1.classId = class1; e1.courseId = yuwen; e1.teacherIds = {teacher};
    e1.weeklyPeriods = 1; e1.progressGroup = -1;
    PlanEntry e2; e2.classId = class2; e2.courseId = yuwen; e2.teacherIds = {teacher};
    e2.weeklyPeriods = 1; e2.progressGroup = -1;
    school.addPlanEntry(e1);
    const qint64 entry2 = school.addPlanEntry(e2);

    // 同组（默认 -1 自动归同组）：两班语文相邻排比隔开罚分低
    {
        engine::Problem p(school);
        p.apply(0, 0, 0);  // 1班语文 slot0
        p.apply(1, 1, 0);  // 2班语文 slot1（相邻）
        QMap<QString, double> bdAdjacent;
        p.softScore(&bdAdjacent);

        engine::Problem p2(school);
        p2.apply(0, 0, 0);
        p2.apply(1, 7, 0); // 2班语文 slot7（不相邻）
        QMap<QString, double> bdApart;
        p2.softScore(&bdApart);

        QVERIFY(bdAdjacent.value(QStringLiteral("progressAlign"))
                < bdApart.value(QStringLiteral("progressAlign")));
        QCOMPARE(bdAdjacent.value(QStringLiteral("progressAlign")), 0.0);
    }

    // 不同进度组：不再要求相邻，相邻与隔开罚分相同（各自组内只有 1 条会话）
    school.findPlanEntry(entry2)->progressGroup = 2;
    {
        engine::Problem p(school);
        p.apply(0, 0, 0);
        p.apply(1, 1, 0);
        QMap<QString, double> bdAdjacent;
        p.softScore(&bdAdjacent);

        engine::Problem p2(school);
        p2.apply(0, 0, 0);
        p2.apply(1, 7, 0);
        QMap<QString, double> bdApart;
        p2.softScore(&bdApart);

        QCOMPARE(bdAdjacent.value(QStringLiteral("progressAlign")),
                 bdApart.value(QStringLiteral("progressAlign")));
        QCOMPARE(bdAdjacent.value(QStringLiteral("progressAlign")), 0.0);
    }
}

// randomSeed=true：正常完成且硬违例为 0（不比较结果，避免随机性导致测试不稳定）
void TestEngine::randomSeedRuns()
{
    SampleIds ids;
    const School school = makeEngineSchool(&ids);

    EngineOptions opt;
    opt.runs = 2;
    opt.maxIterationsPerRun = 3000;
    opt.randomSeed = true;
    SchedulerEngine engine(school, opt);
    QSignalSpy spy(&engine, &SchedulerEngine::finished);
    engine.start();
    QVERIFY2(spy.wait(30000), "randomSeed 运行未完成");
    const QualityReport report = spy.at(0).at(1).value<QualityReport>();
    QCOMPARE(report.hardViolations, 0);
}

// 未排明细：预排占满 1班全部节位 → 语文无处可排，报告应含"1班·语文"
void TestEngine::unplacedDetails()
{
    School school;
    school.timeConfig.daysPerWeek = 2;
    school.timeConfig.periodsPerDay = 3;
    school.timeConfig.morningPeriods = 2;
    ClassInfo c1; c1.name = QStringLiteral("1班");
    const qint64 class1 = school.addClass(c1);
    Course yw; yw.name = QStringLiteral("语文");
    Course sx; sx.name = QStringLiteral("数学");
    const qint64 yuwen = school.addCourse(yw);
    const qint64 shuxue = school.addCourse(sx);

    PlanEntry e; e.classId = class1; e.courseId = yuwen; e.weeklyPeriods = 2;
    school.addPlanEntry(e);
    // 预排数学占满全部 6 个节位
    for (int slot = 0; slot < 6; ++slot) {
        PreAssignment p; p.classId = class1; p.courseId = shuxue; p.slotIndex = slot;
        school.addPreAssignment(p);
    }

    EngineOptions opt;
    opt.runs = 1;
    opt.maxIterationsPerRun = 100;
    opt.seed = 5;
    opt.placeAll = false; // 无处可排 → 记为未排
    SchedulerEngine engine(school, opt);
    QSignalSpy spy(&engine, &SchedulerEngine::finished);
    engine.start();
    QVERIFY2(spy.wait(15000), "引擎未完成");
    const QualityReport report = spy.at(0).at(1).value<QualityReport>();
    QVERIFY(report.placedSessions < report.totalSessions);
    QVERIFY(report.hardViolations > 0);
    QVERIFY(!report.unplaced.isEmpty());
    QVERIFY(report.unplaced.first().contains(QStringLiteral("1班·语文")));
}

QTEST_GUILESS_MAIN(TestEngine)

#include "tst_engine.moc"

// 预排的单双周：两门课各占半周 → 共用同一格；排课结果里两门课都落在该 slot
void TestEngine::preassignHalfSharesSlot()
{
    School school;
    school.timeConfig.daysPerWeek = 2;
    school.timeConfig.periodsPerDay = 3;
    school.timeConfig.morningPeriods = 2;
    ClassInfo c1; c1.name = QStringLiteral("1班");
    const qint64 class1 = school.addClass(c1);
    Course a; a.name = QStringLiteral("综合实践");
    Course b; b.name = QStringLiteral("心理");
    const qint64 courseA = school.addCourse(a);
    const qint64 courseB = school.addCourse(b);
    for (qint64 co : {courseA, courseB}) {
        PlanEntry e; e.classId = class1; e.courseId = co; e.weeklyPeriods = 0.5;
        school.addPlanEntry(e);
    }
    // 同一格 slot 1：A 占单周、B 占双周
    PreAssignment pa; pa.classId = class1; pa.courseId = courseA; pa.slotIndex = 1; pa.half = 1;
    PreAssignment pb; pb.classId = class1; pb.courseId = courseB; pb.slotIndex = 1; pb.half = 2;
    school.addPreAssignment(pa);
    school.addPreAssignment(pb);

    EngineOptions opt;
    opt.runs = 1;
    opt.maxIterationsPerRun = 200;
    opt.seed = 7;
    School copy = school;
    SchedulerEngine engine(copy, opt);
    QSignalSpy spy(&engine, &SchedulerEngine::finished);
    engine.start();
    QVERIFY2(spy.wait(15000), "引擎未完成");
    const QualityReport report = spy.at(0).at(1).value<QualityReport>();
    QCOMPARE(report.hardViolations, 0);
    const Schedule sch = spy.at(0).at(0).value<Schedule>();
    QCOMPARE(sch.courseAt(class1, 1, 1).value_or(-1), courseA); // 单周 = A
    QCOMPARE(sch.courseAt(class1, 1, 2).value_or(-1), courseB); // 双周 = B
}

// 同时段组（走班）：不同课程的两个班必须排在同一节
void TestEngine::timeSyncGroupSameSlot()
{
    School school;
    school.timeConfig.daysPerWeek = 2;
    school.timeConfig.periodsPerDay = 3;
    school.timeConfig.morningPeriods = 2;
    ClassInfo c1; c1.name = QStringLiteral("16班");
    ClassInfo c2; c2.name = QStringLiteral("14班");
    const qint64 class1 = school.addClass(c1);
    const qint64 class2 = school.addClass(c2);
    Course sw; sw.name = QStringLiteral("生物");
    Course zz; zz.name = QStringLiteral("政治");
    const qint64 shengwu = school.addCourse(sw);
    const qint64 zhengzhi = school.addCourse(zz);
    PlanEntry e1; e1.classId = class1; e1.courseId = shengwu; e1.weeklyPeriods = 1;
    PlanEntry e2; e2.classId = class2; e2.courseId = zhengzhi; e2.weeklyPeriods = 1;
    school.addPlanEntry(e1);
    school.addPlanEntry(e2);

    TimeSyncGroup g;
    g.name = QStringLiteral("高三走班");
    g.entries = {{class1, shengwu}, {class2, zhengzhi}};
    school.addTimeSyncGroup(g);

    EngineOptions opt;
    opt.runs = 1;
    opt.maxIterationsPerRun = 200;
    opt.seed = 11;
    School copy = school;
    SchedulerEngine engine(copy, opt);
    QSignalSpy spy(&engine, &SchedulerEngine::finished);
    engine.start();
    QVERIFY2(spy.wait(15000), "引擎未完成");
    const QualityReport report = spy.at(0).at(1).value<QualityReport>();
    QCOMPARE(report.hardViolations, 0);
    const Schedule sch = spy.at(0).at(0).value<Schedule>();
    int slot1 = -1, slot2 = -1;
    for (const auto &a : sch.assignments) {
        if (a.classId == class1)
            slot1 = a.slotIndex;
        if (a.classId == class2)
            slot2 = a.slotIndex;
    }
    QVERIFY(slot1 >= 0 && slot2 >= 0);
    QCOMPARE(slot1, slot2); // 走班：同一节
}

// 计划条目的时段要求（软）：morningPeriods 与 earlyPeriods 都应出现在软分项里
void TestEngine::planTimePreferenceScoring()
{
    School school;
    school.timeConfig.daysPerWeek = 1;
    school.timeConfig.periodsPerDay = 4;
    school.timeConfig.morningPeriods = 2;
    ClassInfo c1; c1.name = QStringLiteral("1班");
    const qint64 class1 = school.addClass(c1);
    Course yw; yw.name = QStringLiteral("语文");
    const qint64 yuwen = school.addCourse(yw);
    PlanEntry e; e.classId = class1; e.courseId = yuwen; e.weeklyPeriods = 2;
    e.earlyPeriods = 2; // 尽量排前 2 节
    school.addPlanEntry(e);

    PreAssignment p; p.classId = class1; p.courseId = yuwen; p.slotIndex = 3; // 钉在最后两节之一
    school.addPreAssignment(p);

    EngineOptions opt;
    opt.runs = 1;
    opt.maxIterationsPerRun = 50;
    opt.seed = 3;
    School copy = school;
    SchedulerEngine engine(copy, opt);
    QSignalSpy spy(&engine, &SchedulerEngine::finished);
    engine.start();
    QVERIFY2(spy.wait(15000), "引擎未完成");
    const QualityReport report = spy.at(0).at(1).value<QualityReport>();
    // 预排在第 4 节（0 基 3）超出「前 2 节」→ 该项必须有罚分
    QVERIFY(report.softBreakdown.value(QStringLiteral("planTimePreference")) > 0.0);
}
