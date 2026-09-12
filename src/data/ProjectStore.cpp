#include "ProjectStore.h"

#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace sched {

namespace {

constexpr int kSchemaVersion = 1;

// 具名名单公共列（id,name,long_name,short_name,code,priority,remark）的读写辅助

// null QString 会被绑定为 SQL NULL，违反 NOT NULL；统一转为空串
QVariant textValue(const QString &s)
{
    return s.isNull() ? QVariant(QStringLiteral("")) : QVariant(s);
}

void bindNamed(QSqlQuery &q, const NamedEntity &e)
{
    q.addBindValue(e.id);
    q.addBindValue(textValue(e.name));
    q.addBindValue(textValue(e.longName));
    q.addBindValue(textValue(e.shortName));
    q.addBindValue(textValue(e.code));
    q.addBindValue(e.priority);
    q.addBindValue(textValue(e.remark));
}

void readNamed(QSqlQuery &q, NamedEntity &e)
{
    e.id = q.value(0).toLongLong();
    e.name = q.value(1).toString();
    e.longName = q.value(2).toString();
    e.shortName = q.value(3).toString();
    e.code = q.value(4).toString();
    e.priority = q.value(5).toInt();
    e.remark = q.value(6).toString();
}

} // namespace

ProjectStore::ProjectStore()
    : m_connName(QStringLiteral("sched_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
}

ProjectStore::~ProjectStore()
{
    if (m_db.isValid()) {
        if (m_db.isOpen())
            m_db.close();
        m_db = QSqlDatabase();
    }
    QSqlDatabase::removeDatabase(m_connName);
}

bool ProjectStore::fail(const QString &message)
{
    m_lastError = message;
    return false;
}

bool ProjectStore::exec(const QString &sql)
{
    QSqlQuery q(m_db);
    if (!q.exec(sql))
        return fail(q.lastError().text() + QStringLiteral(" | SQL: ") + sql);
    return true;
}

bool ProjectStore::createNew(const QString &path)
{
    close();
    // 覆盖已有文件
    if (QFile::exists(path) && !QFile::remove(path))
        return fail(QStringLiteral("无法覆盖已有文件: %1").arg(path));
    m_db.setDatabaseName(path);
    if (!m_db.open())
        return fail(m_db.lastError().text());
    if (!exec(QStringLiteral("PRAGMA foreign_keys = ON")))
        return false;
    return createSchema();
}

bool ProjectStore::open(const QString &path)
{
    close();
    if (!QFile::exists(path))
        return fail(QStringLiteral("文件不存在: %1").arg(path));
    m_db.setDatabaseName(path);
    if (!m_db.open())
        return fail(m_db.lastError().text());
    if (!exec(QStringLiteral("PRAGMA foreign_keys = ON")))
        return false;
    // 校验 schema 版本
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT value FROM meta WHERE key='schema_version'")) || !q.next())
        return fail(QStringLiteral("不是有效的 SmartScheduler 工程文件（缺少 meta 表）"));
    if (q.value(0).toInt() > kSchemaVersion)
        return fail(QStringLiteral("工程文件版本过新（%1），当前程序支持到 %2")
                        .arg(q.value(0).toInt())
                        .arg(kSchemaVersion));
    return true;
}

void ProjectStore::close()
{
    if (m_db.isOpen())
        m_db.close();
}

bool ProjectStore::isOpen() const
{
    return m_db.isOpen();
}

bool ProjectStore::createSchema()
{
    // 注意：entity_groups 不叫 groups —— GROUPS 是 SQLite 保留字
    const char *ddl[] = {
        "CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL)",
        "CREATE TABLE classes("
        " id INTEGER PRIMARY KEY, name TEXT NOT NULL DEFAULT '', long_name TEXT NOT NULL DEFAULT '',"
        " short_name TEXT NOT NULL DEFAULT '', code TEXT NOT NULL DEFAULT '',"
        " priority INTEGER NOT NULL DEFAULT 0, remark TEXT NOT NULL DEFAULT '',"
        " grade_id INTEGER NOT NULL DEFAULT -1)",
        "CREATE TABLE courses("
        " id INTEGER PRIMARY KEY, name TEXT NOT NULL DEFAULT '', long_name TEXT NOT NULL DEFAULT '',"
        " short_name TEXT NOT NULL DEFAULT '', code TEXT NOT NULL DEFAULT '',"
        " priority INTEGER NOT NULL DEFAULT 0, remark TEXT NOT NULL DEFAULT '',"
        " group_id INTEGER NOT NULL DEFAULT -1, is_major INTEGER NOT NULL DEFAULT 0,"
        " is_science INTEGER NOT NULL DEFAULT 0, even_spread INTEGER NOT NULL DEFAULT 0,"
        " progress_align_count INTEGER NOT NULL DEFAULT 2, am_pm_policy INTEGER NOT NULL DEFAULT 0)",
        "CREATE TABLE teachers("
        " id INTEGER PRIMARY KEY, name TEXT NOT NULL DEFAULT '', long_name TEXT NOT NULL DEFAULT '',"
        " short_name TEXT NOT NULL DEFAULT '', code TEXT NOT NULL DEFAULT '',"
        " priority INTEGER NOT NULL DEFAULT 0, remark TEXT NOT NULL DEFAULT '',"
        " group_id INTEGER NOT NULL DEFAULT -1, max_periods_per_day INTEGER NOT NULL DEFAULT -1,"
        " mutual_exclusion_group INTEGER NOT NULL DEFAULT -1)",
        "CREATE TABLE classrooms("
        " id INTEGER PRIMARY KEY, name TEXT NOT NULL DEFAULT '', long_name TEXT NOT NULL DEFAULT '',"
        " short_name TEXT NOT NULL DEFAULT '', code TEXT NOT NULL DEFAULT '',"
        " priority INTEGER NOT NULL DEFAULT 0, remark TEXT NOT NULL DEFAULT '',"
        " capacity INTEGER NOT NULL DEFAULT 1)",
        "CREATE TABLE entity_groups("
        " id INTEGER PRIMARY KEY, type INTEGER NOT NULL DEFAULT 0, name TEXT NOT NULL DEFAULT '')",
        "CREATE TABLE group_members("
        " group_id INTEGER NOT NULL REFERENCES entity_groups(id) ON DELETE CASCADE,"
        " member_id INTEGER NOT NULL, PRIMARY KEY(group_id, member_id))",
        "CREATE TABLE plan_entries("
        " id INTEGER PRIMARY KEY, class_id INTEGER NOT NULL, course_id INTEGER NOT NULL,"
        " weekly_periods REAL NOT NULL DEFAULT 0, chained_count INTEGER NOT NULL DEFAULT 0,"
        " chain_length INTEGER NOT NULL DEFAULT 2, morning_periods INTEGER NOT NULL DEFAULT -1,"
        " is_combined INTEGER NOT NULL DEFAULT 0, classroom_id INTEGER NOT NULL DEFAULT -1,"
        " start_week INTEGER NOT NULL DEFAULT 1, end_week INTEGER NOT NULL DEFAULT 0,"
        " sync_course_id INTEGER NOT NULL DEFAULT -1)",
        "CREATE TABLE plan_entry_teachers("
        " entry_id INTEGER NOT NULL REFERENCES plan_entries(id) ON DELETE CASCADE,"
        " teacher_id INTEGER NOT NULL, PRIMARY KEY(entry_id, teacher_id))",
        "CREATE TABLE combined_groups("
        " id INTEGER PRIMARY KEY, name TEXT NOT NULL DEFAULT '')",
        "CREATE TABLE combined_group_members("
        " group_id INTEGER NOT NULL REFERENCES combined_groups(id) ON DELETE CASCADE,"
        " class_id INTEGER NOT NULL, course_id INTEGER NOT NULL)",
        // 每条约束占多行（一个 slot 一行），constraint_id 标识同一条约束
        "CREATE TABLE no_schedule("
        " constraint_id INTEGER NOT NULL, entity_type INTEGER NOT NULL,"
        " entity_id INTEGER NOT NULL, slot INTEGER NOT NULL,"
        " PRIMARY KEY(constraint_id, slot))",
        "CREATE TABLE preassign("
        " id INTEGER PRIMARY KEY, class_id INTEGER NOT NULL, course_id INTEGER NOT NULL,"
        " slot_index INTEGER NOT NULL)",
        "CREATE TABLE teacher_activities("
        " id INTEGER PRIMARY KEY, teacher_id INTEGER NOT NULL, slot_index INTEGER NOT NULL,"
        " activity_course_id INTEGER NOT NULL DEFAULT -1)",
        "CREATE TABLE schedule_assignments("
        " class_id INTEGER NOT NULL, slot_index INTEGER NOT NULL, half INTEGER NOT NULL DEFAULT 0,"
        " course_id INTEGER NOT NULL, PRIMARY KEY(class_id, slot_index, half))",
    };
    for (const char *sql : ddl) {
        if (!exec(QString::fromLatin1(sql)))
            return false;
    }
    return exec(QStringLiteral("INSERT INTO meta(key,value) VALUES('schema_version','%1')").arg(kSchemaVersion));
}

bool ProjectStore::save(const School &school)
{
    if (!m_db.isOpen())
        return fail(QStringLiteral("数据库未打开"));
    if (!m_db.transaction())
        return fail(m_db.lastError().text());

    auto rollback = [this] { m_db.rollback(); };

    // 1. 清表：子表在前，避免违反外键
    const char *clearOrder[] = {
        "schedule_assignments", "teacher_activities", "preassign", "no_schedule",
        "combined_group_members", "combined_groups", "plan_entry_teachers", "plan_entries",
        "group_members", "entity_groups", "classrooms", "teachers", "courses", "classes",
    };
    for (const char *t : clearOrder) {
        if (!exec(QStringLiteral("DELETE FROM %1").arg(QLatin1StringView(t)))) {
            rollback();
            return false;
        }
    }

    // 2. meta：学校名与时间结构
    {
        QSqlQuery q(m_db);
        if (!q.prepare(QStringLiteral("INSERT OR REPLACE INTO meta(key,value) VALUES(?,?)"))) {
            rollback();
            return fail(q.lastError().text());
        }
        const QPair<QString, QString> rows[] = {
            {QStringLiteral("school_name"), school.name},
            {QStringLiteral("days_per_week"), QString::number(school.timeConfig.daysPerWeek)},
            {QStringLiteral("periods_per_day"), QString::number(school.timeConfig.periodsPerDay)},
            {QStringLiteral("morning_periods"), QString::number(school.timeConfig.morningPeriods)},
            {QStringLiteral("weeks_per_term"), QString::number(school.timeConfig.weeksPerTerm)},
        };
        for (const auto &row : rows) {
            q.bindValue(0, row.first);
            q.bindValue(1, textValue(row.second));
            if (!q.exec()) {
                rollback();
                return fail(q.lastError().text());
            }
        }
    }

    // 3. 主表（父表先行，子表随后，满足外键约束）
    // classes
    {
        QSqlQuery q(m_db);
        if (!q.prepare(QStringLiteral(
                "INSERT INTO classes(id,name,long_name,short_name,code,priority,remark,grade_id)"
                " VALUES(?,?,?,?,?,?,?,?)"))) {
            rollback();
            return fail(q.lastError().text());
        }
        for (const auto &c : school.classes) {
            bindNamed(q, c);
            q.addBindValue(c.gradeId);
            if (!q.exec()) {
                rollback();
                return fail(q.lastError().text());
            }
        }
    }

    // courses
    {
        QSqlQuery q(m_db);
        if (!q.prepare(QStringLiteral(
                "INSERT INTO courses(id,name,long_name,short_name,code,priority,remark,"
                " group_id,is_major,is_science,even_spread,progress_align_count,am_pm_policy)"
                " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)"))) {
            rollback();
            return fail(q.lastError().text());
        }
        for (const auto &c : school.courses) {
            bindNamed(q, c);
            q.addBindValue(c.groupId);
            q.addBindValue(c.isMajor ? 1 : 0);
            q.addBindValue(c.isScience ? 1 : 0);
            q.addBindValue(c.evenSpread ? 1 : 0);
            q.addBindValue(c.progressAlignCount);
            q.addBindValue(static_cast<int>(c.amPmPolicy));
            if (!q.exec()) {
                rollback();
                return fail(q.lastError().text());
            }
        }
    }

    // teachers
    {
        QSqlQuery q(m_db);
        if (!q.prepare(QStringLiteral(
                "INSERT INTO teachers(id,name,long_name,short_name,code,priority,remark,"
                " group_id,max_periods_per_day,mutual_exclusion_group)"
                " VALUES(?,?,?,?,?,?,?,?,?,?)"))) {
            rollback();
            return fail(q.lastError().text());
        }
        for (const auto &t : school.teachers) {
            bindNamed(q, t);
            q.addBindValue(t.groupId);
            q.addBindValue(t.maxPeriodsPerDay);
            q.addBindValue(t.mutualExclusionGroup);
            if (!q.exec()) {
                rollback();
                return fail(q.lastError().text());
            }
        }
    }

    // classrooms
    {
        QSqlQuery q(m_db);
        if (!q.prepare(QStringLiteral(
                "INSERT INTO classrooms(id,name,long_name,short_name,code,priority,remark,capacity)"
                " VALUES(?,?,?,?,?,?,?,?)"))) {
            rollback();
            return fail(q.lastError().text());
        }
        for (const auto &c : school.classrooms) {
            bindNamed(q, c);
            q.addBindValue(c.capacity);
            if (!q.exec()) {
                rollback();
                return fail(q.lastError().text());
            }
        }
    }

    // entity_groups + group_members
    {
        QSqlQuery qg(m_db), qm(m_db);
        if (!qg.prepare(QStringLiteral("INSERT INTO entity_groups(id,type,name) VALUES(?,?,?)"))
            || !qm.prepare(QStringLiteral("INSERT INTO group_members(group_id,member_id) VALUES(?,?)"))) {
            rollback();
            return fail(qg.lastError().text() + qm.lastError().text());
        }
        for (const auto &g : school.groups) {
            qg.addBindValue(g.id);
            qg.addBindValue(static_cast<int>(g.type));
            qg.addBindValue(textValue(g.name));
            if (!qg.exec()) {
                rollback();
                return fail(qg.lastError().text());
            }
            for (qint64 memberId : g.memberIds) {
                qm.addBindValue(g.id);
                qm.addBindValue(memberId);
                if (!qm.exec()) {
                    rollback();
                    return fail(qm.lastError().text());
                }
            }
        }
    }

    // plan_entries + plan_entry_teachers
    {
        QSqlQuery qe(m_db), qt(m_db);
        if (!qe.prepare(QStringLiteral(
                "INSERT INTO plan_entries(id,class_id,course_id,weekly_periods,chained_count,"
                " chain_length,morning_periods,is_combined,classroom_id,start_week,end_week,sync_course_id)"
                " VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"))
            || !qt.prepare(QStringLiteral(
                   "INSERT INTO plan_entry_teachers(entry_id,teacher_id) VALUES(?,?)"))) {
            rollback();
            return fail(qe.lastError().text() + qt.lastError().text());
        }
        for (const auto &e : school.planEntries) {
            qe.addBindValue(e.id);
            qe.addBindValue(e.classId);
            qe.addBindValue(e.courseId);
            qe.addBindValue(e.weeklyPeriods);
            qe.addBindValue(e.chainedCount);
            qe.addBindValue(e.chainLength);
            qe.addBindValue(e.morningPeriods);
            qe.addBindValue(e.isCombined ? 1 : 0);
            qe.addBindValue(e.classroomId);
            qe.addBindValue(e.startWeek);
            qe.addBindValue(e.endWeek);
            qe.addBindValue(e.syncCourseId);
            if (!qe.exec()) {
                rollback();
                return fail(qe.lastError().text());
            }
            for (qint64 teacherId : e.teacherIds) {
                qt.addBindValue(e.id);
                qt.addBindValue(teacherId);
                if (!qt.exec()) {
                    rollback();
                    return fail(qt.lastError().text());
                }
            }
        }
    }

    // combined_groups + combined_group_members
    {
        QSqlQuery qg(m_db), qm(m_db);
        if (!qg.prepare(QStringLiteral("INSERT INTO combined_groups(id,name) VALUES(?,?)"))
            || !qm.prepare(QStringLiteral(
                   "INSERT INTO combined_group_members(group_id,class_id,course_id) VALUES(?,?,?)"))) {
            rollback();
            return fail(qg.lastError().text() + qm.lastError().text());
        }
        for (const auto &g : school.combinedGroups) {
            qg.addBindValue(g.id);
            qg.addBindValue(textValue(g.name));
            if (!qg.exec()) {
                rollback();
                return fail(qg.lastError().text());
            }
            for (const auto &entry : g.entries) {
                qm.addBindValue(g.id);
                qm.addBindValue(entry.first);
                qm.addBindValue(entry.second);
                if (!qm.exec()) {
                    rollback();
                    return fail(qm.lastError().text());
                }
            }
        }
    }

    // no_schedule（一个 slot 一行）
    {
        QSqlQuery q(m_db);
        if (!q.prepare(QStringLiteral(
                "INSERT INTO no_schedule(constraint_id,entity_type,entity_id,slot) VALUES(?,?,?,?)"))) {
            rollback();
            return fail(q.lastError().text());
        }
        for (const auto &c : school.noScheduleConstraints) {
            for (int slot : c.slots) {
                q.addBindValue(c.id);
                q.addBindValue(static_cast<int>(c.entityType));
                q.addBindValue(c.entityId);
                q.addBindValue(slot);
                if (!q.exec()) {
                    rollback();
                    return fail(q.lastError().text());
                }
            }
        }
    }

    // preassign
    {
        QSqlQuery q(m_db);
        if (!q.prepare(QStringLiteral(
                "INSERT INTO preassign(id,class_id,course_id,slot_index) VALUES(?,?,?,?)"))) {
            rollback();
            return fail(q.lastError().text());
        }
        for (const auto &p : school.preAssignments) {
            q.addBindValue(p.id);
            q.addBindValue(p.classId);
            q.addBindValue(p.courseId);
            q.addBindValue(p.slotIndex);
            if (!q.exec()) {
                rollback();
                return fail(q.lastError().text());
            }
        }
    }

    // teacher_activities
    {
        QSqlQuery q(m_db);
        if (!q.prepare(QStringLiteral(
                "INSERT INTO teacher_activities(id,teacher_id,slot_index,activity_course_id)"
                " VALUES(?,?,?,?)"))) {
            rollback();
            return fail(q.lastError().text());
        }
        for (const auto &a : school.teacherActivities) {
            q.addBindValue(a.id);
            q.addBindValue(a.teacherId);
            q.addBindValue(a.slotIndex);
            q.addBindValue(a.activityCourseId);
            if (!q.exec()) {
                rollback();
                return fail(q.lastError().text());
            }
        }
    }

    // schedule_assignments
    {
        QSqlQuery q(m_db);
        if (!q.prepare(QStringLiteral(
                "INSERT INTO schedule_assignments(class_id,slot_index,half,course_id) VALUES(?,?,?,?)"))) {
            rollback();
            return fail(q.lastError().text());
        }
        for (const auto &a : school.schedule.assignments) {
            q.addBindValue(a.classId);
            q.addBindValue(a.slotIndex);
            q.addBindValue(a.half);
            q.addBindValue(a.courseId);
            if (!q.exec()) {
                rollback();
                return fail(q.lastError().text());
            }
        }
    }

    if (!m_db.commit()) {
        rollback();
        return fail(m_db.lastError().text());
    }
    return true;
}

bool ProjectStore::load(School *school)
{
    if (!m_db.isOpen())
        return fail(QStringLiteral("数据库未打开"));
    if (!school)
        return fail(QStringLiteral("load 目标为空"));

    *school = School();

    // meta
    {
        QSqlQuery q(m_db);
        if (!q.exec(QStringLiteral("SELECT key,value FROM meta")))
            return fail(q.lastError().text());
        while (q.next()) {
            const QString key = q.value(0).toString();
            const QString value = q.value(1).toString();
            if (key == QLatin1StringView("school_name"))
                school->name = value;
            else if (key == QLatin1StringView("days_per_week"))
                school->timeConfig.daysPerWeek = value.toInt();
            else if (key == QLatin1StringView("periods_per_day"))
                school->timeConfig.periodsPerDay = value.toInt();
            else if (key == QLatin1StringView("morning_periods"))
                school->timeConfig.morningPeriods = value.toInt();
            else if (key == QLatin1StringView("weeks_per_term"))
                school->timeConfig.weeksPerTerm = value.toInt();
        }
    }

    // classes
    {
        QSqlQuery q(m_db);
        if (!q.exec(QStringLiteral(
                "SELECT id,name,long_name,short_name,code,priority,remark,grade_id FROM classes ORDER BY id")))
            return fail(q.lastError().text());
        while (q.next()) {
            ClassInfo c;
            readNamed(q, c);
            c.gradeId = q.value(7).toLongLong();
            school->classes.append(c);
        }
    }

    // courses
    {
        QSqlQuery q(m_db);
        if (!q.exec(QStringLiteral(
                "SELECT id,name,long_name,short_name,code,priority,remark,group_id,is_major,"
                "is_science,even_spread,progress_align_count,am_pm_policy FROM courses ORDER BY id")))
            return fail(q.lastError().text());
        while (q.next()) {
            Course c;
            readNamed(q, c);
            c.groupId = q.value(7).toLongLong();
            c.isMajor = q.value(8).toInt() != 0;
            c.isScience = q.value(9).toInt() != 0;
            c.evenSpread = q.value(10).toInt() != 0;
            c.progressAlignCount = q.value(11).toInt();
            c.amPmPolicy = static_cast<AmPmPolicy>(q.value(12).toInt());
            school->courses.append(c);
        }
    }

    // teachers
    {
        QSqlQuery q(m_db);
        if (!q.exec(QStringLiteral(
                "SELECT id,name,long_name,short_name,code,priority,remark,group_id,"
                "max_periods_per_day,mutual_exclusion_group FROM teachers ORDER BY id")))
            return fail(q.lastError().text());
        while (q.next()) {
            Teacher t;
            readNamed(q, t);
            t.groupId = q.value(7).toLongLong();
            t.maxPeriodsPerDay = q.value(8).toInt();
            t.mutualExclusionGroup = q.value(9).toLongLong();
            school->teachers.append(t);
        }
    }

    // classrooms
    {
        QSqlQuery q(m_db);
        if (!q.exec(QStringLiteral(
                "SELECT id,name,long_name,short_name,code,priority,remark,capacity FROM classrooms ORDER BY id")))
            return fail(q.lastError().text());
        while (q.next()) {
            Classroom c;
            readNamed(q, c);
            c.capacity = q.value(7).toInt();
            school->classrooms.append(c);
        }
    }

    // entity_groups + group_members
    {
        QSqlQuery qg(m_db);
        if (!qg.exec(QStringLiteral("SELECT id,type,name FROM entity_groups ORDER BY id")))
            return fail(qg.lastError().text());
        while (qg.next()) {
            Group g;
            g.id = qg.value(0).toLongLong();
            g.type = static_cast<Group::Type>(qg.value(1).toInt());
            g.name = qg.value(2).toString();
            school->groups.append(g);
        }
        QSqlQuery qm(m_db);
        if (!qm.exec(QStringLiteral("SELECT group_id,member_id FROM group_members ORDER BY group_id,rowid")))
            return fail(qm.lastError().text());
        while (qm.next()) {
            if (Group *g = school->findGroup(qm.value(0).toLongLong()))
                g->memberIds.append(qm.value(1).toLongLong());
        }
    }

    // plan_entries + plan_entry_teachers
    {
        QSqlQuery qe(m_db);
        if (!qe.exec(QStringLiteral(
                "SELECT id,class_id,course_id,weekly_periods,chained_count,chain_length,"
                "morning_periods,is_combined,classroom_id,start_week,end_week,sync_course_id"
                " FROM plan_entries ORDER BY id")))
            return fail(qe.lastError().text());
        while (qe.next()) {
            PlanEntry e;
            e.id = qe.value(0).toLongLong();
            e.classId = qe.value(1).toLongLong();
            e.courseId = qe.value(2).toLongLong();
            e.weeklyPeriods = qe.value(3).toDouble();
            e.chainedCount = qe.value(4).toInt();
            e.chainLength = qe.value(5).toInt();
            e.morningPeriods = qe.value(6).toInt();
            e.isCombined = qe.value(7).toInt() != 0;
            e.classroomId = qe.value(8).toLongLong();
            e.startWeek = qe.value(9).toInt();
            e.endWeek = qe.value(10).toInt();
            e.syncCourseId = qe.value(11).toLongLong();
            school->planEntries.append(e);
        }
        QSqlQuery qt(m_db);
        if (!qt.exec(QStringLiteral("SELECT entry_id,teacher_id FROM plan_entry_teachers ORDER BY entry_id,rowid")))
            return fail(qt.lastError().text());
        while (qt.next()) {
            if (PlanEntry *e = school->findPlanEntry(qt.value(0).toLongLong()))
                e->teacherIds.append(qt.value(1).toLongLong());
        }
    }

    // combined_groups + combined_group_members
    {
        QSqlQuery qg(m_db);
        if (!qg.exec(QStringLiteral("SELECT id,name FROM combined_groups ORDER BY id")))
            return fail(qg.lastError().text());
        while (qg.next()) {
            CombinedGroup g;
            g.id = qg.value(0).toLongLong();
            g.name = qg.value(1).toString();
            school->combinedGroups.append(g);
        }
        QSqlQuery qm(m_db);
        if (!qm.exec(QStringLiteral(
                "SELECT group_id,class_id,course_id FROM combined_group_members ORDER BY group_id,rowid")))
            return fail(qm.lastError().text());
        while (qm.next()) {
            if (CombinedGroup *g = school->findCombinedGroup(qm.value(0).toLongLong()))
                g->entries.append({qm.value(1).toLongLong(), qm.value(2).toLongLong()});
        }
    }

    // no_schedule（按 constraint_id 聚合多行）
    {
        QSqlQuery q(m_db);
        if (!q.exec(QStringLiteral(
                "SELECT constraint_id,entity_type,entity_id,slot FROM no_schedule"
                " ORDER BY constraint_id,slot")))
            return fail(q.lastError().text());
        qint64 currentId = -1;
        while (q.next()) {
            const qint64 id = q.value(0).toLongLong();
            if (id != currentId || school->noScheduleConstraints.isEmpty()) {
                NoScheduleConstraint c;
                c.id = id;
                c.entityType = static_cast<NoScheduleConstraint::EntityType>(q.value(1).toInt());
                c.entityId = q.value(2).toLongLong();
                school->noScheduleConstraints.append(c);
                currentId = id;
            }
            school->noScheduleConstraints.last().slots.insert(q.value(3).toInt());
        }
    }

    // preassign
    {
        QSqlQuery q(m_db);
        if (!q.exec(QStringLiteral("SELECT id,class_id,course_id,slot_index FROM preassign ORDER BY id")))
            return fail(q.lastError().text());
        while (q.next()) {
            PreAssignment p;
            p.id = q.value(0).toLongLong();
            p.classId = q.value(1).toLongLong();
            p.courseId = q.value(2).toLongLong();
            p.slotIndex = q.value(3).toInt();
            school->preAssignments.append(p);
        }
    }

    // teacher_activities
    {
        QSqlQuery q(m_db);
        if (!q.exec(QStringLiteral(
                "SELECT id,teacher_id,slot_index,activity_course_id FROM teacher_activities ORDER BY id")))
            return fail(q.lastError().text());
        while (q.next()) {
            TeacherActivity a;
            a.id = q.value(0).toLongLong();
            a.teacherId = q.value(1).toLongLong();
            a.slotIndex = q.value(2).toInt();
            a.activityCourseId = q.value(3).toLongLong();
            school->teacherActivities.append(a);
        }
    }

    // schedule_assignments（按插入顺序读回）
    {
        QSqlQuery q(m_db);
        if (!q.exec(QStringLiteral(
                "SELECT class_id,slot_index,half,course_id FROM schedule_assignments ORDER BY rowid")))
            return fail(q.lastError().text());
        while (q.next()) {
            ScheduleAssignment a;
            a.classId = q.value(0).toLongLong();
            a.slotIndex = q.value(1).toInt();
            a.half = q.value(2).toInt();
            a.courseId = q.value(3).toLongLong();
            school->schedule.assignments.append(a);
        }
    }

    return true;
}

} // namespace sched
