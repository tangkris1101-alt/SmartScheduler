#include "ExcelIo.h"
#include "model/School.h"

#include <QTemporaryDir>
#include <QtTest/QTest>
#include <xlsxdocument.h>

using namespace sched;

// Excel 导入导出测试
class TestExcel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void templateRoundTrip();
    void importBasic();
    void importSkipAndOverwrite();
    void importErrorRows();
    void exportTimetablesContent();
};

void TestExcel::templateRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("template.xlsx"));
    QString err;
    QVERIFY2(ExcelImporter::writeImportTemplate(path, &err), qPrintable(err));

    // 读回：5 张表 + 表头
    QXlsx::Document doc(path);
    QVERIFY(doc.load());
    const QStringList sheets = doc.sheetNames();
    for (const QString &s : {QStringLiteral("班级"), QStringLiteral("课程"), QStringLiteral("老师"),
                             QStringLiteral("教室"), QStringLiteral("教学计划")})
        QVERIFY2(sheets.contains(s), qPrintable(s));
    doc.selectSheet(QStringLiteral("班级"));
    QCOMPARE(doc.read(1, 1).toString(), QStringLiteral("名称*"));
    QCOMPARE(doc.read(2, 1).toString(), QStringLiteral("高一1班")); // 示例行
}

// 造一个导入文件：2 班 / 2 课 / 1 师 / 1 教室 / 2 条计划
static QString makeImportFile(QTemporaryDir &dir)
{
    const QString path = dir.filePath(QStringLiteral("import.xlsx"));
    QXlsx::Document doc;
    doc.addSheet(QStringLiteral("班级"));
    doc.selectSheet(QStringLiteral("班级"));
    doc.write(1, 1, QStringLiteral("名称*"));
    doc.write(2, 1, QStringLiteral("1班"));
    doc.write(2, 4, QStringLiteral("yb"));
    doc.write(2, 6, QStringLiteral("高一年级"));
    doc.write(3, 1, QStringLiteral("2班"));

    doc.addSheet(QStringLiteral("课程"));
    doc.selectSheet(QStringLiteral("课程"));
    doc.write(1, 1, QStringLiteral("名称*"));
    doc.write(2, 1, QStringLiteral("语文"));
    doc.write(2, 3, QStringLiteral("语文教研组"));
    doc.write(2, 4, QStringLiteral("是"));
    doc.write(2, 8, QStringLiteral("优先上午"));
    doc.write(3, 1, QStringLiteral("数学"));
    doc.write(3, 5, QStringLiteral("是"));

    doc.addSheet(QStringLiteral("老师"));
    doc.selectSheet(QStringLiteral("老师"));
    doc.write(1, 1, QStringLiteral("姓名*"));
    doc.write(2, 1, QStringLiteral("张明"));
    doc.write(2, 4, 5);

    doc.addSheet(QStringLiteral("教室"));
    doc.selectSheet(QStringLiteral("教室"));
    doc.write(1, 1, QStringLiteral("名称*"));
    doc.write(2, 1, QStringLiteral("实验室"));
    doc.write(2, 2, 2);

    doc.addSheet(QStringLiteral("教学计划"));
    doc.selectSheet(QStringLiteral("教学计划"));
    doc.write(1, 1, QStringLiteral("班级*"));
    doc.write(2, 1, QStringLiteral("1班"));
    doc.write(2, 2, QStringLiteral("语文"));
    doc.write(2, 3, QStringLiteral("张明"));
    doc.write(2, 4, 5);
    doc.write(2, 5, 1);
    doc.write(2, 6, 2);
    doc.write(3, 1, QStringLiteral("2班"));
    doc.write(3, 2, QStringLiteral("数学"));
    doc.write(3, 4, 4.5);

    if (!doc.saveAs(path))
        return {};
    return path;
}

void TestExcel::importBasic()
{
    QTemporaryDir dir;
    const QString path = makeImportFile(dir);

    School school;
    const ImportReport rep = ExcelImporter::importFile(path, &school, false);
    QVERIFY(!rep.fatal);
    QVERIFY(rep.errors.isEmpty());
    QCOMPARE(rep.created, 8); // 2班+2课+1师+1室+2计划
    QCOMPARE(rep.skipped, 0);

    QCOMPARE(school.classes.size(), 2);
    QCOMPARE(school.courses.size(), 2);
    QCOMPARE(school.teachers.size(), 1);
    QCOMPARE(school.classrooms.size(), 1);
    QCOMPARE(school.planEntries.size(), 2);

    // 字段解析：课程特征
    const Course *yw = school.findCourse(school.courses.first().id);
    QCOMPARE(yw->name, QStringLiteral("语文"));
    QVERIFY(yw->isMajor);
    QCOMPARE(yw->amPmPolicy, AmPmPolicy::Morning);
    QVERIFY(school.courses[1].isScience);
    // 年级组自动创建且 1班 已入组
    QCOMPARE(school.groups.size(), 2); // 年级 + 语文教研组
    const ClassInfo *c1 = school.findClass(school.classes.first().id);
    QVERIFY(c1->gradeId > 0);
    // 计划字段
    QCOMPARE(school.planEntries[0].weeklyPeriods, 5.0);
    QCOMPARE(school.planEntries[0].chainedCount, 1);
    QCOMPARE(school.planEntries[0].teacherIds.size(), 1);
    QCOMPARE(school.planEntries[1].weeklyPeriods, 4.5);
}

