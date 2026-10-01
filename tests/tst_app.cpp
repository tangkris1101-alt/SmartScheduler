#include "AdjustController.h"
#include "AppController.h"
#include "CombinedModel.h"
#include "GroupModel.h"
#include "PlanModels.h"
#include "PrintController.h"
#include "RosterModels.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QSettings>
#include <QSignalSpy>
#include <QGuiApplication>
#include <QUrl>
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
    void defaultEntityNames();
    void courseTeacherClassroomRoles();
    void groupModelMembers();
    void planListModelOps();
    void dutyModelOps();
    void combinedModelValidation();
    void constraintsAndPreassign();
    void gradeMask();
    void updateProjectInfo();
    void qualityReport();
    void recentFilesOps();
    void batchClassesWithAutoGroup();
    void batchOps();
    void aiToolLayer();
    void aiToolsExtended();
    void aiTimeSyncTools();
    void aiCombinedGroupTools();
    void aiNoLoadTool();
    void schedulerBridge();
    void adjustRulesMatrix();
    void adjustUndoRedo();
    void unplacedAndClear();
    void temporaryModeOps();
    void printExport();
    void sampleSchool();
    void projectTemplates();
    void versionAndUpdate();
    void logExport();
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

void TestApp::defaultEntityNames()
{
    AppController app;
    app.newProject(QStringLiteral("命名校"), 5, 7, 4, 20);

    ClassListModel classes(&app);
    classes.appendRow();
    QCOMPARE(classes.get(0, QStringLiteral("name")).toString(), QStringLiteral("1班"));
    classes.appendRow();
    QCOMPARE(classes.get(1, QStringLiteral("name")).toString(), QStringLiteral("2班"));
    // 不规则命名不打断顺延；空出的序号优先回填
    classes.set(1, QStringLiteral("name"), QStringLiteral("实验班"));
    classes.appendRow();
    QCOMPARE(classes.get(2, QStringLiteral("name")).toString(), QStringLiteral("2班"));
    // 统一前缀顺延：高一1班 / 高一3班 → 高一4班
    classes.set(0, QStringLiteral("name"), QStringLiteral("高一1班"));
    classes.set(2, QStringLiteral("name"), QStringLiteral("高一3班"));
    classes.appendRow();
    QCOMPARE(classes.get(3, QStringLiteral("name")).toString(), QStringLiteral("高一4班"));
    // 前缀混合（高一/高二并存）→ 回退 base 去重
    classes.set(3, QStringLiteral("name"), QStringLiteral("高二1班"));
    classes.appendRow();
    QCOMPARE(classes.get(4, QStringLiteral("name")).toString(), QStringLiteral("新班级"));
    classes.appendRow();
    QCOMPARE(classes.get(5, QStringLiteral("name")).toString(), QStringLiteral("新班级2"));

    TeacherListModel teachers(&app);
    teachers.appendRow();
    teachers.appendRow();
    QCOMPARE(teachers.get(0, QStringLiteral("name")).toString(), QStringLiteral("新老师"));
    QCOMPARE(teachers.get(1, QStringLiteral("name")).toString(), QStringLiteral("新老师2"));
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
    // 「不计课时、不挂老师」列：界面开关写的就是这一对角色
    QCOMPARE(model.get(0, QStringLiteral("noLoad")).toBool(), false);
    QVERIFY(model.set(0, QStringLiteral("noLoad"), true));
    QCOMPARE(model.get(0, QStringLiteral("noLoad")).toBool(), true);
    QVERIFY(model.set(0, QStringLiteral("noLoad"), false));
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

void TestApp::gradeMask()
{
    AppController app;
    const MiniSchool ids = makeMini(app);
    sched::Group g;
    g.type = sched::Group::Type::Grade;
    g.name = QStringLiteral("一年级");
    const qint64 gid = app.school()->addGroup(g);
    app.school()->findGroup(gid)->memberIds = {ids.class1, ids.class2};

    // 年级组列表
    const QVariantList groups = app.gradeGroups();
    QCOMPARE(groups.size(), 1);
    QCOMPARE(groups.first().toMap().value(QStringLiteral("memberCount")).toInt(), 2);

    // 作息 5 天 × 5 节：每天第 6-7 节（共 10 格/班）禁排
    app.applyGradeMask(gid, 5, 5, true);
    QCOMPARE(app.noScheduleSlots(0, ids.class1).size(), 10);
    QCOMPARE(app.noScheduleSlots(0, ids.class2).size(), 10);
    QVERIFY(app.noScheduleSlots(0, ids.class1).contains(5)); // 周一第 6 节
    QVERIFY(!app.noScheduleSlots(0, ids.class1).contains(4)); // 周一第 5 节仍在

    // 课表数据带 blocked 标记
    const QVariantList row = app.classRow(ids.class1);
    QVERIFY(row[5].toMap().value(QStringLiteral("blocked")).toBool());
    QVERIFY(!row[4].toMap().value(QStringLiteral("blocked")).toBool());

    // 改设为 4 天 × 7 节：旧遮罩先撤销，只保留周六整列（7 格）
    app.applyGradeMask(gid, 4, 7, true);
    QCOMPARE(app.noScheduleSlots(0, ids.class1).size(), 7);
    QVERIFY(app.noScheduleSlots(0, ids.class1).contains(28)); // 周六第 1 节
    QVERIFY(!app.noScheduleSlots(0, ids.class1).contains(5)); // 旧遮罩已还原

    // 清除：按记录的遮罩精确还原
    app.applyGradeMask(gid, 5, 7, false);
    QCOMPARE(app.noScheduleSlots(0, ids.class1).size(), 0);
    QCOMPARE(app.school()->noScheduleConstraints.size(), 0);
    QVERIFY(!app.classRow(ids.class1)[5].toMap().value(QStringLiteral("blocked")).toBool());

    // 越界取值被钳制；非年级组忽略；无遮罩时清除不改数据
    app.applyGradeMask(gid, 99, 99, true);
    QCOMPARE(app.school()->findGroup(gid)->maskDays, 5);
    QCOMPARE(app.noScheduleSlots(0, ids.class1).size(), 0);
    app.applyGradeMask(-1, 1, 1, true);
    QCOMPARE(app.school()->noScheduleConstraints.size(), 0);
}

void TestApp::updateProjectInfo()
{
    AppController app;
    const MiniSchool ids = makeMini(app);
    DutyModel duty(&app);
    duty.setCurrentTeacherId(ids.tZhang);
    duty.setDuty(ids.yuwen, {ids.class1}, 5);
    app.setNoScheduleSlot(2, ids.tZhang, 0, true);
    app.togglePreAssignment(ids.class1, ids.yuwen, 10);
    QCOMPARE(app.constraintCount(), 2);

    // 仅改名/上午节数/学期周数：结构未变，约束保留
    app.updateProjectInfo(QStringLiteral("改名小学"), 5, 7, 3, 18);
    QCOMPARE(app.schoolName(), QStringLiteral("改名小学"));
    QCOMPARE(app.morningPeriods(), 3);
    QCOMPARE(app.weeksPerTerm(), 18);
    QCOMPARE(app.constraintCount(), 2);
    QVERIFY(app.projectDirty());

    // 空名忽略；越界值被钳制
    app.updateProjectInfo(QStringLiteral("  "), 5, 7, 3, 18);
    QCOMPARE(app.schoolName(), QStringLiteral("改名小学"));
    app.updateProjectInfo(QStringLiteral("改名小学"), 5, 7, 99, 99);
    QCOMPARE(app.morningPeriods(), 7);
    QCOMPARE(app.weeksPerTerm(), 30);
    QCOMPARE(app.constraintCount(), 2);

    // 每周天数变化：结构变更，课表与槽位约束清空
    app.updateProjectInfo(QStringLiteral("改名小学"), 6, 7, 3, 18);
    QCOMPARE(app.daysPerWeek(), 6);
    QCOMPARE(app.constraintCount(), 0);
    QVERIFY(!app.hasSchedule());
}

void TestApp::qualityReport()
{
    AppController app;
    const MiniSchool ids = makeMini(app);
    sched::Group g;
    g.type = sched::Group::Type::Grade;
    g.name = QStringLiteral("一年级");
    const qint64 gid = app.school()->addGroup(g);
    app.school()->findGroup(gid)->memberIds = {ids.class1, ids.class2};

    // 无课表时也要能出报告（指标为 0 / 缺口）
    const QVariantMap empty = app.qualityReport();
    QCOMPARE(empty.value(QStringLiteral("hasSchedule")).toBool(), false);
    QCOMPARE(empty.value(QStringLiteral("indicators")).toList().size(), 10);
    QCOMPARE(empty.value(QStringLiteral("grades")).toList().size(), 1);

    // 排一节课后：排上节数 1、老师撞课 0
    sched::PlanEntry e;
    e.classId = ids.class1;
    e.courseId = ids.yuwen;
    e.teacherIds = {ids.tZhang};
    e.weeklyPeriods = 1;
    app.school()->addPlanEntry(e);
    app.school()->schedule.setAssignment(ids.class1, 0, 0, ids.yuwen);

    // 批量添加：忽略空行与首尾空白，其余原样建行
    CourseListModel courses(&app);
    const int before = app.courseCount();
    QCOMPARE(courses.appendRows({QStringLiteral(" 语文 "), QString(),
                                 QStringLiteral("数学"), QStringLiteral("   "),
                                 QStringLiteral("英语")}), 3);
    QCOMPARE(app.courseCount(), before + 3);
    QCOMPARE(courses.get(courses.count() - 3, QStringLiteral("name")).toString(),
             QStringLiteral("语文"));
    QCOMPARE(courses.appendRows({}), 0);
    QCOMPARE(courses.appendRows({QStringLiteral("  ")}), 0);

    const QVariantMap rep = app.qualityReport();
    QCOMPARE(rep.value(QStringLiteral("hasSchedule")).toBool(), true);
    QCOMPARE(rep.value(QStringLiteral("hardViolations")).toInt(), 0);
    bool foundPlaced = false, foundClash = false;
    for (const auto &v : rep.value(QStringLiteral("indicators")).toList()) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("key")).toString() == QLatin1StringView("placedPeriods")) {
            foundPlaced = true;
            QCOMPARE(m.value(QStringLiteral("value")).toDouble(), 1.0);
            QCOMPARE(m.value(QStringLiteral("required")).toDouble(), 1.0);
        }
        if (m.value(QStringLiteral("key")).toString() == QLatin1StringView("teacherClash")) {
            foundClash = true;
            QCOMPARE(m.value(QStringLiteral("value")).toDouble(), 0.0);
        }
    }
    QVERIFY(foundPlaced && foundClash);
    QCOMPARE(rep.value(QStringLiteral("teachers")).toList().size(), 1);

    // HTML 报告：结构完整、含关键口径与打印样式
    const QString html = AppController::buildQualityReportHtml(*app.school());
    QVERIFY(html.startsWith(QStringLiteral("<!DOCTYPE html>")));
    QVERIFY(html.contains(QStringLiteral("M3b校")));
    QVERIFY(html.contains(QStringLiteral("十项指标")));
    QVERIFY(html.contains(QStringLiteral("教案连续性")));
    QVERIFY(html.contains(QStringLiteral("教师开窗")));
    QVERIFY(html.contains(QStringLiteral("@media print")));
    QVERIFY(html.contains(QStringLiteral("一年级"))); // 年级组分段
}

