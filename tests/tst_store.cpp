#include "ProjectStore.h"
#include "model/School.h"

#include <QTemporaryDir>
#include <QtTest/QTest>

using namespace sched;

// 存储层单元测试：School -> SQLite 文件 -> School 的 round-trip 一致性
class TestStore : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void roundTrip();
    void openNonExistent();
    void overwriteSave();
};

// 造一所 2 班 / 3 课 / 2 师 / 1 教室 / 1 合班组 + 约束与预排的学校
static School makeSampleSchool()
{
    School school;
    school.name = QStringLiteral("示例中学");
    school.timeConfig.daysPerWeek = 5;
    school.timeConfig.periodsPerDay = 7;
    school.timeConfig.morningPeriods = 4;
    school.timeConfig.weeksPerTerm = 20;

    Group grade;
    grade.type = Group::Type::Grade;
    grade.name = QStringLiteral("高一年级");
    const qint64 gradeId = school.addGroup(grade); // id=1

    ClassInfo c1;
    c1.name = QStringLiteral("1班");
    c1.longName = QStringLiteral("高一（1）班");
    c1.shortName = QStringLiteral("一1");
    c1.code = QStringLiteral("gy1b");
    c1.priority = 1;
    c1.remark = QStringLiteral("重点班");
    c1.gradeId = gradeId;
    const qint64 class1 = school.addClass(c1);

    ClassInfo c2;
    c2.name = QStringLiteral("2班");
    c2.gradeId = gradeId;
    const qint64 class2 = school.addClass(c2);
    school.findGroup(gradeId)->memberIds = {class1, class2};

    Group jiaoyanzu;
    jiaoyanzu.type = Group::Type::CourseGroup;
    jiaoyanzu.name = QStringLiteral("数学教研组");
    const qint64 courseGroupId = school.addGroup(jiaoyanzu);

    Course yuwen;
    yuwen.name = QStringLiteral("语文");
    yuwen.isMajor = true;
    yuwen.evenSpread = true;
    yuwen.amPmPolicy = AmPmPolicy::Morning;
    yuwen.code = QStringLiteral("yw");
    const qint64 courseYuwen = school.addCourse(yuwen);

    Course shuxue;
    shuxue.name = QStringLiteral("数学");
    shuxue.isMajor = true;
    shuxue.isScience = true;
    shuxue.groupId = courseGroupId;
    shuxue.progressAlignCount = 3;
    const qint64 courseShuxue = school.addCourse(shuxue);

    Course tiyu;
    tiyu.name = QStringLiteral("体育");
    tiyu.amPmPolicy = AmPmPolicy::Afternoon;
    const qint64 courseTiyu = school.addCourse(tiyu);
    school.findGroup(courseGroupId)->memberIds = {courseShuxue};

    Teacher t1;
    t1.name = QStringLiteral("张明");
    t1.code = QStringLiteral("zm");
    t1.maxPeriodsPerDay = 5;
    const qint64 teacher1 = school.addTeacher(t1);

    Teacher t2;
    t2.name = QStringLiteral("李华");
    t2.mutualExclusionGroup = 7; // 与某互斥组关联（如夫妻同校不同时）
    const qint64 teacher2 = school.addTeacher(t2);

    Classroom lab;
    lab.name = QStringLiteral("物理实验室");
    lab.capacity = 2;
    const qint64 roomId = school.addClassroom(lab);

    // 教学计划：1班语文（张老师，周5节，连课1次×2节）
    PlanEntry e1;
    e1.classId = class1;
    e1.courseId = courseYuwen;
    e1.teacherIds = {teacher1};
    e1.weeklyPeriods = 5;
    e1.chainedCount = 1;
    e1.chainLength = 2;
    e1.progressGroup = 1; // 进度一致组（M3b 新增字段，验证存取）
    school.addPlanEntry(e1);

    // 2班数学（双师），周4.5节（单双周）
    PlanEntry e2;
    e2.classId = class2;
    e2.courseId = courseShuxue;
    e2.teacherIds = {teacher1, teacher2};
    e2.weeklyPeriods = 4.5;
    e2.morningPeriods = 3;
    e2.startWeek = 2;
    e2.endWeek = 18;
    school.addPlanEntry(e2);

    // 合班体育：1班+2班 合上，用实验室
    PlanEntry e3;
    e3.classId = class1;
    e3.courseId = courseTiyu;
    e3.teacherIds = {teacher2};
    e3.weeklyPeriods = 2;
    e3.isCombined = true;
    e3.classroomId = roomId;
    e3.syncCourseId = -1;
    school.addPlanEntry(e3);

    CombinedGroup cg;
    cg.name = QStringLiteral("体育合班");
    cg.entries = {{class1, courseTiyu}, {class2, courseTiyu}};
    school.addCombinedGroup(cg);

    // 不排课：张老师 周一上午1-3节 不排
    NoScheduleConstraint ns1;
    ns1.entityType = NoScheduleConstraint::EntityType::Teacher;
    ns1.entityId = teacher1;
    ns1.slots = {0, 1, 2};
    school.addNoScheduleConstraint(ns1);
    // 不排课：2班 周五下午最后两节 不排
    NoScheduleConstraint ns2;
    ns2.entityType = NoScheduleConstraint::EntityType::Class;
    ns2.entityId = class2;
    ns2.slots = {33, 34};
    school.addNoScheduleConstraint(ns2);

    // 固定预排：1班 体育 固定在周三第5节
    PreAssignment p;
    p.classId = class1;
    p.courseId = courseTiyu;
    p.slotIndex = 2 * 7 + 4;
    school.addPreAssignment(p);

    // 老师活动：李老师 周二第6节 教研活动（记为数学组活动）
    TeacherActivity act;
    act.teacherId = teacher2;
    act.slotIndex = 1 * 7 + 5;
    act.activityCourseId = courseShuxue;
    school.addTeacherActivity(act);

    // 手工排课结果若干
    school.schedule.setAssignment(class1, 0, 0, courseYuwen);
    school.schedule.setAssignment(class1, 8, 0, courseYuwen); // 连课
    school.schedule.setAssignment(class1, 9, 0, courseYuwen);
    school.schedule.setAssignment(class2, 1, 1, courseShuxue); // 单周
    school.schedule.setAssignment(class2, 1, 2, courseShuxue); // 双周
    school.schedule.setAssignment(class2, 0, 0, courseTiyu);

    return school;
}

