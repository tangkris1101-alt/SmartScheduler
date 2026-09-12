#pragma once

#include <QSet>
#include <QtGlobal>

namespace sched {

// 不排课限制：某实体（班级/课程/老师/教室）在指定节次不允许排课
struct NoScheduleConstraint {
    enum class EntityType {
        Class = 0,
        Course,
        Teacher,
        Classroom
    };

    qint64 id = -1;
    EntityType entityType = EntityType::Class;
    qint64 entityId = -1;
    QSet<int> slots; // 禁止排课的 slot 序号集合

    bool operator==(const NoScheduleConstraint &) const = default;
};

// 固定预排：某班某课程必须排在指定节次
struct PreAssignment {
    qint64 id = -1;
    qint64 classId = -1;
    qint64 courseId = -1;
    int slotIndex = -1;

    bool operator==(const PreAssignment &) const = default;
};

// 老师活动时间：老师在某节次参加某「活动课程」（占用该老师但不占班级）
struct TeacherActivity {
    qint64 id = -1;
    qint64 teacherId = -1;
    int slotIndex = -1;
    qint64 activityCourseId = -1;

    bool operator==(const TeacherActivity &) const = default;
};

} // namespace sched