void TestApp::recentFilesOps()
{
    QSettings().setValue(QStringLiteral("recentFiles"), QStringList());
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    AppController app;
    QSignalSpy spy(&app, &AppController::recentFilesChanged);

    // 通过保存项目登记两条最近记录（最近的在最前）
    const QString pathA = dir.filePath(QStringLiteral("a.spk"));
    const QString pathB = dir.filePath(QStringLiteral("b.spk"));
    app.newProject(QStringLiteral("甲校"), 5, 7, 4, 20);
    QVERIFY(app.saveProjectAs(pathA));
    app.newProject(QStringLiteral("乙校"), 5, 7, 4, 20);
    QVERIFY(app.saveProjectAs(pathB));
    QCOMPARE(app.recentFiles().size(), 2);
    QCOMPARE(app.recentFiles().first(), pathB);

    // 移除一项：只去掉该项，其余保留；移除不存在的路径不发信号
    app.removeRecentFile(pathB);
    QCOMPARE(app.recentFiles(), QStringList({pathA}));
    const int before = spy.count();
    app.removeRecentFile(dir.filePath(QStringLiteral("missing.spk")));
    QCOMPARE(spy.count(), before);

    // 清空列表；已空时不再发信号
    app.clearRecentFiles();
    QVERIFY(app.recentFiles().isEmpty());
    const int afterClear = spy.count();
    app.clearRecentFiles();
    QCOMPARE(spy.count(), afterClear);
}

void TestApp::batchClassesWithAutoGroup()
{
    AppController app;
    app.newProject(QStringLiteral("批量建班校"), 5, 7, 4, 20);

    // 前缀建组：高一1/2班 → 高一年级；高二1班 → 高二年级；
    // 无数字（实验班）与数字开头（3班）不建组；空行忽略
    const QVariantMap res = app.addClassesWithAutoGroup(
        {QStringLiteral(" 高一1班 "), QStringLiteral("高一2班"), QStringLiteral("高二1班"),
         QStringLiteral("实验班"), QStringLiteral("3班"), QStringLiteral("   ")});
    QCOMPARE(res.value(QStringLiteral("created")).toInt(), 5);
    QCOMPARE(app.classCount(), 5);

    const auto *school = app.school();
    QSet<QString> names;
    for (const auto &g : school->groups)
        names.insert(g.name);
    QVERIFY(names.contains(QStringLiteral("高一年级")));
    QVERIFY(names.contains(QStringLiteral("高二年级")));
    QCOMPARE(school->groups.size(), 2);

    const sched::Group *g1 = nullptr;
    for (const auto &g : school->groups) {
        if (g.name == QStringLiteral("高一年级"))
            g1 = &g;
    }
    QVERIFY(g1);
    QCOMPARE(g1->memberIds.size(), 2);

    // 班级的 gradeId 已指向所在年级组（高二1班→高二年级；实验班/3班不建组）
    qint64 g2id = -1;
    for (const auto &g : school->groups) {
        if (g.name == QStringLiteral("高二年级"))
            g2id = g.id;
    }
    for (const auto &c : school->classes) {
        if (c.name.startsWith(QStringLiteral("高一")))
            QCOMPARE(c.gradeId, g1->id);
        else if (c.name.startsWith(QStringLiteral("高二")))
            QCOMPARE(c.gradeId, g2id);
        else
            QCOMPARE(c.gradeId, qint64(-1));
    }

    // 再次批量：同名组并入而不新建
    app.addClassesWithAutoGroup({QStringLiteral("高一3班")});
    QCOMPARE(app.school()->groups.size(), 2);
    for (const auto &g : app.school()->groups) {
        if (g.name == QStringLiteral("高一年级"))
            QCOMPARE(g.memberIds.size(), 3);
    }

    // 全空输入：不新增
    QCOMPARE(app.addClassesWithAutoGroup({QStringLiteral(" "), QString()})
                 .value(QStringLiteral("created"))
                 .toInt(),
             0);
    QCOMPARE(app.classCount(), 6);
}

