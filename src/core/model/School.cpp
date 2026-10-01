#include "School.h"

#include <algorithm>

namespace sched {

namespace {

template<typename T>
qint64 allocateId(const QVector<T> &items)
{
    qint64 maxId = 0;
    for (const auto &it : items)
        maxId = std::max(maxId, it.id);
    return maxId + 1;
}

template<typename T>
T *findById(QVector<T> &items, qint64 id)
{
    for (auto &it : items) {
        if (it.id == id)
            return &it;
    }
    return nullptr;
}

template<typename T>
const T *findByIdConst(const QVector<T> &items, qint64 id)
{
    for (const auto &it : items) {
        if (it.id == id)
            return &it;
    }
    return nullptr;
}

template<typename T>
bool eraseById(QVector<T> &items, qint64 id)
{
    auto it = std::remove_if(items.begin(), items.end(), [&](const T &x) { return x.id == id; });
    bool removed = it != items.end();
    items.erase(it, items.end());
    return removed;
}

template<typename T, typename Pred>
void eraseIf(QVector<T> &items, Pred pred)
{
    auto it = std::remove_if(items.begin(), items.end(), pred);
    items.erase(it, items.end());
}

} // namespace

// ---- 查找 ----

ClassInfo *School::findClass(qint64 id) { return findById(classes, id); }
Course *School::findCourse(qint64 id) { return findById(courses, id); }
Teacher *School::findTeacher(qint64 id) { return findById(teachers, id); }
Classroom *School::findClassroom(qint64 id) { return findById(classrooms, id); }
Group *School::findGroup(qint64 id) { return findById(groups, id); }
PlanEntry *School::findPlanEntry(qint64 id) { return findById(planEntries, id); }
CombinedGroup *School::findCombinedGroup(qint64 id) { return findById(combinedGroups, id); }
TimeSyncGroup *School::findTimeSyncGroup(qint64 id) { return findById(timeSyncGroups, id); }

const ClassInfo *School::findClass(qint64 id) const { return findByIdConst(classes, id); }
const Course *School::findCourse(qint64 id) const { return findByIdConst(courses, id); }
const Teacher *School::findTeacher(qint64 id) const { return findByIdConst(teachers, id); }
const Classroom *School::findClassroom(qint64 id) const { return findByIdConst(classrooms, id); }
const Group *School::findGroup(qint64 id) const { return findByIdConst(groups, id); }
const PlanEntry *School::findPlanEntry(qint64 id) const { return findByIdConst(planEntries, id); }
const CombinedGroup *School::findCombinedGroup(qint64 id) const { return findByIdConst(combinedGroups, id); }
const TimeSyncGroup *School::findTimeSyncGroup(qint64 id) const { return findByIdConst(timeSyncGroups, id); }

// ---- 新增 ----

qint64 School::addClass(ClassInfo c)
{
    if (c.id < 0)
        c.id = allocateId(classes);
    classes.append(c);
    return c.id;
}

qint64 School::addCourse(Course c)
{
    if (c.id < 0)
        c.id = allocateId(courses);
    courses.append(c);
    return c.id;
}

qint64 School::addTeacher(Teacher t)
{
    if (t.id < 0)
        t.id = allocateId(teachers);
    teachers.append(t);
    return t.id;
}

qint64 School::addClassroom(Classroom c)
{
    if (c.id < 0)
        c.id = allocateId(classrooms);
    classrooms.append(c);
    return c.id;
}

qint64 School::addGroup(Group g)
{
    if (g.id < 0)
        g.id = allocateId(groups);
    groups.append(g);
    return g.id;
}

qint64 School::addPlanEntry(PlanEntry e)
{
    if (e.id < 0)
        e.id = allocateId(planEntries);
    planEntries.append(e);
    return e.id;
}

qint64 School::addCombinedGroup(CombinedGroup g)
{
    if (g.id < 0)
        g.id = allocateId(combinedGroups);
    combinedGroups.append(g);
    return g.id;
}

qint64 School::addTimeSyncGroup(TimeSyncGroup g)
{
    if (g.id < 0)
        g.id = allocateId(timeSyncGroups);
    timeSyncGroups.append(g);
    return g.id;
}

qint64 School::addNoScheduleConstraint(NoScheduleConstraint c)
{
    if (c.id < 0)
        c.id = allocateId(noScheduleConstraints);
    noScheduleConstraints.append(c);
    return c.id;
}

qint64 School::addPreAssignment(PreAssignment p)
{
    if (p.id < 0)
        p.id = allocateId(preAssignments);
    preAssignments.append(p);
    return p.id;
}

qint64 School::addTeacherActivity(TeacherActivity a)
{
    if (a.id < 0)
        a.id = allocateId(teacherActivities);
    teacherActivities.append(a);
    return a.id;
}

// ---- 删除（含级联清理）----

bool School::removeClass(qint64 id)
{
    if (!eraseById(classes, id))
        return false;
    eraseIf(planEntries, [&](const PlanEntry &e) { return e.classId == id; });
    eraseIf(preAssignments, [&](const PreAssignment &p) { return p.classId == id; });
    eraseIf(noScheduleConstraints, [&](const NoScheduleConstraint &c) {
        return c.entityType == NoScheduleConstraint::EntityType::Class && c.entityId == id;
    });
    eraseIf(schedule.assignments, [&](const ScheduleAssignment &a) { return a.classId == id; });
    // 合班组中移除该班，空的合班组一并删除
    for (auto &g : combinedGroups)
        eraseIf(g.entries, [&](const QPair<qint64, qint64> &e) { return e.first == id; });
    eraseIf(combinedGroups, [](const CombinedGroup &g) { return g.entries.isEmpty(); });
    // 同时段组同理
    for (auto &g : timeSyncGroups)
        eraseIf(g.entries, [&](const QPair<qint64, qint64> &e) { return e.first == id; });
    eraseIf(timeSyncGroups, [](const TimeSyncGroup &g) { return g.entries.isEmpty(); });
    // 年级名单成员中移除
    for (auto &g : groups) {
        if (g.type == Group::Type::Grade)
            g.memberIds.removeAll(id);
    }
    return true;
}

bool School::removeCourse(qint64 id)
{
    if (!eraseById(courses, id))
        return false;
    eraseIf(planEntries, [&](const PlanEntry &e) { return e.courseId == id; });
    for (auto &e : planEntries) {
        if (e.syncCourseId == id)
            e.syncCourseId = -1;
    }
    eraseIf(preAssignments, [&](const PreAssignment &p) { return p.courseId == id; });
    eraseIf(teacherActivities, [&](const TeacherActivity &a) { return a.activityCourseId == id; });
    eraseIf(noScheduleConstraints, [&](const NoScheduleConstraint &c) {
        return c.entityType == NoScheduleConstraint::EntityType::Course && c.entityId == id;
    });
    eraseIf(schedule.assignments, [&](const ScheduleAssignment &a) { return a.courseId == id; });
    for (auto &g : combinedGroups)
        eraseIf(g.entries, [&](const QPair<qint64, qint64> &e) { return e.second == id; });
    eraseIf(combinedGroups, [](const CombinedGroup &g) { return g.entries.isEmpty(); });
    for (auto &g : timeSyncGroups)
        eraseIf(g.entries, [&](const QPair<qint64, qint64> &e) { return e.second == id; });
    eraseIf(timeSyncGroups, [](const TimeSyncGroup &g) { return g.entries.isEmpty(); });
    for (auto &g : groups) {
        if (g.type == Group::Type::CourseGroup)
            g.memberIds.removeAll(id);
    }
    return true;
}

bool School::removeTeacher(qint64 id)
{
    if (!eraseById(teachers, id))
        return false;
    // 教学计划中摘掉该老师（条目保留，允许暂未定老师）
    for (auto &e : planEntries)
        e.teacherIds.removeAll(id);
    eraseIf(teacherActivities, [&](const TeacherActivity &a) { return a.teacherId == id; });
    eraseIf(noScheduleConstraints, [&](const NoScheduleConstraint &c) {
        return c.entityType == NoScheduleConstraint::EntityType::Teacher && c.entityId == id;
    });
    for (auto &g : groups) {
        if (g.type == Group::Type::TeacherGroup)
            g.memberIds.removeAll(id);
    }
    return true;
}

bool School::removeClassroom(qint64 id)
{
    if (!eraseById(classrooms, id))
        return false;
    for (auto &e : planEntries) {
        if (e.classroomId == id)
            e.classroomId = -1;
    }
    eraseIf(noScheduleConstraints, [&](const NoScheduleConstraint &c) {
        return c.entityType == NoScheduleConstraint::EntityType::Classroom && c.entityId == id;
    });
    return true;
}

bool School::removeGroup(qint64 id)
{
    if (!eraseById(groups, id))
        return false;
    // 清除实体上对归类名单的引用
    for (auto &c : classes) {
        if (c.gradeId == id)
            c.gradeId = -1;
    }
    for (auto &c : courses) {
        if (c.groupId == id)
            c.groupId = -1;
    }
    for (auto &t : teachers) {
        if (t.groupId == id)
            t.groupId = -1;
    }
    return true;
}

bool School::removePlanEntry(qint64 id) { return eraseById(planEntries, id); }
bool School::removeCombinedGroup(qint64 id) { return eraseById(combinedGroups, id); }
bool School::removeTimeSyncGroup(qint64 id) { return eraseById(timeSyncGroups, id); }
bool School::removeNoScheduleConstraint(qint64 id) { return eraseById(noScheduleConstraints, id); }
bool School::removePreAssignment(qint64 id) { return eraseById(preAssignments, id); }
bool School::removeTeacherActivity(qint64 id) { return eraseById(teacherActivities, id); }

// ---- 冲突查询 ----

QVector<ScheduleAssignment> School::teacherOccupancy(qint64 teacherId, int slotIndex) const
{
    QVector<ScheduleAssignment> result;
    for (const auto &e : planEntries) {
        if (!e.teacherIds.contains(teacherId))
            continue;
        for (const auto &a : schedule.assignments) {
            if (a.classId == e.classId && a.courseId == e.courseId && a.slotIndex == slotIndex)
                result.append(a);
        }
    }
    // 老师活动时间也算占用
    for (const auto &act : teacherActivities) {
        if (act.teacherId == teacherId && act.slotIndex == slotIndex)
            result.append(ScheduleAssignment{-1, slotIndex, 0, act.activityCourseId});
    }
    return result;
}

} // namespace sched
