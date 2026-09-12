#pragma once

#include "NamedEntity.h"

namespace sched {

// 老师
struct Teacher : NamedEntity {
    qint64 groupId = -1;             // 所属教研组（Group::TeacherGroup id）
    int maxPeriodsPerDay = -1;       // 每天最多上课节数，-1 表示不限
    qint64 mutualExclusionGroup = -1; // 互斥组 id（同组老师同时只能上一人），-1 表示无

    bool operator==(const Teacher &) const = default;
};

} // namespace sched
