#include "Schedule.h"

#include <algorithm>

namespace sched {

static bool sameKey(const ScheduleAssignment &a, qint64 classId, int slotIndex, int half)
{
    return a.classId == classId && a.slotIndex == slotIndex && a.half == half;
}

void Schedule::setAssignment(qint64 classId, int slotIndex, int half, qint64 courseId)
{
    removeAssignment(classId, slotIndex, half);
    assignments.append(ScheduleAssignment{classId, slotIndex, half, courseId});
}

bool Schedule::removeAssignment(qint64 classId, int slotIndex, int half)
{
    auto it = std::remove_if(assignments.begin(), assignments.end(), [&](const ScheduleAssignment &a) {
        return sameKey(a, classId, slotIndex, half);
    });
    bool removed = it != assignments.end();
    assignments.erase(it, assignments.end());
    return removed;
}

std::optional<qint64> Schedule::courseAt(qint64 classId, int slotIndex, int half) const
{
    for (const auto &a : assignments) {
        if (a.classId != classId || a.slotIndex != slotIndex)
            continue;
        // 查询单/双周时，整节占用同样命中；反之整节查询只命中整节
        if (half == 0 ? a.half == 0 : (a.half == 0 || a.half == half))
            return a.courseId;
    }
    return std::nullopt;
}

QVector<ScheduleAssignment> Schedule::assignmentsAt(int slotIndex) const
{
    QVector<ScheduleAssignment> result;
    for (const auto &a : assignments) {
        if (a.slotIndex == slotIndex)
            result.append(a);
    }
    return result;
}

QVector<ScheduleAssignment> Schedule::assignmentsForClass(qint64 classId) const
{
    QVector<ScheduleAssignment> result;
    for (const auto &a : assignments) {
        if (a.classId == classId)
            result.append(a);
    }
    return result;
}

QVector<ScheduleAssignment> Schedule::assignmentsForCourse(qint64 courseId) const
{
    QVector<ScheduleAssignment> result;
    for (const auto &a : assignments) {
        if (a.courseId == courseId)
            result.append(a);
    }
    return result;
}

} // namespace sched