void TestStore::roundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("roundtrip.spk"));

    const School original = makeSampleSchool();

    {
        ProjectStore store;
        QVERIFY2(store.createNew(path), qPrintable(store.lastError()));
        QVERIFY2(store.save(original), qPrintable(store.lastError()));
        store.close();
    }

    School loaded;
    {
        ProjectStore store;
        QVERIFY2(store.open(path), qPrintable(store.lastError()));
        QVERIFY2(store.load(&loaded), qPrintable(store.lastError()));
    }

    // 逐字段比对
    QCOMPARE(loaded.name, original.name);
    QCOMPARE(loaded.timeConfig, original.timeConfig);
    QCOMPARE(loaded.classes, original.classes);
    QCOMPARE(loaded.courses, original.courses);
    QCOMPARE(loaded.teachers, original.teachers);
    QCOMPARE(loaded.classrooms, original.classrooms);
    QCOMPARE(loaded.groups, original.groups);
    QCOMPARE(loaded.planEntries, original.planEntries);
    QCOMPARE(loaded.combinedGroups, original.combinedGroups);
    QCOMPARE(loaded.noScheduleConstraints, original.noScheduleConstraints);
    QCOMPARE(loaded.preAssignments, original.preAssignments);
    QCOMPARE(loaded.teacherActivities, original.teacherActivities);

    // 课表逐条比对（不依赖顺序）
    QCOMPARE(loaded.schedule.assignments.size(), original.schedule.assignments.size());
    for (const auto &a : original.schedule.assignments) {
        QVERIFY2(loaded.schedule.assignments.contains(a),
                 qPrintable(QStringLiteral("课表条目丢失: class=%1 slot=%2 half=%3")
                                .arg(a.classId)
                                .arg(a.slotIndex)
                                .arg(a.half)));
    }
}

void TestStore::openNonExistent()
{
    ProjectStore store;
    QVERIFY(!store.open(QStringLiteral("Z:/no/such/file.spk")));
    QVERIFY(!store.lastError().isEmpty());
    // 未打开时 save/load 应失败而不是崩溃
    School s;
    QVERIFY(!store.save(s));
    QVERIFY(!store.load(&s));
}

void TestStore::overwriteSave()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("overwrite.spk"));

    School s1 = makeSampleSchool();
    {
        ProjectStore store;
        QVERIFY2(store.createNew(path), qPrintable(store.lastError()));
        QVERIFY2(store.save(s1), qPrintable(store.lastError()));
    }

    // 修改后再次整体保存（同事务清表重写），再读回应与新内容一致
    s1.name = QStringLiteral("改名后的学校");
    QVERIFY(s1.removeClass(s1.classes.last().id));
    s1.schedule.setAssignment(s1.classes.first().id, 20, 0, s1.courses.first().id);

    {
        ProjectStore store;
        QVERIFY2(store.open(path), qPrintable(store.lastError()));
        QVERIFY2(store.save(s1), qPrintable(store.lastError()));
        store.close();
    }

    School loaded;
    {
        ProjectStore store;
        QVERIFY2(store.open(path), qPrintable(store.lastError()));
        QVERIFY2(store.load(&loaded), qPrintable(store.lastError()));
    }

    QCOMPARE(loaded.name, s1.name);
    QCOMPARE(loaded.classes, s1.classes);
    QCOMPARE(loaded.schedule.assignments.size(), s1.schedule.assignments.size());

    // createNew 覆盖已存在文件：内容应被重置
    {
        ProjectStore store;
        QVERIFY2(store.createNew(path), qPrintable(store.lastError()));
        School empty;
        empty.name = QStringLiteral("空白学校");
        QVERIFY2(store.save(empty), qPrintable(store.lastError()));
        store.close();
    }
    {
        ProjectStore store;
        QVERIFY2(store.open(path), qPrintable(store.lastError()));
        QVERIFY2(store.load(&loaded), qPrintable(store.lastError()));
        QCOMPARE(loaded.name, QStringLiteral("空白学校"));
        QCOMPARE(loaded.classes.size(), 0);
    }
}

QTEST_GUILESS_MAIN(TestStore)

#include "tst_store.moc"
