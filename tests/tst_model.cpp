#include "model/School.h"

#include <QtTest/QTest>

using namespace sched;

// 领域模型单元测试：时间结构换算、School 增删改与级联删除
class TestModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void slotIndexConversion();
    void addAndFind();
    void scheduleBasics();
    void teacherOccupancy();
    void cascadeRemoveClass();
    void cascadeRemoveCourse();
    void cascadeRemoveTeacher();
    void cascadeRemoveClassroomAndGroup();
};

void TestModel::slotIndexConversion()
{
    TimeConfig tc;
    tc.daysPerWeek = 5;
    tc.periodsPerDay = 7;
    tc.morningPeriods = 4;
    tc.weeksPerTerm = 20;

    QCOMPARE(tc.slotCount(), 35);
    QCOMPARE(tc.slotIndex(0, 0), 0);
    QCOMPARE(tc.slotIndex(1, 0), 7);
    QCOMPARE(tc.slotIndex(4, 6), 34);

    // 换算可逆
    for (int i = 0; i < tc.slotCount(); ++i) {
        QCOMPARE(tc.slotIndex(tc.slotDay(i), tc.slotPeriod(i)), i);
        QVERIFY(tc.isValidSlot(i));
    }
    QVERIFY(!tc.isValidSlot(-1));
    QVERIFY(!tc.isValidSlot(tc.slotCount()));
}

void TestModel::addAndFind()
{
    School school;
    school.name = QStringLiteral("测试中学");

    ClassInfo c1;
    c1.name = QStringLiteral("高一1班");
    ClassInfo c2;
    c2.name = QStringLiteral("高一2班");
    const qint64 id1 = school.addClass(c1);
    const qint64 id2 = school.addClass(c2);
    QVERIFY(id1 > 0);
    QCOMPARE(id2, id1 + 1); // 自动分配递增 id

    QCOMPARE(school.classes.size(), 2);
    QCOMPARE(school.findClass(id1)->name, QStringLiteral("高一1班"));
    QCOMPARE(school.findClass(999), nullptr);

    // 修改（通过 find 拿到的指针就地编辑）
    school.findClass(id1)->remark = QStringLiteral("重点班");
    QCOMPARE(school.findClass(id1)->remark, QStringLiteral("重点班"));

    Course yuwen;
    yuwen.name = QStringLiteral("语文");
    yuwen.isMajor = true;
    const qint64 courseId = school.addCourse(yuwen);
    QCOMPARE(school.findCourse(courseId)->progressAlignCount, 2); // 默认值

    QVERIFY(school.removeClass(id2));
    QCOMPARE(school.classes.size(), 1);
    QVERIFY(!school.removeClass(id2)); // 重复删除返回 false
}

void TestModel::scheduleBasics()
{
    Schedule s;
    s.setAssignment(1, 0, 0, 10); // 班1 周一第1节 整节 课程10
    s.setAssignment(1, 1, 1, 11); // 班1 周一第2节 单周 课程11
    s.setAssignment(1, 1, 2, 12); // 班1 周一第2节 双周 课程12
    s.setAssignment(2, 0, 0, 10); // 班2 周一第1节 整节 课程10（合班场景）

    QCOMPARE(s.courseAt(1, 0, 0), std::optional<qint64>(10));
    QCOMPARE(s.courseAt(1, 1, 1), std::optional<qint64>(11));
    QCOMPARE(s.courseAt(1, 1, 2), std::optional<qint64>(12));
    QCOMPARE(s.courseAt(1, 1, 0), std::nullopt); // 整节查询不命中单双周条目
    QCOMPARE(s.courseAt(1, 0, 1), std::optional<qint64>(10)); // 整节占用覆盖单双周查询

    QCOMPARE(s.assignmentsAt(0).size(), 2);
    QCOMPARE(s.assignmentsForClass(2).size(), 1);
    QCOMPARE(s.assignmentsForCourse(10).size(), 2);

    // 同键覆盖
    s.setAssignment(1, 0, 0, 13);
    QCOMPARE(s.courseAt(1, 0, 0), std::optional<qint64>(13));
    QCOMPARE(s.assignments.size(), 4);

    QVERIFY(s.removeAssignment(1, 0, 0));
    QVERIFY(!s.removeAssignment(1, 0, 0));
    QCOMPARE(s.courseAt(1, 0, 0), std::nullopt);
}

