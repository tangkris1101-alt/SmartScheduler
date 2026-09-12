#include "AppController.h"
#include "CombinedModel.h"
#include "GroupModel.h"
#include "PlanModels.h"
#include "RosterModels.h"

#include <QTemporaryDir>
#include <QSettings>
#include <QtTest/QTest>

// 桥接层单元测试：项目生命周期、模型角色读写、过滤、级联删除、分组成员
class TestApp : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // QSettings 需要明确的组织/应用名（Windows 注册表），且与真实应用隔离
        QCoreApplication::setOrganizationName(QStringLiteral("SmartScheduler"));
        QCoreApplication::setApplicationName(QStringLiteral("SmartSchedulerTest"));
    }
    void projectLifecycle();
    void classModelEditFilterCascade();
    void courseTeacherClassroomRoles();
    void groupModelMembers();
    void planListModelOps();
    void dutyModelOps();
    void combinedModelValidation();
    void constraintsAndPreassign();
};

void TestApp::projectLifecycle()
{
    // 隔离 QSettings 状态
    QSettings().remove(QStringLiteral("recentFiles"));

    AppController app;
    QVERIFY(!app.hasProject());
    QVERIFY(!app.projectDirty());

    app.newProject(QStringLiteral("测试中学"), 5, 7, 4, 20);
    QVERIFY(app.hasProject());
    QCOMPARE(app.schoolName(), QStringLiteral("测试中学"));
    QVERIFY(app.projectDirty()); // 新项目未落盘
    QCOMPARE(app.school()->timeConfig.periodsPerDay, 7);

    // 无路径时 saveProject 失败
    QVERIFY(!app.saveProject());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("life.spk"));
    QVERIFY2(app.saveProjectAs(path), qPrintable(app.lastError()));
    QVERIFY(!app.projectDirty());
    QCOMPARE(app.currentProjectPath(), path);
    QVERIFY(app.recentFiles().contains(path));

    // 加两个班后关闭再打开，数据仍在
    ClassListModel classModel(&app);
    classModel.appendRow();
    classModel.appendRow();
    QCOMPARE(app.classCount(), 2);
    QVERIFY(app.projectDirty());
    QVERIFY(app.saveProject());

    app.closeProject();
    QVERIFY(!app.hasProject());
    QCOMPARE(app.classCount(), 0);

    QVERIFY2(app.openProject(path), qPrintable(app.lastError()));
    QVERIFY(app.hasProject());
    QCOMPARE(app.schoolName(), QStringLiteral("测试中学"));
    QCOMPARE(app.classCount(), 2);
    QVERIFY(!app.projectDirty());

    app.closeProject(); // 释放 SQLite 句柄，避免临时目录删除告警
}

void TestApp::classModelEditFilterCascade()
{
    AppController app;
    app.newProject(QStringLiteral("级联校"), 5, 7, 4, 20);
    ClassListModel model(&app);
    QCOMPARE(model.count(), 0);

    const qint64 id1 = model.appendRow();
    const qint64 id2 = model.appendRow();
    QCOMPARE(model.count(), 2);
    QVERIFY(id1 > 0 && id2 > id1);

    // 角色名读写
    QVERIFY(model.set(0, QStringLiteral("name"), QStringLiteral("高一1班")));
    QVERIFY(model.set(0, QStringLiteral("code"), QStringLiteral("gy1b")));
    QVERIFY(model.set(0, QStringLiteral("priority"), 3));
    QCOMPARE(model.get(0, QStringLiteral("name")).toString(), QStringLiteral("高一1班"));
    QCOMPARE(model.get(0, QStringLiteral("priority")).toInt(), 3);
    QVERIFY(!model.set(0, QStringLiteral("noSuchRole"), 1)); // 未知角色失败
    QCOMPARE(model.idAt(0), id1);

    // 过滤：按名称 / 拼音码
    model.setFilter(QStringLiteral("gy1"));
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.get(0, QStringLiteral("name")).toString(), QStringLiteral("高一1班"));
    model.setFilter(QStringLiteral("不存在"));
    QCOMPARE(model.count(), 0);
    model.setFilter(QString());
    QCOMPARE(model.count(), 2);

    // 级联删除：计划条目 / 不排限制 / 预排随班级一起清理
    auto *school = app.school();
    sched::Course course;
    course.name = QStringLiteral("语文");
    const qint64 courseId = school->addCourse(course);
    sched::PlanEntry e;
    e.classId = id1;
    e.courseId = courseId;
    e.weeklyPeriods = 5;
    school->addPlanEntry(e);
    sched::NoScheduleConstraint ns;
    ns.entityType = sched::NoScheduleConstraint::EntityType::Class;
    ns.entityId = id1;
    ns.slots = {0};
    school->addNoScheduleConstraint(ns);
    QCOMPARE(school->planEntries.size(), 1);

    QVERIFY(model.removeRow(0));
    QCOMPARE(model.count(), 1);
    QCOMPARE(school->planEntries.size(), 0);
    QCOMPARE(school->noScheduleConstraints.size(), 0);
    QVERIFY(!model.removeRow(99)); // 越界失败
}

