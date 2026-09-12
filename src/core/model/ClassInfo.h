#pragma once

#include "NamedEntity.h"

namespace sched {

// 班级
struct ClassInfo : NamedEntity {
    qint64 gradeId = -1; // 所属年级（Group::Grade 名单 id），-1 表示未归类

    bool operator==(const ClassInfo &) const = default;
};

} // namespace sched
