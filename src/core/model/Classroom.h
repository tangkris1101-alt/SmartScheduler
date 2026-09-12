#pragma once

#include "NamedEntity.h"

namespace sched {

// 公共教室 / 场地
struct Classroom : NamedEntity {
    int capacity = 1; // 同一节次最多可容纳的班级数

    bool operator==(const Classroom &) const = default;
};

} // namespace sched
