#include "ExcelIo.h"

#include <QRegularExpression>
#include <algorithm>
#include <xlsxdocument.h>
#include <xlsxformat.h>
#include <xlsxworksheet.h>

namespace sched {

namespace {

const char *kSheetClasses = "班级";
const char *kSheetCourses = "课程";
const char *kSheetTeachers = "老师";
const char *kSheetRooms = "教室";
const char *kSheetPlans = "教学计划";

QXlsx::Format headerFormat()
{
    QXlsx::Format f;
    f.setFontBold(true);
    return f;
}

QString cellText(QXlsx::Document &doc, int row, int col)
{
    return doc.read(row, col).toString().trimmed();
}

double cellNumber(QXlsx::Document &doc, int row, int col, double def = 0)
{
    const QVariant v = doc.read(row, col);
    bool ok = false;
    const double d = v.toDouble(&ok);
    return ok ? d : def;
}

// 名单按名查找或新建
qint64 findOrCreateClass(School &s, const QString &name, bool *created)
{
    for (const auto &c : s.classes) {
        if (c.name == name) {
            *created = false;
            return c.id;
        }
    }
    ClassInfo c;
    c.name = name;
    *created = true;
    return s.addClass(c);
}

qint64 findOrCreateCourse(School &s, const QString &name, bool *created)
{
    for (const auto &c : s.courses) {
        if (c.name == name) {
            *created = false;
            return c.id;
        }
    }
    Course c;
    c.name = name;
    *created = true;
    return s.addCourse(c);
}

qint64 findOrCreateTeacher(School &s, const QString &name, bool *created)
{
    for (const auto &t : s.teachers) {
        if (t.name == name) {
            *created = false;
            return t.id;
        }
    }
    Teacher t;
    t.name = name;
    *created = true;
    return s.addTeacher(t);
}

// 归类名单按名查找或新建（用于年级/教研组列）
qint64 findOrCreateGroup(School &s, Group::Type type, const QString &name)
{
    for (const auto &g : s.groups) {
        if (g.type == type && g.name == name)
            return g.id;
    }
    Group g;
    g.type = type;
    g.name = name;
    return s.addGroup(g);
}

bool yesNo(const QString &text)
{
    return text == QStringLiteral("是") || text.compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0
           || text == QStringLiteral("1");
}

} // namespace

// ==================== 模板 ====================

bool ExcelImporter::writeImportTemplate(const QString &path, QString *error)
{
    QXlsx::Document doc;
    const QXlsx::Format header = headerFormat();

    auto writeHeader = [&](const QStringList &cols) {
        for (int i = 0; i < cols.size(); ++i)
            doc.write(1, i + 1, cols[i], header);
    };

    doc.addSheet(QString::fromUtf8(kSheetClasses));
    doc.selectSheet(QString::fromUtf8(kSheetClasses));
    writeHeader({QStringLiteral("名称*"), QStringLiteral("全称"), QStringLiteral("简称"), QStringLiteral("拼音码"),
                 QStringLiteral("优先次序"), QStringLiteral("年级"), QStringLiteral("备注")});
    doc.write(2, 1, QStringLiteral("高一1班"));
    doc.write(2, 2, QStringLiteral("高一年级（1）班"));
    doc.write(2, 3, QStringLiteral("一1"));
    doc.write(2, 4, QStringLiteral("gy1b"));
    doc.write(2, 5, 1);
    doc.write(2, 6, QStringLiteral("高一年级"));

    doc.addSheet(QString::fromUtf8(kSheetCourses));
    doc.selectSheet(QString::fromUtf8(kSheetCourses));
    writeHeader({QStringLiteral("名称*"), QStringLiteral("拼音码"), QStringLiteral("教研组"), QStringLiteral("主课(是/否)"),
                 QStringLiteral("理科(是/否)"), QStringLiteral("平均分布(是/否)"), QStringLiteral("进度一致班级数"),
                 QStringLiteral("上下午(自动/优先上午/优先下午)"), QStringLiteral("备注")});
    doc.write(2, 1, QStringLiteral("语文"));
    doc.write(2, 2, QStringLiteral("yw"));
    doc.write(2, 3, QStringLiteral("语文教研组"));
    doc.write(2, 4, QStringLiteral("是"));
    doc.write(2, 8, QStringLiteral("优先上午"));

    doc.addSheet(QString::fromUtf8(kSheetTeachers));
    doc.selectSheet(QString::fromUtf8(kSheetTeachers));
    writeHeader({QStringLiteral("姓名*"), QStringLiteral("拼音码"), QStringLiteral("教研组"), QStringLiteral("日最多课(空=不限)"),
                 QStringLiteral("互斥组(同组同时只上一人)"), QStringLiteral("备注")});
    doc.write(2, 1, QStringLiteral("张明"));
    doc.write(2, 2, QStringLiteral("zm"));
    doc.write(2, 3, QStringLiteral("语文教研组"));
    doc.write(2, 4, 5);

    doc.addSheet(QString::fromUtf8(kSheetRooms));
    doc.selectSheet(QString::fromUtf8(kSheetRooms));
    writeHeader({QStringLiteral("名称*"), QStringLiteral("容量(同时容纳班级数)"), QStringLiteral("拼音码"), QStringLiteral("备注")});
    doc.write(2, 1, QStringLiteral("物理实验室"));
    doc.write(2, 2, 2);

    doc.addSheet(QString::fromUtf8(kSheetPlans));
    doc.selectSheet(QString::fromUtf8(kSheetPlans));
    writeHeader({QStringLiteral("班级*"), QStringLiteral("课程*"), QStringLiteral("任课老师(顿号分隔)"),
                 QStringLiteral("周节数*"), QStringLiteral("连课次数"), QStringLiteral("连课节数"),
                 QStringLiteral("上午节数(空=自动)"), QStringLiteral("合班(是/否)"), QStringLiteral("教室"),
                 QStringLiteral("开课周"), QStringLiteral("结束周(空=学期末)"), QStringLiteral("进度组")});
    doc.write(2, 1, QStringLiteral("高一1班"));
    doc.write(2, 2, QStringLiteral("语文"));
    doc.write(2, 3, QStringLiteral("张明"));
    doc.write(2, 4, 5);
    doc.write(2, 5, 1);
    doc.write(2, 6, 2);

    if (!doc.saveAs(path)) {
        if (error)
            *error = QStringLiteral("无法写入文件：%1").arg(path);
        return false;
    }
    return true;
}

// ==================== 导入 ====================

ImportReport ExcelImporter::importFile(const QString &path, School *school, bool overwrite)
{
    ImportReport report;
    QXlsx::Document doc(path);
    if (!doc.load()) {
        report.fatal = true;
        report.errors.append(QStringLiteral("无法打开 Excel 文件：%1").arg(path));
        return report;
    }
    School &s = *school;

    // ---- 班级表 ----
    if (doc.selectSheet(QString::fromUtf8(kSheetClasses))) {
        const auto dims = doc.currentWorksheet()->dimension();
        for (int row = 2; row <= dims.lastRow(); ++row) {
            const QString name = cellText(doc, row, 1);
            if (name.isEmpty()) {
                if (!cellText(doc, row, 2).isEmpty())
                    report.errors.append(QStringLiteral("【班级】第%1行：名称为空").arg(row));
                continue;
            }
            ClassInfo *existing = nullptr;
            for (auto &c : s.classes) {
                if (c.name == name)
                    existing = &c;
            }
            if (existing && !overwrite) {
                report.skipped++;
                continue;
            }
            if (!existing) {
                ClassInfo c;
                c.name = name;
                const qint64 id = s.addClass(c);
                existing = s.findClass(id);
                report.created++;
            } else {
                report.updated++;
            }
            existing->longName = cellText(doc, row, 2);
            existing->shortName = cellText(doc, row, 3);
            existing->code = cellText(doc, row, 4);
            if (doc.read(row, 5).isValid())
                existing->priority = int(cellNumber(doc, row, 5, 0));
            const QString gradeName = cellText(doc, row, 6);
            if (!gradeName.isEmpty()) {
                const qint64 gid = findOrCreateGroup(s, Group::Type::Grade, gradeName);
                existing->gradeId = gid;
                if (Group *g = s.findGroup(gid); g && !g->memberIds.contains(existing->id))
                    g->memberIds.append(existing->id);
            }
            existing->remark = cellText(doc, row, 7);
        }
    }

    // ---- 课程表 ----
    if (doc.selectSheet(QString::fromUtf8(kSheetCourses))) {
        const auto dims = doc.currentWorksheet()->dimension();
        for (int row = 2; row <= dims.lastRow(); ++row) {
            const QString name = cellText(doc, row, 1);
            if (name.isEmpty()) {
                if (!cellText(doc, row, 2).isEmpty())
                    report.errors.append(QStringLiteral("【课程】第%1行：名称为空").arg(row));
                continue;
            }
            Course *existing = nullptr;
            for (auto &c : s.courses) {
                if (c.name == name)
                    existing = &c;
            }
            if (existing && !overwrite) {
                report.skipped++;
                continue;
            }
            if (!existing) {
                Course c;
                c.name = name;
                const qint64 id = s.addCourse(c);
                existing = s.findCourse(id);
                report.created++;
            } else {
                report.updated++;
            }
            existing->code = cellText(doc, row, 2);
            const QString groupName = cellText(doc, row, 3);
            if (!groupName.isEmpty()) {
                const qint64 gid = findOrCreateGroup(s, Group::Type::CourseGroup, groupName);
                existing->groupId = gid;
                if (Group *g = s.findGroup(gid); g && !g->memberIds.contains(existing->id))
                    g->memberIds.append(existing->id);
            }
            existing->isMajor = yesNo(cellText(doc, row, 4));
            existing->isScience = yesNo(cellText(doc, row, 5));
            existing->evenSpread = yesNo(cellText(doc, row, 6));
            if (doc.read(row, 7).isValid() && !cellText(doc, row, 7).isEmpty())
                existing->progressAlignCount = int(cellNumber(doc, row, 7, 2));
            const QString ampm = cellText(doc, row, 8);
            if (ampm.contains(QStringLiteral("上午")))
                existing->amPmPolicy = AmPmPolicy::Morning;
            else if (ampm.contains(QStringLiteral("下午")))
                existing->amPmPolicy = AmPmPolicy::Afternoon;
            existing->remark = cellText(doc, row, 9);
        }
    }

    // ---- 老师表 ----
    if (doc.selectSheet(QString::fromUtf8(kSheetTeachers))) {
        const auto dims = doc.currentWorksheet()->dimension();
        for (int row = 2; row <= dims.lastRow(); ++row) {
            const QString name = cellText(doc, row, 1);
            if (name.isEmpty()) {
                if (!cellText(doc, row, 2).isEmpty())
                    report.errors.append(QStringLiteral("【老师】第%1行：姓名为空").arg(row));
                continue;
            }
            Teacher *existing = nullptr;
            for (auto &t : s.teachers) {
                if (t.name == name)
                    existing = &t;
            }
            if (existing && !overwrite) {
                report.skipped++;
                continue;
            }
            if (!existing) {
                Teacher t;
                t.name = name;
                const qint64 id = s.addTeacher(t);
                existing = s.findTeacher(id);
                report.created++;
            } else {
                report.updated++;
            }
            existing->code = cellText(doc, row, 2);
            const QString groupName = cellText(doc, row, 3);
            if (!groupName.isEmpty()) {
                const qint64 gid = findOrCreateGroup(s, Group::Type::TeacherGroup, groupName);
                existing->groupId = gid;
                if (Group *g = s.findGroup(gid); g && !g->memberIds.contains(existing->id))
                    g->memberIds.append(existing->id);
            }
            existing->maxPeriodsPerDay = cellText(doc, row, 4).isEmpty()
                ? -1
                : int(cellNumber(doc, row, 4, -1));
            const QString mutex = cellText(doc, row, 5);
            existing->mutualExclusionGroup = mutex.isEmpty() ? -1 : qint64(cellNumber(doc, row, 5, -1));
            existing->remark = cellText(doc, row, 6);
        }
    }

    // ---- 教室表 ----
    if (doc.selectSheet(QString::fromUtf8(kSheetRooms))) {
        const auto dims = doc.currentWorksheet()->dimension();
        for (int row = 2; row <= dims.lastRow(); ++row) {
            const QString name = cellText(doc, row, 1);
            if (name.isEmpty())
                continue;
            Classroom *existing = nullptr;
            for (auto &r : s.classrooms) {
                if (r.name == name)
                    existing = &r;
            }
            if (existing && !overwrite) {
                report.skipped++;
                continue;
            }
            if (!existing) {
                Classroom r;
                r.name = name;
                const qint64 id = s.addClassroom(r);
                existing = s.findClassroom(id);
                report.created++;
            } else {
                report.updated++;
            }
            if (!cellText(doc, row, 2).isEmpty())
                existing->capacity = std::max(1, int(cellNumber(doc, row, 2, 1)));
            existing->code = cellText(doc, row, 3);
            existing->remark = cellText(doc, row, 4);
        }
    }

    // ---- 教学计划表 ----
    if (doc.selectSheet(QString::fromUtf8(kSheetPlans))) {
        const auto dims = doc.currentWorksheet()->dimension();
        for (int row = 2; row <= dims.lastRow(); ++row) {
            const QString className = cellText(doc, row, 1);
            const QString courseName = cellText(doc, row, 2);
            if (className.isEmpty() && courseName.isEmpty())
                continue;
            if (className.isEmpty() || courseName.isEmpty()) {
                report.errors.append(QStringLiteral("【教学计划】第%1行：班级或课程为空").arg(row));
                continue;
            }
            bool created = false;
            const qint64 classId = findOrCreateClass(s, className, &created);
            if (created)
                report.created++;
            const qint64 courseId = findOrCreateCourse(s, courseName, &created);
            if (created)
                report.created++;

            PlanEntry *entry = nullptr;
            for (auto &e : s.planEntries) {
                if (e.classId == classId && e.courseId == courseId)
                    entry = &e;
            }
            if (entry && !overwrite) {
                report.skipped++;
                continue;
            }
            if (!entry) {
                PlanEntry e;
                e.classId = classId;
                e.courseId = courseId;
                const qint64 id = s.addPlanEntry(e);
                entry = s.findPlanEntry(id);
                report.created++;
            } else {
                report.updated++;
            }
            // 任课老师（顿号/逗号分隔）
            entry->teacherIds.clear();
            const QString teachersText = cellText(doc, row, 3);
            for (const QString &tname : teachersText.split(QRegularExpression(QStringLiteral("[、,，;；]")),
                                                           Qt::SkipEmptyParts)) {
                bool tc = false;
                const qint64 tid = findOrCreateTeacher(s, tname.trimmed(), &tc);
                if (tc)
                    report.created++;
                if (!entry->teacherIds.contains(tid))
                    entry->teacherIds.append(tid);
            }
            const double periods = cellNumber(doc, row, 4, 0);
            if (periods <= 0) {
                report.errors.append(
                    QStringLiteral("【教学计划】第%1行：周节数无效（%2）").arg(row).arg(cellText(doc, row, 4)));
            }
            entry->weeklyPeriods = periods;
            entry->chainedCount = int(cellNumber(doc, row, 5, 0));
            if (doc.read(row, 6).isValid() && !cellText(doc, row, 6).isEmpty())
                entry->chainLength = std::max(2, int(cellNumber(doc, row, 6, 2)));
            entry->morningPeriods = cellText(doc, row, 7).isEmpty() ? -1 : int(cellNumber(doc, row, 7, -1));
            entry->isCombined = yesNo(cellText(doc, row, 8));
            const QString roomName = cellText(doc, row, 9);
            entry->classroomId = -1;
            if (!roomName.isEmpty()) {
                bool rc = false;
                bool found = false;
                for (const auto &r : s.classrooms) {
                    if (r.name == roomName) {
                        entry->classroomId = r.id;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    Classroom r;
                    r.name = roomName;
                    entry->classroomId = s.addClassroom(r);
                    report.created++;
                    Q_UNUSED(rc);
                }
            }
            if (!cellText(doc, row, 10).isEmpty())
                entry->startWeek = std::max(1, int(cellNumber(doc, row, 10, 1)));
            entry->endWeek = cellText(doc, row, 11).isEmpty() ? 0 : int(cellNumber(doc, row, 11, 0));
            entry->progressGroup = cellText(doc, row, 12).isEmpty() ? -1 : int(cellNumber(doc, row, 12, -1));
        }
    }

    return report;
}

// ==================== 课表导出 ====================

namespace {

QXlsx::Format cellFormat(bool center = true)
{
    QXlsx::Format f;
    f.setTextWrap(true);
    if (center) {
        f.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
        f.setVerticalAlignment(QXlsx::Format::AlignVCenter);
    }
    return f;
}

QString cellLabel(const School &s, qint64 classId, int slot)
{
    QStringList lines;
    for (const auto &a : s.schedule.assignments) {
        if (a.classId != classId || a.slotIndex != slot)
            continue;
        const Course *co = s.findCourse(a.courseId);
        QString text = co ? co->name : QStringLiteral("?");
        if (a.half == 1)
            text += QStringLiteral("(单)");
        else if (a.half == 2)
            text += QStringLiteral("(双)");
        QStringList teacherNames;
        for (const auto &e : s.planEntries) {
            if (e.classId == classId && e.courseId == a.courseId) {
                for (qint64 tid : e.teacherIds) {
                    if (const Teacher *t = s.findTeacher(tid))
                        teacherNames.append(t->name);
                }
                break;
            }
        }
        if (!teacherNames.isEmpty())
            text += QStringLiteral("\n") + teacherNames.join(QStringLiteral("、"));
        lines.append(text);
    }
    return lines.join(QStringLiteral(" / "));
}

void writeWeekHeader(QXlsx::Document &doc, const TimeConfig &tc, const QXlsx::Format &fmt)
{
    const QStringList days = {QStringLiteral("周一"), QStringLiteral("周二"), QStringLiteral("周三"),
                              QStringLiteral("周四"), QStringLiteral("周五"), QStringLiteral("周六"),
                              QStringLiteral("周日")};
    doc.write(1, 1, QStringLiteral("节次"), fmt);
    for (int d = 0; d < tc.daysPerWeek; ++d)
        doc.write(1, d + 2, days[d], fmt);
}

void writeClassSheet(QXlsx::Document &doc, const School &s, qint64 classId)
{
    const ClassInfo *c = s.findClass(classId);
    if (!c)
        return;
    const TimeConfig &tc = s.timeConfig;
    const QXlsx::Format fmt = cellFormat();
    QXlsx::Format head = headerFormat();
    head.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
    writeWeekHeader(doc, tc, head);
    for (int p = 0; p < tc.periodsPerDay; ++p) {
        doc.write(p + 2, 1, QStringLiteral("第%1节").arg(p + 1), head);
        for (int d = 0; d < tc.daysPerWeek; ++d)
            doc.write(p + 2, d + 2, cellLabel(s, classId, tc.slotIndex(d, p)), fmt);
    }
    Q_UNUSED(c);
}

} // namespace

bool ExcelExporter::exportTimetables(const QString &path, const School &school,
                                     const QVector<qint64> &classIds, const QVector<qint64> &teacherIds,
                                     bool masterSheet, QString *error)
{
    QXlsx::Document doc;
    bool anySheet = false;

    for (qint64 cid : classIds) {
        const ClassInfo *c = school.findClass(cid);
        if (!c)
            continue;
        doc.addSheet(QStringLiteral("班级-%1").arg(c->name));
        doc.selectSheet(QStringLiteral("班级-%1").arg(c->name));
        writeClassSheet(doc, school, cid);
        anySheet = true;
    }

    for (qint64 tid : teacherIds) {
        const Teacher *t = school.findTeacher(tid);
        if (!t)
            continue;
        doc.addSheet(QStringLiteral("老师-%1").arg(t->name));
        doc.selectSheet(QStringLiteral("老师-%1").arg(t->name));
        const TimeConfig &tc = school.timeConfig;
        QXlsx::Format head = headerFormat();
        head.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
        writeWeekHeader(doc, tc, head);
        const QXlsx::Format fmt = cellFormat();
        for (int p = 0; p < tc.periodsPerDay; ++p) {
            doc.write(p + 2, 1, QStringLiteral("第%1节").arg(p + 1), head);
            for (int d = 0; d < tc.daysPerWeek; ++d) {
                const int slot = tc.slotIndex(d, p);
                QStringList lines;
                for (const auto &occ : school.teacherOccupancy(tid, slot)) {
                    const ClassInfo *c = school.findClass(occ.classId);
                    const Course *co = school.findCourse(occ.courseId);
                    QString text = QStringLiteral("%1 %2")
                                       .arg(c ? c->name : QStringLiteral("活动"),
                                            co ? co->name : QStringLiteral("?"));
                    if (occ.half == 1)
                        text += QStringLiteral("(单)");
                    else if (occ.half == 2)
                        text += QStringLiteral("(双)");
                    lines.append(text);
                }
                doc.write(p + 2, d + 2, lines.join(QStringLiteral("\n")), fmt);
            }
        }
        anySheet = true;
    }

    if (masterSheet) {
        doc.addSheet(QStringLiteral("总课表"));
        doc.selectSheet(QStringLiteral("总课表"));
        const TimeConfig &tc = school.timeConfig;
        QXlsx::Format head = headerFormat();
        head.setHorizontalAlignment(QXlsx::Format::AlignHCenter);
        const QXlsx::Format fmt = cellFormat();
        // 行=班级，列=节次（周一1..N、周二1..N…）
        doc.write(1, 1, QStringLiteral("班级"), head);
        int col = 2;
        const QStringList days = {QStringLiteral("一"), QStringLiteral("二"), QStringLiteral("三"),
                                  QStringLiteral("四"), QStringLiteral("五"), QStringLiteral("六"),
                                  QStringLiteral("日")};
        for (int d = 0; d < tc.daysPerWeek; ++d) {
            for (int p = 0; p < tc.periodsPerDay; ++p)
                doc.write(1, col++, QStringLiteral("周%1第%2节").arg(days[d]).arg(p + 1), head);
        }
        int row = 2;
        for (const auto &c : school.classes) {
            doc.write(row, 1, c.name, head);
            col = 2;
            for (int slot = 0; slot < tc.slotCount(); ++slot) {
                QString text;
                for (const auto &a : school.schedule.assignments) {
                    if (a.classId == c.id && a.slotIndex == slot) {
                        const Course *co = school.findCourse(a.courseId);
                        text = co ? co->name : QStringLiteral("?");
                        if (a.half == 1)
                            text += QStringLiteral("(单)");
                        else if (a.half == 2)
                            text += QStringLiteral("(双)");
                        break;
                    }
                }
                doc.write(row, col++, text, fmt);
            }
            row++;
        }
        anySheet = true;
    }

    if (!anySheet) {
        if (error)
            *error = QStringLiteral("没有选择任何导出对象");
        return false;
    }
    if (!doc.saveAs(path)) {
        if (error)
            *error = QStringLiteral("无法写入文件：%1").arg(path);
        return false;
    }
    return true;
}

} // namespace sched
