#include "AppController.h"
#include "GroupModel.h"
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

QTEST_GUILESS_MAIN(TestApp)

#include "tst_app.moc"