void TestApp::courseTeacherClassroomRoles()
{
    AppController app;
    app.newProject(QStringLiteral("特征校"), 5, 7, 4, 20);

    CourseListModel courses(&app);
    courses.appendRow();
    QVERIFY(courses.set(0, QStringLiteral("isMajor"), true));
    QVERIFY(courses.set(0, QStringLiteral("isScience"), true));
    QVERIFY(courses.set(0, QStringLiteral("evenSpread"), true));
    QVERIFY(courses.set(0, QStringLiteral("progressAlignCount"), 4));
    QVERIFY(courses.set(0, QStringLiteral("amPmPolicy"), 1));
    QCOMPARE(courses.get(0, QStringLiteral("isMajor")).toBool(), true);
    QCOMPARE(courses.get(0, QStringLiteral("progressAlignCount")).toInt(), 4);
    QCOMPARE(courses.get(0, QStringLiteral("amPmText")).toString(), QStringLiteral("优先上午"));
    QVERIFY(app.school()->courses.first().isMajor);

    TeacherListModel teachers(&app);
    teachers.appendRow();
    QVERIFY(teachers.set(0, QStringLiteral("maxPeriodsPerDay"), 6));
    QVERIFY(teachers.set(0, QStringLiteral("mutualExclusionGroup"), 2));
    QCOMPARE(teachers.get(0, QStringLiteral("maxPeriodsPerDay")).toInt(), 6);
    QCOMPARE(app.school()->teachers.first().mutualExclusionGroup, 2);

    ClassroomListModel rooms(&app);
    rooms.appendRow();
    QVERIFY(rooms.set(0, QStringLiteral("capacity"), 3));
    QCOMPARE(rooms.get(0, QStringLiteral("capacity")).toInt(), 3);
    QVERIFY(rooms.set(0, QStringLiteral("capacity"), 0)); // 容量下限为 1
    QCOMPARE(rooms.get(0, QStringLiteral("capacity")).toInt(), 1);
}

void TestApp::groupModelMembers()
{
    AppController app;
    app.newProject(QStringLiteral("分组校"), 5, 7, 4, 20);

    ClassListModel classes(&app);
    const qint64 c1 = classes.appendRow();
    const qint64 c2 = classes.appendRow();
    classes.set(0, QStringLiteral("name"), QStringLiteral("一班"));
    classes.set(1, QStringLiteral("name"), QStringLiteral("二班"));

    GroupModel groups(&app);
    QCOMPARE(groups.count(), 0);
    const qint64 gid = groups.addGroup(static_cast<int>(sched::Group::Type::Grade),
                                       QStringLiteral("高一年级"));
    QVERIFY(gid > 0);
    QCOMPARE(groups.count(), 1);
    QCOMPARE(groups.get(0, QStringLiteral("name")).toString(), QStringLiteral("高一年级"));
    QCOMPARE(groups.get(0, QStringLiteral("typeName")).toString(), QStringLiteral("年级"));

    // 候选成员按类型取班级名单
    QCOMPARE(groups.candidates(gid).size(), 2);
    QVERIFY(!groups.isMember(gid, c1));
    groups.toggleMember(gid, c1);
    groups.toggleMember(gid, c2);
    QVERIFY(groups.isMember(gid, c1));
    QCOMPARE(groups.get(0, QStringLiteral("memberCount")).toInt(), 2);
    QVERIFY(groups.get(0, QStringLiteral("memberNames")).toString().contains(QStringLiteral("一班")));
    groups.toggleMember(gid, c1);
    QVERIFY(!groups.isMember(gid, c1));
    QCOMPARE(groups.get(0, QStringLiteral("memberCount")).toInt(), 1);

    // 改类型会清空成员
    QVERIFY(groups.set(0, QStringLiteral("type"),
                       static_cast<int>(sched::Group::Type::TeacherGroup)));
    QCOMPARE(groups.get(0, QStringLiteral("memberCount")).toInt(), 0);
    QCOMPARE(groups.candidates(gid).size(), 0); // 暂无老师

    // 改名 + 删除
    QVERIFY(groups.set(0, QStringLiteral("name"), QStringLiteral("语文组")));
    QVERIFY(groups.removeRow(0));
    QCOMPARE(groups.count(), 0);
    QVERIFY(app.projectDirty());
}

