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

    bool operator==(const Group &) const = default;
};

} // namespace sched
