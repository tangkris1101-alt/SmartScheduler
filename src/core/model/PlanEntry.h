#pragma once

#include <QVector>
#include <QtGlobal>

namespace sched {

// 教学计划条目：某班某课程的周课时安排
struct PlanEntry {
    qint64 id = -1;
    qint64 classId = -1;               // 班级
    qint64 courseId = -1;              // 课程
    QVector<qint64> teacherIds;        // 任课老师（可多个，老师组展开后逐一列入）
    double weeklyPeriods = 0;          // 周课时数，0.5 表示单双周各一半
    int chainedCount = 0;              // 连课次数
    int chainLength = 2;               // 每次连课节数
    int morningPeriods = -1;           // 要求排在上午的节数，-1 自动
    int earlyPeriods = -1;             // 要求尽量排在前 N 节内，-1 不限（软约束）
    bool isCombined = false;           // 是否合班上课
    qint64 classroomId = -1;           // 指定公共教室/场地，-1 表示用本班教室
    int startWeek = 1;                 // 起始周
    int endWeek = 0;                   // 结束周，0 表示到学期末
    qint64 syncCourseId = -1;          // 同步课程（与该课程同时同节排课），-1 无
    int progressGroup = -1;            // 进度一致组号（同老师同课同组要求连排），-1 未分组
    // 不计课时、不挂老师（如高中第 9 节的体育活动）：排课时它不占老师、不产生老师撞课，
    // 发布给老师看的个人课表不显示这一格，也不计入老师的周课时
    bool noLoad = false;

    bool operator==(const PlanEntry &) const = default;
};

} // namespace sched
