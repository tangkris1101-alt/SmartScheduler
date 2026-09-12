#pragma once

#include "model/School.h"

#include <QString>
#include <QStringList>

// Excel 读写（QXlsx）：批量导入名单/教学计划，课表导出 xlsx
namespace sched {

// 导入报告
struct ImportReport {
    int created = 0;   // 新建条数
    int updated = 0;   // 覆盖更新条数
    int skipped = 0;   // 跳过条数（重复名且不覆盖）
    QStringList errors; // 行级错误明细："【老师】第3行：姓名为空"
    bool fatal = false; // 文件打不开/完全无法解析
};

// 从 xlsx 批量导入。五张工作表（顺序与列序即模板约定）：
// 班级 / 课程 / 老师 / 教室 / 教学计划
class ExcelImporter
{
public:
    // 生成导入模板（表头 + 示例行 + 说明列）
    static bool writeImportTemplate(const QString &path, QString *error = nullptr);
    // overwrite=false 重复名跳过，true 覆盖更新；报告含行级错误
    static ImportReport importFile(const QString &path, School *school, bool overwrite);
};

// 课表导出 xlsx：按所选班级/老师各一张表 + 可选总课表
class ExcelExporter
{
public:
    static bool exportTimetables(const QString &path, const School &school,
                                 const QVector<qint64> &classIds, const QVector<qint64> &teacherIds,
                                 bool masterSheet, QString *error = nullptr);
};

} // namespace sched
