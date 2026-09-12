#pragma once

#include <QPair>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace sched {

// 合班组：若干「班级-课程」组合在同一时间同一教室上课
struct CombinedGroup {
    qint64 id = -1;
    QString name;
    QVector<QPair<qint64, qint64>> entries; // (classId, courseId)

    bool operator==(const CombinedGroup &) const = default;
};

} // namespace sched
