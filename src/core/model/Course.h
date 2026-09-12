#pragma once

#include "NamedEntity.h"

namespace sched {

// 课程的上下午分配策略
enum class AmPmPolicy {
    Auto = 0, // 自动
    Morning,  // 优先安排上午
    Afternoon // 优先安排下午
};

// 课程
struct Course : NamedEntity {
    qint64 groupId = -1;        // 所属教研组（Group::CourseGroup id）
    bool isMajor = false;       // 是否主课
    bool isScience = false;     // 是否理科（文/理搭配用）
    bool evenSpread = false;    // 是否要求周内平均分布
    int progressAlignCount = 2; // 进度一致的连续班级数
    AmPmPolicy amPmPolicy = AmPmPolicy::Auto;

    bool operator==(const Course &) const = default;
};

} // namespace sched