// 造一所 2 班 2 课 2 师的最小校例，返回各 id
struct MiniSchool {
    qint64 class1, class2, yuwen, shuxue, tZhang, tWang;
};

static MiniSchool makeMini(AppController &app)
{
    MiniSchool ids{};
    app.newProject(QStringLiteral("M3b校"), 5, 7, 4, 20);
    sched::ClassInfo c1; c1.name = QStringLiteral("1班");
    sched::ClassInfo c2; c2.name = QStringLiteral("2班");
    ids.class1 = app.school()->addClass(c1);
    ids.class2 = app.school()->addClass(c2);
    sched::Course yw; yw.name = QStringLiteral("语文");
    sched::Course sx; sx.name = QStringLiteral("数学");
    ids.yuwen = app.school()->addCourse(yw);
    ids.shuxue = app.school()->addCourse(sx);
    sched::Teacher tz; tz.name = QStringLiteral("张");
    sched::Teacher tw; tw.name = QStringLiteral("王");
    ids.tZhang = app.school()->addTeacher(tz);
    ids.tWang = app.school()->addTeacher(tw);
    return ids;
}

void TestApp::planListModelOps()
{
    AppController app;
    const MiniSchool ids = makeMini(app);

    PlanListModel model(&app);
    QCOMPARE(model.count(), 0);
    model.setCurrentClassId(ids.class1);

    const qint64 e1 = model.addEntry(ids.yuwen);
    QVERIFY(e1 > 0);
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.get(0, QStringLiteral("courseName")).toString(), QStringLiteral("语文"));
    QCOMPARE(model.get(0, QStringLiteral("weeklyPeriods")).toDouble(), 2.0);

    // 字段写入 + 解析角色
    QVERIFY(model.set(0, QStringLiteral("weeklyPeriods"), 4.5));
    QVERIFY(model.set(0, QStringLiteral("chainedCount"), 1));
    QVERIFY(model.set(0, QStringLiteral("chainLength"), 2));
    QVERIFY(model.set(0, QStringLiteral("progressGroup"), 2));
    model.toggleTeacher(0, ids.tZhang);
    QCOMPARE(model.get(0, QStringLiteral("teacherNames")).toString(), QStringLiteral("张"));
    QCOMPARE(model.get(0, QStringLiteral("progressGroup")).toInt(), 2);
    QCOMPARE(model.totalWeeklyPeriods(), 4.5);

    // 重复课程警告
    model.addEntry(ids.yuwen);
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.get(0, QStringLiteral("duplicate")).toBool(), true);
    QCOMPARE(model.get(1, QStringLiteral("duplicate")).toBool(), true);
    QVERIFY(model.removeRow(1));
    QCOMPARE(model.get(0, QStringLiteral("duplicate")).toBool(), false);

    // 复制到 2 班：首次全部复制，再次跳过，overwrite 后覆盖
    QCOMPARE(model.copyToClass(ids.class2, false), 1);
    QCOMPARE(model.copyToClass(ids.class2, false), 0); // 已存在跳过
    QVERIFY(model.set(0, QStringLiteral("weeklyPeriods"), 6));
    QCOMPARE(model.copyToClass(ids.class2, true), 1);
    const auto &entries = app.school()->planEntries;
    double copiedPeriods = 0;
    for (const auto &e : entries) {
        if (e.classId == ids.class2)
            copiedPeriods = e.weeklyPeriods;
    }
    QCOMPARE(copiedPeriods, 6.0);

    // 切换班级视图
    model.setCurrentClassId(ids.class2);
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.get(0, QStringLiteral("weeklyPeriods")).toDouble(), 6.0);
    QVERIFY(app.projectDirty());
}