void TestApp::batchOps()
{
    AppController app;
    const MiniSchool ids = makeMini(app);
    sched::Course zixi;
    zixi.name = QStringLiteral("自习");
    const qint64 course = app.school()->addCourse(zixi);
    sched::Course xinli;
    xinli.name = QStringLiteral("心理");
    const qint64 courseXinli = app.school()->addCourse(xinli);
    sched::Group g;
    g.type = sched::Group::Type::TeacherGroup;
    g.name = QStringLiteral("数学组");
    const qint64 gid = app.school()->addGroup(g);
    app.school()->findGroup(gid)->memberIds = {ids.tZhang};

    const auto addPlan = [&](qint64 cid) {
        sched::PlanEntry e;
        e.classId = cid;
        e.courseId = course;
        e.teacherIds = {ids.tZhang};
        e.weeklyPeriods = 5;
        app.school()->addPlanEntry(e);
    };
    addPlan(ids.class1);
    addPlan(ids.class2);
    for (qint64 cid : {ids.class1, ids.class2}) { // 心理：与自习共用格用
        sched::PlanEntry e;
        e.classId = cid;
        e.courseId = courseXinli;
        e.weeklyPeriods = 0.5;
        app.school()->addPlanEntry(e);
    }
    // 语文也在 1 班计划内（后面用它制造"该格已被别的课钉住"的冲突）
    sched::PlanEntry ywPlan;
    ywPlan.classId = ids.class1;
    ywPlan.courseId = ids.yuwen;
    ywPlan.teacherIds = {ids.tWang};
    ywPlan.weeklyPeriods = 5;
    app.school()->addPlanEntry(ywPlan);

    // 批量预排：2 班 × 5 格 = 10 条
    const QVariantMap pre = app.batchSetPreassignments(course, {ids.class1, ids.class2},
                                                       {0, 1, 2, 3, 4}, 0);
    QCOMPARE(pre.value(QStringLiteral("set")).toInt(), 10);
    QCOMPARE(pre.value(QStringLiteral("affected")).toInt(), 2);
    QCOMPARE(app.school()->preAssignments.size(), 10);

    // 冲突：先给 1 班第 5 格钉别的课，再批量钉自习 → 跳过 1 格
    app.togglePreAssignment(ids.class1, ids.yuwen, 5);
    const QVariantMap pre2 = app.batchSetPreassignments(course, {ids.class1}, {5}, 0);
    QCOMPARE(pre2.value(QStringLiteral("set")).toInt(), 0);
    QCOMPARE(pre2.value(QStringLiteral("skipped")).toInt(), 1);
    // 覆盖模式：替换掉原有预排
    const QVariantMap pre3 = app.batchSetPreassignments(course, {ids.class1}, {5}, 1);
    QCOMPARE(pre3.value(QStringLiteral("set")).toInt(), 1);
    QCOMPARE(app.preAssignments(ids.class1).size(), 6); // 5 格 + 覆盖后的第 5 格

    // 撤销：回到「10 条 + 1 条别的课」的状态
    QVERIFY(app.canUndoBatch());
    QVERIFY(app.undoLastBatch());
    QCOMPARE(app.school()->preAssignments.size(), 11);
    QVERIFY(!app.canUndoBatch());

    // 批量清除：清掉 2 班全部自习预排
    const QVariantMap clr = app.batchClearPreassignments(course, {ids.class2}, {0, 1, 2, 3, 4});
    QCOMPARE(clr.value(QStringLiteral("cleared")).toInt(), 5);
    // 撤销后恢复
    QVERIFY(app.undoLastBatch());
    QCOMPARE(app.school()->preAssignments.size(), 11);

    // 批量不排（老师）：3 格禁排 → 恢复
    const QVariantMap ns = app.batchSetNoSchedule(2, {ids.tZhang}, {0, 1, 2}, true);
    QCOMPARE(ns.value(QStringLiteral("set")).toInt(), 3);
    QCOMPARE(app.noScheduleSlots(2, ids.tZhang).size(), 3);
    const QVariantMap ns2 = app.batchSetNoSchedule(2, {ids.tZhang}, {1}, false);
    QCOMPARE(ns2.value(QStringLiteral("cleared")).toInt(), 1);
    QCOMPARE(app.noScheduleSlots(2, ids.tZhang).size(), 2);
    QVERIFY(app.undoLastBatch());
    QCOMPARE(app.noScheduleSlots(2, ids.tZhang).size(), 3);

    // 共用格：单周钉自习、双周钉心理 → 同一格两条并存（不再算冲突）
    const QVariantMap halfA = app.batchSetPreassignments(course, {ids.class2}, {20}, 0, 1);
    QCOMPARE(halfA.value(QStringLiteral("set")).toInt(), 1);
    const QVariantMap halfB = app.batchSetPreassignments(courseXinli, {ids.class2}, {20}, 0, 2);
    QCOMPARE(halfB.value(QStringLiteral("set")).toInt(), 1);
    int atSlot20 = 0;
    for (const auto &p : app.school()->preAssignments) {
        if (p.classId == ids.class2 && p.slotIndex == 20)
            ++atSlot20;
    }
    QCOMPARE(atSlot20, 2);
    // 整节与半周互斥：跳过模式下该格已有半周预排 → 整节预排被跳过
    const QVariantMap whole = app.batchSetPreassignments(course, {ids.class2}, {20}, 0, 0);
    QCOMPARE(whole.value(QStringLiteral("set")).toInt(), 0);
    QCOMPARE(whole.value(QStringLiteral("skipped")).toInt(), 1);
    // 覆盖模式下整节替换掉两条半周
    const QVariantMap wholeForce = app.batchSetPreassignments(course, {ids.class2}, {20}, 1, 0);
    QCOMPARE(wholeForce.value(QStringLiteral("set")).toInt(), 1);
    int atSlot20After = 0;
    for (const auto &p : app.school()->preAssignments) {
        if (p.classId == ids.class2 && p.slotIndex == 20)
            ++atSlot20After;
    }
    QCOMPARE(atSlot20After, 1);
    QVERIFY(app.undoLastBatch());

    // 组成员（按教研组批量勾选的数据来源）
    QCOMPARE(app.groupMemberIds(gid), QVariantList{ids.tZhang});
    QVERIFY(app.groupMemberIds(-1).isEmpty());

    // 批量改课时：命中 2 条；再改一个没有该课的班级 → missing 1
    const QVariantMap wp = app.batchSetWeeklyPeriods(course, {ids.class1, ids.class2}, 3);
    QCOMPARE(wp.value(QStringLiteral("updated")).toInt(), 2);
    for (const auto &e : app.school()->planEntries) {
        if (e.courseId == course)
            QCOMPARE(e.weeklyPeriods, 3.0);
    }
    const QVariantMap wp2 = app.batchSetWeeklyPeriods(course, {ids.class1, ids.class2}, 5);
    QCOMPARE(wp2.value(QStringLiteral("updated")).toInt(), 2);
    QVERIFY(app.undoLastBatch());
    for (const auto &e : app.school()->planEntries) {
        if (e.courseId == course)
            QCOMPARE(e.weeklyPeriods, 3.0);
    }

    // 无变化时不占用撤销位
    const QVariantMap wp3 = app.batchSetWeeklyPeriods(course, {ids.class1, ids.class2}, 3);
    QCOMPARE(wp3.value(QStringLiteral("updated")).toInt(), 0);
    QVERIFY(!app.canUndoBatch());
}


