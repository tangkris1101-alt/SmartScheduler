#pragma once

#include "ClassInfo.h"
#include "Classroom.h"
#include "CombinedGroup.h"
#include "Constraints.h"
#include "Course.h"
#include "Group.h"
#include "PlanEntry.h"
#include "Schedule.h"
#include "Teacher.h"
#include "TimeConfig.h"
#include "TimeSyncGroup.h"

#include <QString>
#include <QVector>

namespace sched {

// 学校：领域模型聚合根，承载一所学校一学期的全部排课相关数据
class School
{
public:
    QString name;
    TimeConfig timeConfig;

    QVector<ClassInfo> classes;
    QVector<Course> courses;
    QVector<Teacher> teachers;
    QVector<Classroom> classrooms;
    QVector<Group> groups;
    QVector<PlanEntry> planEntries;
    QVector<CombinedGroup> combinedGroups;
    QVector<TimeSyncGroup> timeSyncGroups; // 同时段组（走班）
    QVector<NoScheduleConstraint> noScheduleConstraints;
    QVector<PreAssignment> preAssignments;
    QVector<TeacherActivity> teacherActivities;
    Schedule schedule;

    // ---- 按 id 查找（找不到返回 nullptr）----
    ClassInfo *findClass(qint64 id);
    Course *findCourse(qint64 id);
    Teacher *findTeacher(qint64 id);
    Classroom *findClassroom(qint64 id);
    Group *findGroup(qint64 id);
    PlanEntry *findPlanEntry(qint64 id);
    CombinedGroup *findCombinedGroup(qint64 id);
    TimeSyncGroup *findTimeSyncGroup(qint64 id);
    const ClassInfo *findClass(qint64 id) const;
    const Course *findCourse(qint64 id) const;
    const Teacher *findTeacher(qint64 id) const;
    const Classroom *findClassroom(qint64 id) const;
    const Group *findGroup(qint64 id) const;
    const PlanEntry *findPlanEntry(qint64 id) const;
    const CombinedGroup *findCombinedGroup(qint64 id) const;
    const TimeSyncGroup *findTimeSyncGroup(qint64 id) const;

    // ---- 名单新增（id 为 -1 时自动分配，返回最终 id）----
    qint64 addClass(ClassInfo c);
    qint64 addCourse(Course c);
    qint64 addTeacher(Teacher t);
    qint64 addClassroom(Classroom c);
    qint64 addGroup(Group g);
    qint64 addPlanEntry(PlanEntry e);
    qint64 addCombinedGroup(CombinedGroup g);
    qint64 addTimeSyncGroup(TimeSyncGroup g);
    qint64 addNoScheduleConstraint(NoScheduleConstraint c);
    qint64 addPreAssignment(PreAssignment p);
    qint64 addTeacherActivity(TeacherActivity a);

    // ---- 名单删除（返回是否找到；实体删除会级联清理引用它的数据）----
    bool removeClass(qint64 id);
    bool removeCourse(qint64 id);
    bool removeTeacher(qint64 id);
    bool removeClassroom(qint64 id);
    bool removeGroup(qint64 id);
    bool removePlanEntry(qint64 id);
    bool removeCombinedGroup(qint64 id);
    bool removeTimeSyncGroup(qint64 id);
    bool removeNoScheduleConstraint(qint64 id);
    bool removePreAssignment(qint64 id);
    bool removeTeacherActivity(qint64 id);

    // 某老师在某节次的所有占用（由教学计划推导老师 -> 班级/课程 的任课关系）
    QVector<ScheduleAssignment> teacherOccupancy(qint64 teacherId, int slotIndex) const;
};

} // namespace sched