void TestExcel::importSkipAndOverwrite()
{
    QTemporaryDir dir;
    const QString path = makeImportFile(dir);

    School school;
    ExcelImporter::importFile(path, &school, false);
    // 把 1班 的优先次序改掉，验证两策略
    school.classes.first().priority = 9;

    // 跳过策略：全部重复 → skipped，字段不被改回
    const ImportReport skipRep = ExcelImporter::importFile(path, &school, false);
    QVERIFY(skipRep.skipped >= 5);
    QCOMPARE(skipRep.created, 0);
    QCOMPARE(school.classes.first().priority, 9); // 未被覆盖
    QCOMPARE(school.classes.size(), 2);

    // 覆盖策略：updated > 0
    const ImportReport owRep = ExcelImporter::importFile(path, &school, true);
    QVERIFY(owRep.updated >= 5);
    QCOMPARE(owRep.created, 0);
    QCOMPARE(school.classes.size(), 2); // 不产生重复
}

void TestExcel::importErrorRows()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("bad.xlsx"));
    QXlsx::Document doc;
    doc.addSheet(QStringLiteral("班级"));
    doc.selectSheet(QStringLiteral("班级"));
    doc.write(1, 1, QStringLiteral("名称*"));
    doc.write(2, 2, QStringLiteral("没有名字的班")); // 名称为空 → 错误行
    doc.addSheet(QStringLiteral("教学计划"));
    doc.selectSheet(QStringLiteral("教学计划"));
    doc.write(1, 1, QStringLiteral("班级*"));
    doc.write(2, 1, QStringLiteral("1班"));
    doc.write(2, 2, QStringLiteral("语文"));
    // 周节数缺失 → 错误行
    QVERIFY(doc.saveAs(path));

    School school;
    const ImportReport rep = ExcelImporter::importFile(path, &school, false);
    QVERIFY(!rep.fatal);
    QVERIFY(rep.errors.size() >= 2);
    QVERIFY(rep.errors.join(QStringLiteral("\n")).contains(QStringLiteral("第2行")));
}

void TestExcel::exportTimetablesContent()
{
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
    Teacher tz; tz.name = QStringLiteral("张明");
    const qint64 teacher = school.addTeacher(tz);
    PlanEntry e; e.classId = class1; e.courseId = yuwen; e.teacherIds = {teacher}; e.weeklyPeriods = 5;
    school.addPlanEntry(e);
    school.schedule.setAssignment(class1, 0, 0, yuwen);
    school.schedule.setAssignment(class1, 1, 1, yuwen); // 单周

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("export.xlsx"));
    QString err;
    QVERIFY2(ExcelExporter::exportTimetables(path, school, {class1}, {teacher}, true, &err),
             qPrintable(err));

    QXlsx::Document doc(path);
    QVERIFY(doc.load());
    QVERIFY(doc.sheetNames().contains(QStringLiteral("班级-1班")));
    QVERIFY(doc.sheetNames().contains(QStringLiteral("老师-张明")));
    QVERIFY(doc.sheetNames().contains(QStringLiteral("总课表")));

    doc.selectSheet(QStringLiteral("班级-1班"));
    QCOMPARE(doc.read(1, 2).toString(), QStringLiteral("周一"));
    QVERIFY(doc.read(2, 2).toString().contains(QStringLiteral("语文"))); // 周一第1节
    QVERIFY(doc.read(2, 2).toString().contains(QStringLiteral("张明"))); // 含老师名
    QVERIFY(doc.read(3, 2).toString().contains(QStringLiteral("(单)"))); // 单周标注

    doc.selectSheet(QStringLiteral("老师-张明"));
    QVERIFY(doc.read(2, 2).toString().contains(QStringLiteral("1班")));
    QVERIFY(doc.read(2, 2).toString().contains(QStringLiteral("语文")));

    doc.selectSheet(QStringLiteral("总课表"));
    QCOMPARE(doc.read(2, 1).toString(), QStringLiteral("1班"));
    QCOMPARE(doc.read(2, 2).toString(), QStringLiteral("语文"));
}

QTEST_GUILESS_MAIN(TestExcel)

#include "tst_excel.moc"