void TestApp::dutyModelOps()
{
    AppController app;
    const MiniSchool ids = makeMini(app);

    DutyModel duty(&app);
    duty.setCurrentTeacherId(ids.tZhang);
    QCOMPARE(duty.count(), 0);

    // 批量设置任教：张老师教 1/2 班语文各 5 节
    QCOMPARE(duty.setDuty(ids.yuwen, {ids.class1, ids.class2}, 5), 2);
    QCOMPARE(duty.count(), 2);
    QCOMPARE(duty.setDuty(ids.yuwen, {ids.class1, ids.class2}, 5), 0); // 重复设置不再新增
    QCOMPARE(app.school()->planEntries.size(), 2);

    // 进度组设置
    QVERIFY(duty.set(0, QStringLiteral("progressGroup"), 1));
    QCOMPARE(app.school()->planEntries[0].progressGroup, 1);

    // 解除任教：从条目中摘掉老师（条目保留）
    QVERIFY(duty.removeRow(0));
    QCOMPARE(duty.count(), 1);
    QCOMPARE(app.school()->planEntries.size(), 2); // 条目仍在，仅老师列表变化
    QVERIFY(app.school()->planEntries[0].teacherIds.isEmpty()
            || !app.school()->planEntries[0].teacherIds.contains(ids.tZhang));
}

void TestApp::combinedModelValidation()
{
    AppController app;
    const MiniSchool ids = makeMini(app);

    // 1/2 班体育式合班：这里用语文合班，两班同课同老师
    DutyModel duty(&app);
    duty.setCurrentTeacherId(ids.tZhang);
    duty.setDuty(ids.yuwen, {ids.class1, ids.class2}, 2);
    // 标记为合班课
    for (auto &e : app.school()->planEntries)
        e.isCombined = true;
    // 再加一条 1班数学（王老师）合班课 —— 课程不一致
    sched::PlanEntry e3;
    e3.classId = ids.class1;
    e3.courseId = ids.shuxue;
    e3.teacherIds = {ids.tWang};
    e3.weeklyPeriods = 2;
    e3.isCombined = true;
    const qint64 entryMath = app.school()->addPlanEntry(e3);

    CombinedModel combined(&app);
    QCOMPARE(combined.unlinkedEntries().size(), 3); // 三条合班课均未关联

    const qint64 gid = combined.addGroup(QStringLiteral("语文合班"));
    QVERIFY(gid > 0);
    // 找语文两条 entry id
    qint64 entry1 = -1, entry2 = -1;
    for (const auto &e : app.school()->planEntries) {
        if (e.courseId == ids.yuwen && e.classId == ids.class1)
            entry1 = e.id;
        if (e.courseId == ids.yuwen && e.classId == ids.class2)
            entry2 = e.id;
    }
    QVERIFY(entry1 > 0 && entry2 > 0);

    // 首条选入空组总是允许（确定组课程）
    QVERIFY(combined.addEntry(gid, entry1).isEmpty());
    // 课程不一致 → 阻止
    const QString err1 = combined.addEntry(gid, entryMath);
    QVERIFY(!err1.isEmpty());
    QCOMPARE(combined.get(0, QStringLiteral("memberCount")).toInt(), 1);

    // 同课同老师 → 正常选入
    QVERIFY(combined.addEntry(gid, entry2).isEmpty());
    QCOMPARE(combined.get(0, QStringLiteral("memberCount")).toInt(), 2);
    QCOMPARE(combined.get(0, QStringLiteral("valid")).toBool(), true);
    QCOMPARE(combined.get(0, QStringLiteral("teacherNames")).toString(), QStringLiteral("张"));
    QCOMPARE(combined.unlinkedEntries().size(), 1); // 只剩数学那条

    // 无共同老师 → 阻止：把 2班语文老师换成王，再验证 valid=false
    for (auto &e : app.school()->planEntries) {
        if (e.id == entry2)
            e.teacherIds = {ids.tWang};
    }
    QCOMPARE(combined.get(0, QStringLiteral("valid")).toBool(), false);

    // 移出 + 解散
    combined.removeEntry(gid, ids.class2, ids.yuwen);
    QCOMPARE(combined.get(0, QStringLiteral("memberCount")).toInt(), 1);
    QVERIFY(combined.removeRow(0));
    QCOMPARE(combined.count(), 0);
    QCOMPARE(app.school()->planEntries.size(), 3); // 解散只解除关联，条目不删

    // 无共同老师 → 阻止：数学组先收 1班（王老师），2班数学只有张老师
    sched::PlanEntry e4;
    e4.classId = ids.class2;
    e4.courseId = ids.shuxue;
    e4.teacherIds = {ids.tZhang};
    e4.weeklyPeriods = 2;
    e4.isCombined = true;
    const qint64 entryMath2 = app.school()->addPlanEntry(e4);
    const qint64 gid2 = combined.addGroup(QStringLiteral("数学合班"));
    QVERIFY(combined.addEntry(gid2, entryMath).isEmpty());
    QVERIFY(!combined.addEntry(gid2, entryMath2).isEmpty()); // 老师无交集被拒
    QCOMPARE(combined.get(0, QStringLiteral("memberCount")).toInt(), 1); // 第一组已解散，数学组在 row 0
}