void TestModel::teacherOccupancy()
{
    School school;

    ClassInfo c1;
    c1.name = QStringLiteral("班1");
    ClassInfo c2;
    c2.name = QStringLiteral("班2");
    const qint64 class1 = school.addClass(c1);
    const qint64 class2 = school.addClass(c2);

    Course yuwen;
    yuwen.name = QStringLiteral("语文");
    Course shuxue;
    shuxue.name = QStringLiteral("数学");
    const qint64 course1 = school.addCourse(yuwen);
    const qint64 course2 = school.addCourse(shuxue);

    Teacher t1;
    t1.name = QStringLiteral("张老师");
    const qint64 teacher1 = school.addTeacher(t1);

    // 张老师同时教 班1语文 和 班2数学
    PlanEntry e1;
    e1.classId = class1;
    e1.courseId = course1;
    e1.teacherIds = {teacher1};
    e1.weeklyPeriods = 5;
    school.addPlanEntry(e1);
    PlanEntry e2;
    e2.classId = class2;
    e2.courseId = course2;
    e2.teacherIds = {teacher1};
    e2.weeklyPeriods = 4;
    school.addPlanEntry(e2);

    // 周一第1节：班1 语文；班2 数学 —— 张老师被排重（冲突场景）
    school.schedule.setAssignment(class1, 0, 0, course1);
    school.schedule.setAssignment(class2, 0, 0, course2);
    // 周一第2节：班1 语文
    school.schedule.setAssignment(class1, 1, 0, course1);

    QCOMPARE(school.teacherOccupancy(teacher1, 0).size(), 2); // 检测到两班同时占用
    QCOMPARE(school.teacherOccupancy(teacher1, 1).size(), 1);
    QCOMPARE(school.teacherOccupancy(teacher1, 2).size(), 0);

    // 老师活动时间也算占用
    TeacherActivity act;
    act.teacherId = teacher1;
    act.slotIndex = 5;
    act.activityCourseId = course2;
    school.addTeacherActivity(act);
    QCOMPARE(school.teacherOccupancy(teacher1, 5).size(), 1);

    // 不挂老师的格子（如第 9 节体育活动）不进老师个人课表 —— 打印与 Excel 导出共用此处
    school.schedule.setAssignment(class1, 6, 0, course1);
    QCOMPARE(school.teacherOccupancy(teacher1, 6).size(), 1);
    school.findPlanEntry(school.planEntries.first().id)->noLoad = true;
    QCOMPARE(school.teacherOccupancy(teacher1, 6).size(), 0);
    school.findPlanEntry(school.planEntries.first().id)->noLoad = false;
    QCOMPARE(school.teacherOccupancy(teacher1, 6).size(), 1); // 去掉标记后照常显示
}

void TestModel::cascadeRemoveClass()
{
    School school;

    ClassInfo c1;
    c1.name = QStringLiteral("班1");
    const qint64 class1 = school.addClass(c1);
    ClassInfo c2;
    c2.name = QStringLiteral("班2");
    const qint64 class2 = school.addClass(c2);

    Course yuwen;
    yuwen.name = QStringLiteral("语文");
    const qint64 course1 = school.addCourse(yuwen);
    Course shuxue;
    shuxue.name = QStringLiteral("数学");
    const qint64 course2 = school.addCourse(shuxue);

    PlanEntry e;
    e.classId = class1;
    e.courseId = course1;
    e.weeklyPeriods = 5;
    school.addPlanEntry(e);

    PreAssignment p;
    p.classId = class1;
    p.courseId = course2;
    p.slotIndex = 3;
    school.addPreAssignment(p);

    NoScheduleConstraint ns;
    ns.entityType = NoScheduleConstraint::EntityType::Class;
    ns.entityId = class1;
    ns.slots = {0, 1};
    school.addNoScheduleConstraint(ns);

    school.schedule.setAssignment(class1, 0, 0, course1);
    school.schedule.setAssignment(class2, 0, 0, course1);

    CombinedGroup cg;
    cg.name = QStringLiteral("语文合班");
    cg.entries = {{class1, course1}, {class2, course1}};
    school.addCombinedGroup(cg);

    QVERIFY(school.removeClass(class1));

    QCOMPARE(school.planEntries.size(), 0);          // 计划条目删除
    QCOMPARE(school.preAssignments.size(), 0);       // 预排删除
    QCOMPARE(school.noScheduleConstraints.size(), 0); // 不排课限制删除
    QCOMPARE(school.schedule.assignments.size(), 1); // 该班课表删除，班2 保留
    QCOMPARE(school.combinedGroups.size(), 1);       // 合班组保留但只剩班2
    QCOMPARE(school.combinedGroups.first().entries.size(), 1);
    QCOMPARE(school.combinedGroups.first().entries.first().first, class2);

    // 再删班2：合班组变空后应被一并删除
    QVERIFY(school.removeClass(class2));
    QCOMPARE(school.combinedGroups.size(), 0);
}

