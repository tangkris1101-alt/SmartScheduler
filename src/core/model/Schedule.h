#pragma once

#include <QVector>
#include <QtGlobal>
#include <optional>

namespace sched {

// 排课结果条目：键为 (classId, slotIndex, half)
// half: 0=整节（单双周同上），1=仅单周，2=仅双周
struct ScheduleAssignment {
    qint64 classId = -1;
    int slotIndex = -1;
    int half = 0;
    qint64 courseId = -1;

    bool operator==(const ScheduleAssignment &) const = default;
};

// 排课结果（一张总课表）
class Schedule
{
public:
    QVector<ScheduleAssignment> assignments;

    // 写入/覆盖一条排课结果（同键先删后插）
    void setAssignment(qint64 classId, int slotIndex, int half, qint64 courseId);
    bool removeAssignment(qint64 classId, int slotIndex, int half);

    // 查询某班在某节次的课程；half 传 1/2（单/双周）时整节（0）也算占用
    std::optional<qint64> courseAt(qint64 classId, int slotIndex, int half = 0) const;

    // 某节次全校所有占用（冲突查询基础）
    QVector<ScheduleAssignment> assignmentsAt(int slotIndex) const;
    QVector<ScheduleAssignment> assignmentsForClass(qint64 classId) const;
    QVector<ScheduleAssignment> assignmentsForCourse(qint64 courseId) const;

    void clear() { assignments.clear(); }
};

} // namespace sched
