#pragma once

#include <QPair>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace sched {

// 同时段组：组内「班级-课程」的对应课次必须排在同一个时段（同一 slot、同一半周）。
// 用于跨班走班（不同课程同时上，如 16班生物 = 14班政治 = 1班地理）与
// 「同一个老师一次带几个班」之外的其它同步场景。
// 语义：按课次顺序配对——组内每个成员的第 k 次课与其它成员的第 k 次课同节。
struct TimeSyncGroup {
    qint64 id = -1;
    QString name;
    QVector<QPair<qint64, qint64>> entries; // (classId, courseId)

    bool operator==(const TimeSyncGroup &) const = default;
};

} // namespace sched
