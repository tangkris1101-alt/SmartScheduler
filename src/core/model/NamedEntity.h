#pragma once

#include <QString>
#include <QtGlobal>

namespace sched {

// 具名名单（班级/课程/老师/教室）的公共基类
struct NamedEntity {
    qint64 id = -1; // 持久化 id，-1 表示尚未入库
    QString name;      // 名称
    QString longName;  // 全称
    QString shortName; // 简称（课表格子内显示用）
    QString code;      // 拼音检索码
    int priority = 0;  // 优先次序（排课时的先后权重）
    QString remark;    // 备注

    bool operator==(const NamedEntity &) const = default;
};

} // namespace sched