void TestApp::constraintsAndPreassign()
{
    AppController app;
    const MiniSchool ids = makeMini(app);

    // 不排时间：创建→加 slot→去 slot→清空
    QCOMPARE(app.noScheduleSlots(2, ids.tZhang).size(), 0);
    app.setNoScheduleSlot(2, ids.tZhang, 0, true);
    app.setNoScheduleSlot(2, ids.tZhang, 3, true);
    QCOMPARE(app.noScheduleSlots(2, ids.tZhang).size(), 2);
    QCOMPARE(app.school()->noScheduleConstraints.size(), 1);
    app.setNoScheduleSlot(2, ids.tZhang, 0, false);
    QCOMPARE(app.noScheduleSlots(2, ids.tZhang).size(), 1);
    app.setNoScheduleSlot(2, ids.tZhang, 3, false);
    QCOMPARE(app.school()->noScheduleConstraints.size(), 0); // 空约束自动删除
    app.setNoScheduleSlot(0, ids.class1, 34, true);
    app.clearNoSchedule(0, ids.class1);
    QCOMPARE(app.noScheduleSlots(0, ids.class1).size(), 0);

    // 预排：同班同 slot 唯一；toggle 往返
    DutyModel duty(&app);
    duty.setCurrentTeacherId(ids.tZhang);
    duty.setDuty(ids.yuwen, {ids.class1}, 5);
    QCOMPARE(app.planCourses(ids.class1).size(), 1);
    app.togglePreAssignment(ids.class1, ids.yuwen, 10);
    QCOMPARE(app.preAssignments(ids.class1).size(), 1);
    app.togglePreAssignment(ids.class1, ids.shuxue, 10); // 覆盖式取消旧预排
    QCOMPARE(app.preAssignments(ids.class1).size(), 0);
    app.togglePreAssignment(ids.class1, ids.yuwen, 10);
    app.togglePreAssignment(ids.class1, ids.shuxue, 10); // 同 slot → 先取消
    QCOMPARE(app.preAssignments(ids.class1).size(), 0);

    // 老师活动：toggle 往返
    app.toggleTeacherActivity(ids.tZhang, 19, ids.yuwen);
    QCOMPARE(app.teacherActivities(ids.tZhang).size(), 1);
    QCOMPARE(app.teacherActivities(ids.tZhang).first().toMap().value(QStringLiteral("courseName")).toString(),
             QStringLiteral("语文"));
    app.toggleTeacherActivity(ids.tZhang, 19, ids.yuwen);
    QCOMPARE(app.teacherActivities(ids.tZhang).size(), 0);
    QVERIFY(app.projectDirty());
}

QTEST_GUILESS_MAIN(TestApp)

#include "tst_app.moc"