void TestModel::cascadeRemoveCourse()
{
    School school;

    ClassInfo c1;
    c1.name = QStringLiteral("班1");
    const qint64 class1 = school.addClass(c1);

    Course yuwen;
    yuwen.name = QStringLiteral("语文");
    const qint64 course1 = school.addCourse(yuwen);
    Course tiyu;
    tiyu.name = QStringLiteral("体育");
    const qint64 course2 = school.addCourse(tiyu);

    Teacher t1;
    t1.name = QStringLiteral("王老师");
    const qint64 teacher1 = school.addTeacher(t1);

    // 体育同步到语文
    PlanEntry e1;
    e1.classId = class1;
    e1.courseId = course2;
    e1.syncCourseId = course1;
    e1.weeklyPeriods = 2;
    school.addPlanEntry(e1);
    // 另一条语文计划
    PlanEntry e2;
    e2.classId = class1;
    e2.courseId = course1;
    e2.weeklyPeriods = 5;
    school.addPlanEntry(e2);

    PreAssignment p;
    p.classId = class1;
    p.courseId = course1;
    p.slotIndex = 0;
    school.addPreAssignment(p);

    TeacherActivity act;
    act.teacherId = teacher1;
    act.slotIndex = 6;
    act.activityCourseId = course1;
    school.addTeacherActivity(act);

    NoScheduleConstraint ns;
    ns.entityType = NoScheduleConstraint::EntityType::Course;
    ns.entityId = course1;
    ns.slots = {30};
    school.addNoScheduleConstraint(ns);

    CombinedGroup cg;
    cg.name = QStringLiteral("合班");
    cg.entries = {{class1, course1}};
    school.addCombinedGroup(cg);

    school.schedule.setAssignment(class1, 0, 0, course1);
    school.schedule.setAssignment(class1, 1, 0, course2);

    QVERIFY(school.removeCourse(course1));

    QCOMPARE(school.planEntries.size(), 1); // 语文条目删除，体育保留
    QCOMPARE(school.planEntries.first().courseId, course2);
    QCOMPARE(school.planEntries.first().syncCourseId, -1); // 同步引用被重置
    QCOMPARE(school.preAssignments.size(), 0);
    QCOMPARE(school.teacherActivities.size(), 0);
    QCOMPARE(school.noScheduleConstraints.size(), 0);
    QCOMPARE(school.combinedGroups.size(), 0); // 合班组成员被移除后组为空删除
    QCOMPARE(school.schedule.assignments.size(), 1);
    QCOMPARE(school.schedule.assignments.first().courseId, course2);
}

void TestModel::cascadeRemoveTeacher()
{
    School school;

    ClassInfo c1;
    c1.name = QStringLiteral("班1");
    const qint64 class1 = school.addClass(c1);
    Course yuwen;
    yuwen.name = QStringLiteral("语文");
    const qint64 course1 = school.addCourse(yuwen);

    Teacher t1;
    t1.name = QStringLiteral("张老师");
    const qint64 teacher1 = school.addTeacher(t1);
    Teacher t2;
    t2.name = QStringLiteral("李老师");
    const qint64 teacher2 = school.addTeacher(t2);

    PlanEntry e;
    e.classId = class1;
    e.courseId = course1;
    e.teacherIds = {teacher1, teacher2};
    e.weeklyPeriods = 5;
    school.addPlanEntry(e);

    TeacherActivity act;
    act.teacherId = teacher1;
    act.slotIndex = 2;
    act.activityCourseId = course1;
    school.addTeacherActivity(act);

    NoScheduleConstraint ns;
    ns.entityType = NoScheduleConstraint::EntityType::Teacher;
    ns.entityId = teacher1;
    ns.slots = {0};
    school.addNoScheduleConstraint(ns);

    Group g;
    g.type = Group::Type::TeacherGroup;
    g.name = QStringLiteral("语文教研组");
    g.memberIds = {teacher1, teacher2};
    school.addGroup(g);

    QVERIFY(school.removeTeacher(teacher1));

    QCOMPARE(school.planEntries.size(), 1); // 条目保留
    QCOMPARE(school.planEntries.first().teacherIds.size(), 1);
    QCOMPARE(school.planEntries.first().teacherIds.first(), teacher2);
    QCOMPARE(school.teacherActivities.size(), 0);
    QCOMPARE(school.noScheduleConstraints.size(), 0);
    QCOMPARE(school.groups.first().memberIds.size(), 1); // 教研组成员摘除
}

void TestModel::cascadeRemoveClassroomAndGroup()
{
    School school;

    Classroom room;
    room.name = QStringLiteral("实验室");
    room.capacity = 2;
    const qint64 roomId = school.addClassroom(room);

    ClassInfo c1;
    c1.name = QStringLiteral("班1");
    const qint64 class1 = school.addClass(c1);
    Course wuli;
    wuli.name = QStringLiteral("物理");
    const qint64 course1 = school.addCourse(wuli);

    PlanEntry e;
    e.classId = class1;
    e.courseId = course1;
    e.classroomId = roomId;
    e.weeklyPeriods = 2;
    school.addPlanEntry(e);

    NoScheduleConstraint ns;
    ns.entityType = NoScheduleConstraint::EntityType::Classroom;
    ns.entityId = roomId;
    ns.slots = {7};
    school.addNoScheduleConstraint(ns);

    QVERIFY(school.removeClassroom(roomId));
    QCOMPARE(school.planEntries.first().classroomId, -1); // 引用重置
    QCOMPARE(school.noScheduleConstraints.size(), 0);

    // 删除年级名单：班级 gradeId 应重置
    Group grade;
    grade.type = Group::Type::Grade;
    grade.name = QStringLiteral("高一年级");
    grade.memberIds = {class1};
    const qint64 gradeId = school.addGroup(grade);
    school.findClass(class1)->gradeId = gradeId;

    QVERIFY(school.removeGroup(gradeId));
    QCOMPARE(school.findClass(class1)->gradeId, -1);
    QCOMPARE(school.groups.size(), 0);
}

QTEST_GUILESS_MAIN(TestModel)

#include "tst_model.moc"