void TestApp::aiToolLayer()
{
    AppController app;
    const MiniSchool ids = makeMini(app);
    sched::Course zixi;
    zixi.name = QStringLiteral("自习");
    const qint64 course = app.school()->addCourse(zixi);
    for (qint64 cid : {ids.class1, ids.class2}) {
        sched::PlanEntry e;
        e.classId = cid;
        e.courseId = course;
        e.weeklyPeriods = 5;
        app.school()->addPlanEntry(e);
    }

    // 工具清单：只读 + 变更都在
    const QVariantList tools = app.aiTools();
    QVERIFY(tools.size() >= 12);
    QSet<QString> names;
    for (const auto &v : tools)
        names.insert(v.toMap().value(QStringLiteral("name")).toString());
    QVERIFY(names.contains(QStringLiteral("batch_preassign")));
    QVERIFY(names.contains(QStringLiteral("query_schedule")));
    QVERIFY(names.contains(QStringLiteral("quality_report")));
    // 全部扩展工具都必须暴露给模型，少一个模型就会说「没这个功能」
    for (const char *t : {"set_plan_chained", "set_plan_early_periods", "set_plan_teachers",
                          "set_teacher_daily_max", "batch_no_schedule_by_group",
                          "query_teacher_schedule", "create_time_sync_group",
                          "remove_time_sync_group", "list_time_sync_groups",
                          "set_plan_no_load", "create_combined_group",
                          "remove_combined_group", "list_combined_groups"}) {
        QVERIFY2(names.contains(QString::fromLatin1(t)), t);
    }
    // 每个工具都要有合法的 JSON schema 形态，否则上游会 400
    for (const auto &v : tools) {
        const QVariantMap m = v.toMap();
        QVERIFY(m.contains(QStringLiteral("name")));
        QVERIFY(m.contains(QStringLiteral("description")));
        QCOMPARE(m.value(QStringLiteral("parameters")).toMap().value(QStringLiteral("type")).toString(),
                 QStringLiteral("object"));
    }

    // 只读工具：列班级
    const QVariantMap classes = app.aiRunReadTool(QStringLiteral("list_classes"), {});
    QCOMPARE(classes.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(classes.value(QStringLiteral("data")).toList().size(), 2);
    // 只读工具：不存在的班级给出结构化错误
    const QVariantMap bad = app.aiRunReadTool(
        QStringLiteral("query_schedule"), {{QStringLiteral("className"), QStringLiteral("不存在班")}});
    QCOMPARE(bad.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(!bad.value(QStringLiteral("error")).toString().isEmpty());

    // 干跑：2 班 × 5 天第 7 节 = 10 条新增
    const QVariantMap args{{QStringLiteral("courseName"), QStringLiteral("自习")},
                           {QStringLiteral("classNames"),
                            QVariantList{QStringLiteral("1班"), QStringLiteral("2班")}},
                           {QStringLiteral("days"), QVariantList{1, 2, 3, 4, 5}},
                           {QStringLiteral("period"), 7}};
    const QVariantMap dry = app.aiDescribe(QStringLiteral("batch_preassign"), args);
    QCOMPARE(dry.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(dry.value(QStringLiteral("changes")).toInt(), 10);
    QVERIFY(!dry.value(QStringLiteral("summary")).toString().isEmpty());
    // 干跑不落盘
    QCOMPARE(app.school()->preAssignments.size(), 0);

    // 应用 + 一次撤销
    const QVariantMap applied = app.applyAiOperations(
        {QVariantMap{{QStringLiteral("tool"), QStringLiteral("batch_preassign")},
                     {QStringLiteral("args"), args}}});
    QCOMPARE(applied.value(QStringLiteral("applied")).toInt(), 1);
    QCOMPARE(applied.value(QStringLiteral("changed")).toBool(), true);
    QCOMPARE(app.school()->preAssignments.size(), 10);
    QVERIFY(app.canUndoBatch());
    QVERIFY(app.undoLastBatch());
    QCOMPARE(app.school()->preAssignments.size(), 0);

    // 调课：干跑 + 应用（目标格必须为空）
    app.school()->schedule.setAssignment(ids.class1, 0, 0, ids.yuwen);
    const QVariantMap moveArgs{{QStringLiteral("className"), QStringLiteral("1班")},
                               {QStringLiteral("fromDay"), 1},
                               {QStringLiteral("fromPeriod"), 1},
                               {QStringLiteral("toDay"), 1},
                               {QStringLiteral("toPeriod"), 2}};
    const QVariantMap moveDry = app.aiDescribe(QStringLiteral("move_cell"), moveArgs);
    QCOMPARE(moveDry.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(moveDry.value(QStringLiteral("changes")).toInt(), 1);
    const QVariantMap moveRun = app.applyAiOperations(
        {QVariantMap{{QStringLiteral("tool"), QStringLiteral("move_cell")},
                     {QStringLiteral("args"), moveArgs}}});
    QCOMPARE(moveRun.value(QStringLiteral("changed")).toBool(), true);
    QCOMPARE(app.school()->schedule.courseAt(ids.class1, 1, 0).value_or(-1), ids.yuwen);
    QVERIFY(!app.school()->schedule.courseAt(ids.class1, 0, 0).has_value());
    QVERIFY(app.undoLastBatch());
    QCOMPARE(app.school()->schedule.courseAt(ids.class1, 0, 0).value_or(-1), ids.yuwen);

    // 目标格已占用 → 干跑就报错，不产生待办
    app.school()->schedule.setAssignment(ids.class1, 1, 0, ids.shuxue);
    const QVariantMap clash = app.aiDescribe(QStringLiteral("move_cell"), moveArgs);
    QCOMPARE(clash.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(!clash.value(QStringLiteral("error")).toString().isEmpty());
}


void TestApp::aiToolsExtended()
{
    AppController app;
    const MiniSchool ids = makeMini(app);
    sched::Course zixi;
    zixi.name = QStringLiteral("自习");
    const qint64 course = app.school()->addCourse(zixi);
    for (qint64 cid : {ids.class1, ids.class2}) {
        sched::PlanEntry e;
        e.classId = cid;
        e.courseId = course;
        e.weeklyPeriods = 6;
        e.teacherIds = {ids.tZhang};
        app.school()->addPlanEntry(e);
    }
    sched::Group tg;
    tg.type = sched::Group::Type::TeacherGroup;
    tg.name = QStringLiteral("数学组");
    const qint64 gid = app.school()->addGroup(tg);
    app.school()->findGroup(gid)->memberIds = {ids.tZhang, ids.tWang};

    const auto apply1 = [&](const QString &tool, const QVariantMap &args) {
        const QVariantMap dry = app.aiDescribe(tool, args);
        if (!dry.value(QStringLiteral("ok")).toBool())
            return QVariantMap{{QStringLiteral("dryError"), dry.value(QStringLiteral("error"))}};
        return app.applyAiOperations(
            {QVariantMap{{QStringLiteral("tool"), tool}, {QStringLiteral("args"), args}}});
    };

    // 1) 连堂：2 班每周 2 次 × 每次 2 节（自习在计划里）
    const QVariantMap chained2 = apply1(
        QStringLiteral("set_plan_chained"),
        {{QStringLiteral("courseName"), QStringLiteral("自习")},
         {QStringLiteral("classNames"), QVariantList{QStringLiteral("1班"), QStringLiteral("2班")}},
         {QStringLiteral("times"), 2},
         {QStringLiteral("length"), 2}});
    QCOMPARE(chained2.value(QStringLiteral("changed")).toBool(), true);
    for (const auto &e : app.school()->planEntries) {
        if (e.courseId == course) {
            QCOMPARE(e.chainedCount, 2);
            QCOMPARE(e.chainLength, 2);
        }
    }
    QVERIFY(app.undoLastBatch());
    for (const auto &e : app.school()->planEntries) {
        if (e.courseId == course)
            QCOMPARE(e.chainedCount, 0);
    }

    // 2) 前 N 节
    const QVariantMap early = apply1(
        QStringLiteral("set_plan_early_periods"),
        {{QStringLiteral("courseName"), QStringLiteral("自习")},
         {QStringLiteral("classNames"), QVariantList{QStringLiteral("1班"), QStringLiteral("2班")}},
         {QStringLiteral("earlyPeriods"), 3}});
    QCOMPARE(early.value(QStringLiteral("changed")).toBool(), true);
    for (const auto &e : app.school()->planEntries) {
        if (e.courseId == course)
            QCOMPARE(e.earlyPeriods, 3);
    }

    // 3) 任课老师按班轮流：1班→张，2班→王
    const QVariantMap teachers = apply1(
        QStringLiteral("set_plan_teachers"),
        {{QStringLiteral("courseName"), QStringLiteral("自习")},
         {QStringLiteral("classNames"), QVariantList{QStringLiteral("1班"), QStringLiteral("2班")}},
         {QStringLiteral("teacherNames"), QVariantList{QStringLiteral("张"), QStringLiteral("王")}},
         {QStringLiteral("rotate"), true}});
    QCOMPARE(teachers.value(QStringLiteral("changed")).toBool(), true);
    for (const auto &e : app.school()->planEntries) {
        if (e.courseId != course)
            continue;
        const qint64 expect = e.classId == ids.class1 ? ids.tZhang : ids.tWang;
        QCOMPARE(e.teacherIds.size(), 1);
        QCOMPARE(e.teacherIds.first(), expect);
    }

    // 4) 老师日最多课（软约束字段）
    const QVariantMap daily = apply1(
        QStringLiteral("set_teacher_daily_max"),
        {{QStringLiteral("teacherNames"), QVariantList{QStringLiteral("张")}},
         {QStringLiteral("maxPerDay"), 4}});
    QCOMPARE(daily.value(QStringLiteral("changed")).toBool(), true);
    QCOMPARE(app.school()->findTeacher(ids.tZhang)->maxPeriodsPerDay, 4);

    // 5) 按组批量不排（教研组的老师 × 周一第1、2节）
    const QVariantMap byGroup = apply1(
        QStringLiteral("batch_no_schedule_by_group"),
        {{QStringLiteral("groupName"), QStringLiteral("数学组")},
         {QStringLiteral("days"), QVariantList{1}},
         {QStringLiteral("periods"), QVariantList{1, 2}}});
    QCOMPARE(byGroup.value(QStringLiteral("changed")).toBool(), true);
    QCOMPARE(app.noScheduleSlots(2, ids.tZhang).size(), 2);
    QCOMPARE(app.noScheduleSlots(2, ids.tWang).size(), 2);

    // 6) 只读：老师课表
    app.school()->schedule.setAssignment(ids.class1, 0, 0, course);
    const QVariantMap ts = app.aiRunReadTool(
        QStringLiteral("query_teacher_schedule"), {{QStringLiteral("teacherName"), QStringLiteral("张")}});
    QCOMPARE(ts.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(ts.value(QStringLiteral("data")).toMap().value(QStringLiteral("任课")).toList().size() >= 1);

    // 7) 撤销这一串（单级：回到本用例最开始的状态）
    QVERIFY(app.undoLastBatch());
}

void TestApp::aiTimeSyncTools()
{
    AppController app;
    const MiniSchool ids = makeMini(app);
    for (qint64 cid : {ids.class1, ids.class2}) {
        sched::PlanEntry e;
        e.classId = cid;
        e.courseId = ids.yuwen;
        e.weeklyPeriods = 1;
        app.school()->addPlanEntry(e);
    }
    sched::Course sx;
    sx.name = QStringLiteral("数学");
    const qint64 math = app.school()->addCourse(sx);
    sched::PlanEntry e2;
    e2.classId = ids.class2;
    e2.courseId = math;
    e2.weeklyPeriods = 1;
    app.school()->addPlanEntry(e2);

    // 干跑：建同组前必须至少两个有效成员
    const QVariantMap bad = app.aiDescribe(
        QStringLiteral("create_time_sync_group"),
        {{QStringLiteral("name"), QStringLiteral("走班1")},
         {QStringLiteral("entries"), QVariantList{QVariantMap{{QStringLiteral("className"), QStringLiteral("1班")},
                                                              {QStringLiteral("courseName"), QStringLiteral("语文")}}}}});
    QCOMPARE(bad.value(QStringLiteral("ok")).toBool(), false);

    // 正常建组：1班语文 = 2班数学 同节
    const QVariantList entries{QVariantMap{{QStringLiteral("className"), QStringLiteral("1班")},
                                           {QStringLiteral("courseName"), QStringLiteral("语文")}},
                               QVariantMap{{QStringLiteral("className"), QStringLiteral("2班")},
                                           {QStringLiteral("courseName"), QStringLiteral("数学")}}};
    const QVariantMap dry = app.aiDescribe(
        QStringLiteral("create_time_sync_group"),
        {{QStringLiteral("name"), QStringLiteral("走班1")}, {QStringLiteral("entries"), entries}});
    QCOMPARE(dry.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(dry.value(QStringLiteral("changes")).toInt(), 2);

    const QVariantMap applied = app.applyAiOperations(
        {QVariantMap{{QStringLiteral("tool"), QStringLiteral("create_time_sync_group")},
                     {QStringLiteral("args"),
                      QVariantMap{{QStringLiteral("name"), QStringLiteral("走班1")},
                                  {QStringLiteral("entries"), entries}}}}});
    QCOMPARE(applied.value(QStringLiteral("changed")).toBool(), true);
    QCOMPARE(app.school()->timeSyncGroups.size(), 1);
    QCOMPARE(app.school()->timeSyncGroups.first().entries.size(), 2);

    // 只读：列同时段组
    const QVariantMap listed = app.aiRunReadTool(QStringLiteral("list_time_sync_groups"), {});
    QCOMPARE(listed.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(listed.value(QStringLiteral("data")).toList().size(), 1);

    // 撤销 → 回到没有组的状态
    QVERIFY(app.undoLastBatch());
    QCOMPARE(app.school()->timeSyncGroups.size(), 0);

    // 删除工具：先建后删
    app.createTimeSyncGroup(QStringLiteral("走班2"), entries);
    QCOMPARE(app.school()->timeSyncGroups.size(), 1);
    const QVariantMap removed = app.applyAiOperations(
        {QVariantMap{{QStringLiteral("tool"), QStringLiteral("remove_time_sync_group")},
                     {QStringLiteral("args"),
                      QVariantMap{{QStringLiteral("name"), QStringLiteral("走班2")}}}}});
    QCOMPARE(removed.value(QStringLiteral("changed")).toBool(), true);
    QCOMPARE(app.school()->timeSyncGroups.size(), 0);
}

void TestApp::aiCombinedGroupTools()
{
    AppController app;
    const MiniSchool ids = makeMini(app);

    const auto addPlan = [&](qint64 classId, qint64 teacherId) {
        sched::PlanEntry e;
        e.classId = classId;
        e.courseId = ids.yuwen;
        e.weeklyPeriods = 2;
        if (teacherId >= 0)
            e.teacherIds = {teacherId};
        return app.school()->addPlanEntry(e);
    };
    addPlan(ids.class1, ids.tZhang);
    const qint64 e2 = addPlan(ids.class2, ids.tWang);

    // 合班 = 同一个班上同一门课：课程不一致必须拒绝
    const QVariantMap mixed = app.aiDescribe(
        QStringLiteral("create_combined_group"),
        {{QStringLiteral("name"), QStringLiteral("错组合")},
         {QStringLiteral("entries"),
          QVariantList{QVariantMap{{QStringLiteral("className"), QStringLiteral("1班")},
                                   {QStringLiteral("courseName"), QStringLiteral("语文")}},
                       QVariantMap{{QStringLiteral("className"), QStringLiteral("2班")},
                                   {QStringLiteral("courseName"), QStringLiteral("数学")}}}}});
    QCOMPARE(mixed.value(QStringLiteral("ok")).toBool(), false);

    const QVariantList entries{QVariantMap{{QStringLiteral("className"), QStringLiteral("1班")},
                                           {QStringLiteral("courseName"), QStringLiteral("语文")}},
                               QVariantMap{{QStringLiteral("className"), QStringLiteral("2班")},
                                           {QStringLiteral("courseName"), QStringLiteral("语文")}}};

    // 与界面同一套校验：1班张、2班王没有共同任课老师 → 拒绝
    const QVariantMap noShare = app.aiDescribe(
        QStringLiteral("create_combined_group"),
        {{QStringLiteral("name"), QStringLiteral("合班")}, {QStringLiteral("entries"), entries}});
    QCOMPARE(noShare.value(QStringLiteral("ok")).toBool(), false);

    // 改成同一位老师（张）后可以建组
    app.school()->findPlanEntry(e2)->teacherIds = {ids.tZhang};
    const QVariantMap dry = app.aiDescribe(
        QStringLiteral("create_combined_group"),
        {{QStringLiteral("name"), QStringLiteral("合班")}, {QStringLiteral("entries"), entries}});
    QCOMPARE(dry.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(dry.value(QStringLiteral("changes")).toInt(), 2);

    const QVariantMap applied = app.applyAiOperations(
        {QVariantMap{{QStringLiteral("tool"), QStringLiteral("create_combined_group")},
                     {QStringLiteral("args"),
                      QVariantMap{{QStringLiteral("name"), QStringLiteral("合班")},
                                  {QStringLiteral("entries"), entries}}}}});
    QCOMPARE(applied.value(QStringLiteral("changed")).toBool(), true);
    QCOMPARE(app.school()->combinedGroups.size(), 1);
    QCOMPARE(app.school()->combinedGroups.first().entries.size(), 2);

    // 只读：列合班组
    const QVariantMap listed = app.aiRunReadTool(QStringLiteral("list_combined_groups"), {});
    QCOMPARE(listed.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(listed.value(QStringLiteral("data")).toList().size(), 1);

    // 撤销覆盖合班组
    QVERIFY(app.undoLastBatch());
    QCOMPARE(app.school()->combinedGroups.size(), 0);

    // 解散工具：先建后删
    app.createCombinedGroup(QStringLiteral("合班"), entries);
    QCOMPARE(app.school()->combinedGroups.size(), 1);
    const QVariantMap removed = app.applyAiOperations(
        {QVariantMap{{QStringLiteral("tool"), QStringLiteral("remove_combined_group")},
                     {QStringLiteral("args"), QVariantMap{{QStringLiteral("name"), QStringLiteral("合班")}}}}});
    QCOMPARE(removed.value(QStringLiteral("changed")).toBool(), true);
    QCOMPARE(app.school()->combinedGroups.size(), 0);
}

void TestApp::aiNoLoadTool()
{
    AppController app;
    const MiniSchool ids = makeMini(app);
    sched::PlanEntry e;
    e.classId = ids.class1;
    e.courseId = ids.yuwen;
    e.weeklyPeriods = 1;
    e.teacherIds = {ids.tZhang};
    app.school()->addPlanEntry(e);
    sched::PlanEntry e2 = e;
    e2.classId = ids.class2;
    app.school()->addPlanEntry(e2);

    // 干跑：两个班都改成「不计课时、不挂老师」
    const QVariantMap dry = app.aiDescribe(
        QStringLiteral("set_plan_no_load"),
        {{QStringLiteral("courseName"), QStringLiteral("语文")},
         {QStringLiteral("classNames"), QVariantList{QStringLiteral("1班"), QStringLiteral("2班")}}});
    QCOMPARE(dry.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(dry.value(QStringLiteral("changes")).toInt(), 2);

    const QVariantMap applied = app.applyAiOperations(
        {QVariantMap{{QStringLiteral("tool"), QStringLiteral("set_plan_no_load")},
                     {QStringLiteral("args"),
                      QVariantMap{{QStringLiteral("courseName"), QStringLiteral("语文")},
                                  {QStringLiteral("classNames"),
                                   QVariantList{QStringLiteral("1班"), QStringLiteral("2班")}}}}}});
    QCOMPARE(applied.value(QStringLiteral("changed")).toBool(), true);
    for (const auto &pe : app.school()->planEntries)
        QCOMPARE(pe.noLoad, true);
    // list_teachers 的周节数不再计它
    const QVariantList teachers = app.aiRunReadTool(QStringLiteral("list_teachers"), {})
                                      .value(QStringLiteral("data"))
                                      .toList();
    for (const QVariant &tv : teachers) {
        if (tv.toMap().value(QStringLiteral("name")).toString() == QStringLiteral("张"))
            QCOMPARE(tv.toMap().value(QStringLiteral("周节数")).toInt(), 0);
    }

    // 老师个人课表不显示这一格（发布给老师之前要摘掉）
    sched::ScheduleAssignment a;
    a.classId = ids.class1;
    a.courseId = ids.yuwen;
    a.slotIndex = 0;
    a.half = 0;
    app.school()->schedule.assignments.append(a);
    QVERIFY(app.teacherRow(ids.tZhang).at(0).toList().isEmpty());

    // 恢复
    const QVariantMap back = app.applyAiOperations(
        {QVariantMap{{QStringLiteral("tool"), QStringLiteral("set_plan_no_load")},
                     {QStringLiteral("args"),
                      QVariantMap{{QStringLiteral("courseName"), QStringLiteral("语文")},
                                  {QStringLiteral("classNames"), QVariantList{QStringLiteral("1班")}},
                                  {QStringLiteral("noLoad"), false}}}}});
    QCOMPARE(back.value(QStringLiteral("changed")).toBool(), true);
    QCOMPARE(app.school()->planEntries[0].noLoad, false);
    QCOMPARE(app.school()->planEntries[1].noLoad, true);
}

void TestApp::schedulerBridge()
{
    AppController app;
    const MiniSchool ids = makeMini(app);

    // precheck：无计划 → 有 err
    bool hasErr = false;
    for (const auto &item : app.precheck()) {
        if (item.toMap().value(QStringLiteral("level")).toString() == QLatin1StringView("err"))
            hasErr = true;
    }
    QVERIFY(hasErr);

    // 造计划：张教 1/2 班语文各 5 节，王教 1/2 班数学各 5 节
    auto *school = app.school();
    for (qint64 cls : {ids.class1, ids.class2}) {
        sched::PlanEntry e1;
        e1.classId = cls; e1.courseId = ids.yuwen; e1.teacherIds = {ids.tZhang}; e1.weeklyPeriods = 5;
        school->addPlanEntry(e1);
        sched::PlanEntry e2;
        e2.classId = cls; e2.courseId = ids.shuxue; e2.teacherIds = {ids.tWang}; e2.weeklyPeriods = 5;
        school->addPlanEntry(e2);
    }

    // precheck 通过（含 ok 级条目）
    bool hasOk = false;
    for (const auto &item : app.precheck()) {
        if (item.toMap().value(QStringLiteral("level")).toString() == QLatin1StringView("ok"))
            hasOk = true;
    }
    QVERIFY(hasOk);

    // 运行引擎（固定 seed，随机性可控）
    QSignalSpy spy(&app, &AppController::schedulerFinished);
    app.runScheduler({{QStringLiteral("runs"), 2},
                      {QStringLiteral("maxIterations"), 5000},
                      {QStringLiteral("seed"), 99},
                      {QStringLiteral("randomSeed"), false},
                      {QStringLiteral("placeAll"), true}});
    QVERIFY(app.schedulerRunning());
    QVERIFY2(spy.wait(30000), "排课未在 30s 内完成");
    QVERIFY(app.resultPending());
    QCOMPARE(app.schedulerReport().value(QStringLiteral("hardViolations")).toInt(), 0);
    QVERIFY(!app.schedulerRunning());

    // 应用结果：写入 School 并置脏
    QVERIFY(!app.hasSchedule());
    app.applyResult();
    QVERIFY(app.hasSchedule());
    QVERIFY(!app.resultPending());
    QVERIFY(app.projectDirty());
    QCOMPARE(app.assignmentsCount(), 20); // 2 班 × 10 节

    // 班级视图数据：1班 35 格中 10 格有课
    const QVariantList row = app.classRow(ids.class1);
    QCOMPARE(row.size(), 35);
    int filled = 0;
    for (const auto &c : row) {
        if (!c.toMap().isEmpty())
            filled++;
    }
    QCOMPARE(filled, 10);

    // 老师视图：张老师 10 格；总课表 2 行
    int teacherCells = 0;
    for (const auto &c : app.teacherRow(ids.tZhang))
        teacherCells += c.toList().size();
    QCOMPARE(teacherCells, 10);
    QCOMPARE(app.masterGrid().size(), 2);

    // 再跑一次后放弃：课表保持上一次应用的结果
    QSignalSpy spy2(&app, &AppController::schedulerFinished);
    app.runScheduler({{QStringLiteral("runs"), 1},
                      {QStringLiteral("maxIterations"), 1000},
                      {QStringLiteral("randomSeed"), true},
                      {QStringLiteral("placeAll"), true}});
    QVERIFY2(spy2.wait(30000), "第二次排课未完成");
    app.discardResult();
    QVERIFY(!app.resultPending());
    QCOMPARE(app.assignmentsCount(), 20);
}

// ---- 调课测试校例 ----
struct AdjustIds {
    qint64 class1, class2;
    qint64 yuwen, shuxue, wuli, huaxue, shengwu, dili, tiyu;
    qint64 tZhang, tWang, tLi, tChen, tZhao;
    qint64 room;
};

static AdjustIds makeAdjustSchool(AppController &app)
{
    AdjustIds ids{};
    app.newProject(QStringLiteral("调课校"), 5, 7, 4, 20);
    auto *s = app.school();

    sched::ClassInfo c1; c1.name = QStringLiteral("1班");
    sched::ClassInfo c2; c2.name = QStringLiteral("2班");
    ids.class1 = s->addClass(c1);
    ids.class2 = s->addClass(c2);

    auto addCourse = [&](const QString &name) {
        sched::Course c; c.name = name; return s->addCourse(c);
    };
    ids.yuwen = addCourse(QStringLiteral("语文"));
    ids.shuxue = addCourse(QStringLiteral("数学"));
    ids.wuli = addCourse(QStringLiteral("物理"));
    ids.huaxue = addCourse(QStringLiteral("化学"));
    ids.shengwu = addCourse(QStringLiteral("生物"));
    ids.dili = addCourse(QStringLiteral("地理"));
    ids.tiyu = addCourse(QStringLiteral("体育"));

    sched::Teacher tz; tz.name = QStringLiteral("张");
    sched::Teacher tw; tw.name = QStringLiteral("王");
    sched::Teacher tl; tl.name = QStringLiteral("李");
    sched::Teacher tc; tc.name = QStringLiteral("陈");
    sched::Teacher tzh; tzh.name = QStringLiteral("赵");
    ids.tZhang = s->addTeacher(tz);
    ids.tWang = s->addTeacher(tw);
    ids.tLi = s->addTeacher(tl);
    ids.tChen = s->addTeacher(tc);
    ids.tZhao = s->addTeacher(tzh);

    sched::Classroom lab; lab.name = QStringLiteral("实验室"); lab.capacity = 1;
    ids.room = s->addClassroom(lab);

    auto plan = [&](qint64 cls, qint64 course, qint64 teacher, double periods, qint64 room = -1,
                    bool combined = false) {
        sched::PlanEntry e;
        e.classId = cls; e.courseId = course; e.teacherIds = {teacher};
        e.weeklyPeriods = periods; e.classroomId = room; e.isCombined = combined;
        return s->addPlanEntry(e);
    };
    plan(ids.class1, ids.yuwen, ids.tZhang, 5);
    plan(ids.class2, ids.yuwen, ids.tZhang, 5);
    plan(ids.class1, ids.shuxue, ids.tWang, 5);
    plan(ids.class1, ids.wuli, ids.tLi, 2, ids.room);
    plan(ids.class2, ids.wuli, ids.tZhao, 2, ids.room); // 2班物理换赵老师，隔离容量校验
    plan(ids.class1, ids.huaxue, ids.tWang, 2.5);
    plan(ids.class1, ids.shengwu, ids.tWang, 2);
    plan(ids.class1, ids.dili, ids.tWang, 2);
    plan(ids.class1, ids.tiyu, ids.tChen, 2, -1, true);
    plan(ids.class2, ids.tiyu, ids.tChen, 2, -1, true);

    sched::CombinedGroup cg;
    cg.name = QStringLiteral("体育合班");
    cg.entries = {{ids.class1, ids.tiyu}, {ids.class2, ids.tiyu}};
    s->addCombinedGroup(cg);

    // 不排：1班 slot34
    sched::NoScheduleConstraint ns;
    ns.entityType = sched::NoScheduleConstraint::EntityType::Class;
    ns.entityId = ids.class1;
    ns.slots = {34};
    s->addNoScheduleConstraint(ns);

    // 预排：1班语文 slot10
    sched::PreAssignment pre;
    pre.classId = ids.class1; pre.courseId = ids.yuwen; pre.slotIndex = 10;
    s->addPreAssignment(pre);

    // 初始课表
    auto put = [&](qint64 cls, int slot, int half, qint64 course) {
        s->schedule.setAssignment(cls, slot, half, course);
    };
    put(ids.class1, 10, 0, ids.yuwen);  // 预排
    put(ids.class1, 0, 0, ids.shuxue);
    put(ids.class2, 0, 0, ids.yuwen);   // 张 slot0
    put(ids.class1, 5, 0, ids.wuli);    // 李 + 实验室 slot5
    put(ids.class2, 6, 0, ids.wuli);    // 李 + 实验室 slot6
    put(ids.class1, 15, 1, ids.huaxue); // 化学 单周
    put(ids.class1, 17, 0, ids.shengwu);
    put(ids.class1, 18, 1, ids.dili);   // 地理 单周
    put(ids.class1, 20, 0, ids.tiyu);   // 体育合班
    put(ids.class2, 20, 0, ids.tiyu);
    return ids;
}

void TestApp::adjustRulesMatrix()
{
    AppController app;
    const AdjustIds ids = makeAdjustSchool(app);
    AdjustController adjust(&app);
    auto ok = [](const QVariantMap &r) { return r.value(QStringLiteral("ok")).toBool(); };
    auto reason = [](const QVariantMap &r) { return r.value(QStringLiteral("reason")).toString(); };

    // 1. 合法移动：2班语文 slot0 → slot1
    QVERIFY(ok(adjust.moveCell(ids.class2, 0, 0, 1, 0)));
    QCOMPARE(app.school()->schedule.courseAt(ids.class2, 1, 0).value_or(-1), ids.yuwen);

    // 2. 老师冲突：2班语文（张）移到 slot10（张在 1班上语文）→ 拒
    const QVariantMap r2 = adjust.moveCell(ids.class2, 1, 0, 10, 0);
    QVERIFY(!ok(r2));
    QVERIFY(reason(r2).contains(QStringLiteral("张")));

    // 3. 教室容量：2班物理 slot6 → slot5（实验室容量 1 已被 1班占）→ 拒
    const QVariantMap r3 = adjust.moveCell(ids.class2, 6, 0, 5, 0);
    QVERIFY(!ok(r3));
    QVERIFY(reason(r3).contains(QStringLiteral("容量")));

    // 4. 不排时间：1班数学 slot0 → slot34（班级不排）→ 拒
    QVERIFY(!ok(adjust.moveCell(ids.class1, 0, 0, 34, 0)));

    // 5. 预排锁定：移走预排课 → 拒；别的课移到预排格 → 拒
    QVERIFY(!ok(adjust.moveCell(ids.class1, 10, 0, 11, 0)));
    QVERIFY(!ok(adjust.moveCell(ids.class1, 0, 0, 10, 0)));

    // 6. half 匹配：整节 → 含单周格的格子 拒；单周 → 空格 可；单周 ↔ 整节格 拒；单周 ↔ 单周 可对调
    QVERIFY(!ok(adjust.moveCell(ids.class1, 0, 0, 15, 0)));   // 整节数学 → 化学(单)格
    QVERIFY(ok(adjust.moveCell(ids.class1, 15, 1, 16, 1)));   // 化学(单) → 空格
    QVERIFY(!ok(adjust.moveCell(ids.class1, 16, 1, 17, 1)));  // 化学(单) → 生物(整)格
    QVERIFY(ok(adjust.moveCell(ids.class1, 16, 1, 18, 1)));   // 化学(单) ↔ 地理(单) 对调
    QCOMPARE(app.school()->schedule.courseAt(ids.class1, 18, 1).value_or(-1), ids.huaxue);
    QCOMPARE(app.school()->schedule.courseAt(ids.class1, 16, 1).value_or(-1), ids.dili);

    // 7. 合班联动：1班体育 slot20 → slot21，两班一起动
    QVERIFY(ok(adjust.moveCell(ids.class1, 20, 0, 21, 0)));
    QCOMPARE(app.school()->schedule.courseAt(ids.class1, 21, 0).value_or(-1), ids.tiyu);
    QCOMPARE(app.school()->schedule.courseAt(ids.class2, 21, 0).value_or(-1), ids.tiyu);
    QVERIFY(!app.school()->schedule.courseAt(ids.class1, 20, 0).has_value());
    QVERIFY(!app.school()->schedule.courseAt(ids.class2, 20, 0).has_value());
    // 合班课对调 → 拒
    QVERIFY(!ok(adjust.moveCell(ids.class1, 21, 0, 0, 0)));

    // 8. targetsFor：对 1班数学(slot0) 计算目标表
    // 注意此时 化学已与地理对调：slot16=地理(单周)、slot18=化学(单周)、slot15 已空
    const QVariantList targets = adjust.targetsFor(ids.class1, 0, 0);
    QCOMPARE(targets.size(), 35);
    QCOMPARE(targets[10].toInt(), 0);  // 预排格禁
    QCOMPARE(targets[34].toInt(), 0);  // 不排格禁
    QCOMPARE(targets[16].toInt(), 0);  // 单周格与整节不兼容
    QCOMPARE(targets[5].toInt(), 2);   // 物理格可对调（王/李无冲突）
    QCOMPARE(targets[1].toInt(), 1);   // 空格可落子
}

void TestApp::adjustUndoRedo()
{
    AppController app;
    const AdjustIds ids = makeAdjustSchool(app);
    AdjustController adjust(&app);
    auto assigns = [&] { return app.school()->schedule.assignments; };

    const auto before = assigns();
    QVERIFY(adjust.moveCell(ids.class1, 0, 0, 1, 0).value(QStringLiteral("ok")).toBool());
    QVERIFY(assigns() != before);
    QVERIFY(adjust.canUndo());
    QCOMPARE(adjust.undoText(), QStringLiteral("移动课程"));

    adjust.undo();
    QCOMPARE(assigns(), before); // 撤销完整还原
    QVERIFY(adjust.canRedo());
    adjust.redo();
    QCOMPARE(assigns().size(), before.size());
    QCOMPARE(app.school()->schedule.courseAt(ids.class1, 1, 0).value_or(-1), ids.shuxue);

    // 对调 + 撤销
    const auto before2 = assigns();
    QVERIFY(adjust.moveCell(ids.class1, 1, 0, 5, 0).value(QStringLiteral("ok")).toBool());
    QCOMPARE(app.school()->schedule.courseAt(ids.class1, 5, 0).value_or(-1), ids.shuxue);
    QCOMPARE(app.school()->schedule.courseAt(ids.class1, 1, 0).value_or(-1), ids.wuli);
    adjust.undo();
    QCOMPARE(assigns(), before2);

    // 清除课程 + 撤销
    QVERIFY(adjust.unscheduleCell(ids.class1, 5, 0).value(QStringLiteral("ok")).toBool());
    QVERIFY(!app.school()->schedule.courseAt(ids.class1, 5, 0).has_value());
    adjust.undo();
    QCOMPARE(app.school()->schedule.courseAt(ids.class1, 5, 0).value_or(-1), ids.wuli);
}

void TestApp::unplacedAndClear()
{
    AppController app;
    app.newProject(QStringLiteral("未排校"), 5, 7, 4, 20);
    auto *s = app.school();
    sched::ClassInfo c1; c1.name = QStringLiteral("1班");
    const qint64 class1 = s->addClass(c1);
    sched::Course yw; yw.name = QStringLiteral("语文");
    const qint64 yuwen = s->addCourse(yw);
    sched::PlanEntry e; e.classId = class1; e.courseId = yuwen; e.weeklyPeriods = 5;
    s->addPlanEntry(e);
    // 已排 3 节
    s->schedule.setAssignment(class1, 0, 0, yuwen);
    s->schedule.setAssignment(class1, 7, 0, yuwen);
    s->schedule.setAssignment(class1, 14, 0, yuwen);

    AdjustController adjust(&app);
    const QVariantList unplaced = adjust.unplacedCourses(class1);
    QCOMPARE(unplaced.size(), 1);
    QCOMPARE(unplaced.first().toMap().value(QStringLiteral("missing")).toDouble(), 2.0);

    // 安排 1 节 → 缺 1；排满后再排 → 拒
    QVERIFY(adjust.placeUnplaced(class1, yuwen, 1, 0).value(QStringLiteral("ok")).toBool());
    QCOMPARE(adjust.unplacedCourses(class1).first().toMap().value(QStringLiteral("missing")).toDouble(),
             1.0);
    QVERIFY(adjust.placeUnplaced(class1, yuwen, 2, 0).value(QStringLiteral("ok")).toBool());
    QVERIFY(adjust.unplacedCourses(class1).isEmpty());
    const QVariantMap over = adjust.placeUnplaced(class1, yuwen, 3, 0);
    QVERIFY(!over.value(QStringLiteral("ok")).toBool());
    QVERIFY(over.value(QStringLiteral("reason")).toString().contains(QStringLiteral("已排满")));

    // 清除一节 → 缺 1；撤销恢复
    QVERIFY(adjust.unscheduleCell(class1, 2, 0).value(QStringLiteral("ok")).toBool());
    QCOMPARE(adjust.unplacedCourses(class1).first().toMap().value(QStringLiteral("missing")).toDouble(),
             1.0);
    adjust.undo();
    QVERIFY(adjust.unplacedCourses(class1).isEmpty());
}

void TestApp::temporaryModeOps()
{
    AppController app;
    app.newProject(QStringLiteral("临时校"), 5, 7, 4, 20);
    auto *s = app.school();
    sched::ClassInfo c1; c1.name = QStringLiteral("3班");
    const qint64 class3 = s->addClass(c1);
    sched::Course yw; yw.name = QStringLiteral("语文");
    const qint64 yuwen = s->addCourse(yw);
    sched::PlanEntry e; e.classId = class3; e.courseId = yuwen; e.weeklyPeriods = 1;
    s->addPlanEntry(e);
    s->schedule.setAssignment(class3, 0, 0, yuwen);

    // 先落盘清脏，验证临时调课不污染正式课表
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(app.saveProjectAs(dir.filePath(QStringLiteral("t.spk"))));
    QVERIFY(!app.projectDirty());
    const auto snapshot = s->schedule.assignments;

    AdjustController adjust(&app);
    adjust.enterTemporary();
    QVERIFY(adjust.temporaryMode());

    // 超出计划节数任意安排：语文计划只 1 节，临时再加一节
    const QVariantMap r = adjust.placeUnplaced(class3, yuwen, 5, 0);
    QVERIFY2(r.value(QStringLiteral("ok")).toBool(), qPrintable(r.value(QStringLiteral("reason")).toString()));
    QCOMPARE(s->schedule.assignments.size(), snapshot.size() + 1);
    QVERIFY(!app.projectDirty()); // 临时改动不置脏

    // 调课通知文本包含班级与变化
    const QString notice = adjust.tempNoticeText();
    QVERIFY(notice.contains(QStringLiteral("3班")));
    QVERIFY(notice.contains(QStringLiteral("语文")));

    // 导出 txt
    QVERIFY(adjust.exportTempNotice(dir.filePath(QStringLiteral("notice.txt"))));
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("notice.txt"))));
    // 导出 pdf（headless 缺字体时跳过）
    if (!adjust.exportTempNotice(dir.filePath(QStringLiteral("notice.pdf"))))
        QSKIP("当前环境不支持 PDF 导出");
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("notice.pdf"))));

    // 退出即丢弃
    adjust.exitTemporary();
    QVERIFY(!adjust.temporaryMode());
    QCOMPARE(s->schedule.assignments, snapshot);
    QVERIFY(!app.projectDirty());
}

void TestApp::printExport()
{
    AppController app;
    app.newProject(QStringLiteral("打印校"), 5, 7, 4, 20);
    auto *s = app.school();
    sched::ClassInfo c1; c1.name = QStringLiteral("1班");
    sched::ClassInfo c2; c2.name = QStringLiteral("2班");
    const qint64 class1 = s->addClass(c1);
    const qint64 class2 = s->addClass(c2);
    sched::Course yw; yw.name = QStringLiteral("语文"); yw.shortName = QStringLiteral("语");
    const qint64 yuwen = s->addCourse(yw);
    sched::Teacher tz; tz.name = QStringLiteral("张明");
    const qint64 teacher = s->addTeacher(tz);
    sched::PlanEntry e; e.classId = class1; e.courseId = yuwen; e.teacherIds = {teacher};
    e.weeklyPeriods = 3;
    s->addPlanEntry(e);
    s->schedule.setAssignment(class1, 0, 0, yuwen);
    s->schedule.setAssignment(class1, 1, 1, yuwen);
    s->schedule.setAssignment(class2, 0, 0, yuwen);

    PrintController printer(&app);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QVariantMap opt{{QStringLiteral("landscape"), true},
                          {QStringLiteral("showTeachers"), true},
                          {QStringLiteral("headerNote"), QStringLiteral("2025 春季学期")}};

    // PDF：非空文件
    const QString pdf = dir.filePath(QStringLiteral("out.pdf"));
    if (!printer.exportPdf(pdf, QStringLiteral("class"), {class1, class2}, opt))
        QSKIP("当前环境不支持 PDF 渲染");
    QVERIFY(QFileInfo(pdf).size() > 500);

    // 总课表 PDF
    QVERIFY(printer.exportPdf(dir.filePath(QStringLiteral("master.pdf")), QStringLiteral("master"),
                              {}, opt));

    // 预览 PNG
    const QString url = printer.renderPreview(QStringLiteral("class"), {class1}, opt);
    QVERIFY(url.startsWith(QStringLiteral("file:///")));
    QVERIFY(QFileInfo(url.section(QStringLiteral("?"), 0, 0).mid(8)).size() > 200);

    // PNG 序列
    QVERIFY(printer.exportPngs(dir.filePath(QStringLiteral("png")), QStringLiteral("class"),
                               {class1, class2}, opt));
    QVERIFY(QFileInfo(dir.filePath(QStringLiteral("png/page1.png"))).size() > 200);
    QVERIFY(QFileInfo(dir.filePath(QStringLiteral("png/page2.png"))).size() > 200);

    // Excel 导出
    const QString xlsx = dir.filePath(QStringLiteral("out.xlsx"));
    QVERIFY2(printer.exportExcel(xlsx, QStringLiteral("class"), {class1}),
             qPrintable(printer.lastError()));
    QVERIFY(QFileInfo(xlsx).size() > 500);

    // 打印设置随项目持久化
    app.setPrintSettings(opt);
    const QString projPath = dir.filePath(QStringLiteral("print.spk"));
    QVERIFY(app.saveProjectAs(projPath));
    app.closeProject();
    QVERIFY(app.openProject(projPath));
    QCOMPARE(app.printSettings().value(QStringLiteral("headerNote")).toString(),
             QStringLiteral("2025 春季学期"));
    app.closeProject();
}

void TestApp::sampleSchool()
{
    AppController app;
    app.createSampleSchool(false); // 先不排课，验证数据结构
    auto *s = app.school();
    QVERIFY(app.hasProject());
    QVERIFY(app.projectDirty());
    QCOMPARE(s->classes.size(), 6);
    QVERIFY(s->courses.size() >= 10);
    QVERIFY(s->teachers.size() >= 10);
    QVERIFY(s->classrooms.size() >= 3);
    QCOMPARE(s->combinedGroups.size(), 3);          // 体育两两合班
    QCOMPARE(s->noScheduleConstraints.size(), 2);   // 体育老师 + 信息课
    QCOMPARE(s->preAssignments.size(), 1);          // 1班体育预排
    // 含连课与单双周
    bool hasChain = false, hasHalf = false;
    for (const auto &e : s->planEntries) {
        if (e.chainedCount > 0) hasChain = true;
        if (e.weeklyPeriods - int(e.weeklyPeriods) > 0.25) hasHalf = true;
    }
    QVERIFY(hasChain && hasHalf);

    // 自动排课：引擎完成后自动应用，硬违例为 0
    QSignalSpy spy(&app, &AppController::schedulerFinished);
    app.createSampleSchool(true);
    QVERIFY(app.schedulerRunning());
    QVERIFY2(spy.wait(60000), "示例学校排课未在 60s 内完成");
    QVERIFY(!app.resultPending()); // 已自动应用
    QVERIFY(app.hasSchedule());
    // 失败时把报告带出来：硬违例可能来自「没排上」或「撞课」，两者修法完全不同
    const QVariantMap rep = app.schedulerReport();
    QVERIFY2(rep.value(QStringLiteral("hardViolations")).toInt() == 0,
             qPrintable(QStringLiteral("硬违例 %1 处｜已排 %2/%3｜未排明细 %4")
                            .arg(rep.value(QStringLiteral("hardViolations")).toInt())
                            .arg(rep.value(QStringLiteral("placedSessions")).toInt())
                            .arg(rep.value(QStringLiteral("totalSessions")).toInt())
                            .arg(QString::fromUtf8(QJsonDocument::fromVariant(rep.value(QStringLiteral("unplaced")))
                                                       .toJson(QJsonDocument::Compact)))));
    // 预排未被移动
    QCOMPARE(s->schedule.courseAt(s->classes.first().id, 4 * 7, 0).value_or(-1),
             s->courses[9].id); // 体育
    // 自习补齐容量：每班每格单双周均有课，不留空
    for (const auto &c : s->classes) {
        for (int slot = 0; slot < 5 * 7; ++slot) {
            QVERIFY2(s->schedule.courseAt(c.id, slot, 1).has_value(),
                     qPrintable(QStringLiteral("%1 slot%2 单周为空").arg(c.name).arg(slot)));
            QVERIFY2(s->schedule.courseAt(c.id, slot, 2).has_value(),
                     qPrintable(QStringLiteral("%1 slot%2 双周为空").arg(c.name).arg(slot)));
        }
    }
}

void TestApp::projectTemplates()
{
    // 空白模板：不填充任何数据
    {
        AppController app;
        app.newProject(QStringLiteral("空白校"), 5, 7, 4, 20);
        app.applyTemplate(0);
        QCOMPARE(app.school()->classes.size(), 0);
        QCOMPARE(app.school()->courses.size(), 0);
    }
    // 小学模板：6 年级 x 3 班 = 18 班、6 个年级组且成员已勾选、10 门课、5 个教室
    {
        AppController app;
        app.newProject(QStringLiteral("实验小学"), 5, 7, 4, 20);
        app.applyTemplate(1);
        const auto *s = app.school();
        QCOMPARE(s->classes.size(), 18);
        QCOMPARE(s->groups.size(), 6);
        QCOMPARE(s->courses.size(), 10);
        QCOMPARE(s->classrooms.size(), 5);
        for (const auto &g : s->groups)
            QCOMPARE(g.memberIds.size(), 3);
        // 班级 gradeId 指向各自年级组
        for (const auto &c : s->classes)
            QVERIFY(s->findGroup(c.gradeId) != nullptr);
    }
    // 初中模板：3 年级 x 4 班，含物理/化学；越界模板号为空操作
    {
        AppController app;
        app.newProject(QStringLiteral("实验初中"), 5, 8, 4, 20);
        app.applyTemplate(4); // 非法：不应改动
        QCOMPARE(app.school()->classes.size(), 0);
        app.applyTemplate(2);
        QCOMPARE(app.school()->classes.size(), 12);
        QCOMPARE(app.school()->groups.size(), 3);
        bool hasPhysics = false;
        for (const auto &c : app.school()->courses)
            if (c.name == QStringLiteral("物理")) hasPhysics = true;
        QVERIFY(hasPhysics);
    }
}

void TestApp::versionAndUpdate()
{
    // 语义化版本比较：v 前缀、不等长、数值段比较
    QCOMPARE(AppController::compareVersions(QStringLiteral("v1.0.4"), QStringLiteral("1.0.4")), 0);
    QCOMPARE(AppController::compareVersions(QStringLiteral("1.0.10"), QStringLiteral("1.0.9")), 1);
    QCOMPARE(AppController::compareVersions(QStringLiteral("1.0"), QStringLiteral("1.0.1")), -1);
    QCOMPARE(AppController::compareVersions(QStringLiteral("2.0.0"), QStringLiteral("1.9.9")), 1);
    QCOMPARE(AppController::compareVersions(QStringLiteral("V1.2"), QStringLiteral("1.2.0")), 0);

    AppController app;
    QVERIFY(!app.appVersion().isEmpty());
    QVERIFY(!app.qtVersion().isEmpty());
    QCOMPARE(app.updateStatus(), 0); // 未检查
    QVERIFY(!app.logFilePath().isEmpty());

    // 更新批处理：延迟 + 带引号的安装包路径 + Inno 静默旗标；CRLF 行尾；绝无 \" 转义残留
    const QString bat =
        AppController::updateBatchContent(QStringLiteral("C:\\临时 目录\\SmartScheduler-setup.exe"));
    QVERIFY(bat.contains(QStringLiteral(
        "\"C:\\临时 目录\\SmartScheduler-setup.exe\" /VERYSILENT /NORESTART /CLOSEAPPLICATIONS")));
    QVERIFY(bat.contains(QStringLiteral("ping 127.0.0.1")));
    QVERIFY(bat.contains(QStringLiteral("\r\n")));
    QVERIFY(!bat.contains(QStringLiteral("\\\"")));
}

void TestApp::logExport()
{
    AppController app;
    // 写入一条假日志（路径基于测试隔离的 org/app 名）
    const QString logPath = app.logFilePath();
    QVERIFY(QDir().mkpath(QFileInfo(logPath).absolutePath()));
    {
        QFile f(logPath);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("2025-01-01 00:00:00.000 [INFO] test log line\n");
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dest = dir.filePath(QStringLiteral("exported.log"));
    QVERIFY(app.exportLog(dest));
    QFile out(dest);
    QVERIFY(out.open(QIODevice::ReadOnly | QIODevice::Text));
    QVERIFY(QString::fromUtf8(out.readAll()).contains(QStringLiteral("test log line")));

    // file: URL 形式同样可用
    const QString dest2 = dir.filePath(QStringLiteral("exported2.log"));
    QVERIFY(app.exportLog(QUrl::fromLocalFile(dest2).toString()));
    QVERIFY(QFile::exists(dest2));
}

// QPdfWriter/QFontDatabase 需要 QGuiApplication；headless 用 minimal 平台
int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "minimal");
    QGuiApplication app(argc, argv);
    TestApp tc;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_app.moc"
