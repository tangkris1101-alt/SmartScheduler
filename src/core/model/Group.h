#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>

namespace sched {

// 归类名单（年级 / 教研组），成员为对应实体的 id 列表
struct Group {
    enum class Type {
        Grade = 0,   // 年级（成员为班级）
        TeacherGroup, // 教研组（成员为老师）
        CourseGroup   // 课程教研组（成员为课程）
    };

    qint64 id = -1;
    Type type = Type::Grade;
    QString name;
    QVector<qint64> memberIds;
    // 年级组作息遮罩（-1 未设置）：记录该组上次应用的「每周天数 / 每天节数」范围，
    // 范围外的槽位由 applyGradeMask 批量设为组内班级的不排，用于精确还原
    int maskDays = -1;
    int maskPeriods = -1;

    bool operator==(const Group &) const = default;
};

} // namespace sched
