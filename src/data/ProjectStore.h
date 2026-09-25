#pragma once

#include "model/School.h"

#include <QSqlDatabase>
#include <QString>

namespace sched {

// 工程存储：将 School 聚合根整体读写到一个 SQLite 文件（.spk）
// 注意：一个实例独占一条数据库连接，不可跨线程使用。
class ProjectStore
{
public:
    ProjectStore();
    ~ProjectStore();

    ProjectStore(const ProjectStore &) = delete;
    ProjectStore &operator=(const ProjectStore &) = delete;

    // 新建工程文件：已存在则覆盖，建全部表并写入 schema 版本
    bool createNew(const QString &path);
    // 打开已有工程文件
    bool open(const QString &path);
    void close();
    bool isOpen() const;

    // 整体保存：事务内清表重写（单学期数据量小，简单可靠）
    bool save(const School &school);
    // 整体载入（覆盖 *school 原有内容）
    bool load(School *school);

    // 项目级设置（存 meta 表，随 save 持久化；需数据库已打开）
    bool setSetting(const QString &key, const QVariant &value);
    QVariant setting(const QString &key, const QVariant &def = {}) const;

    QString lastError() const { return m_lastError; }

private:
    bool createSchema();
    bool migrate(int fromVersion);
    bool exec(const QString &sql);
    bool fail(const QString &message);

    QSqlDatabase m_db;
    QString m_connName;
    QString m_lastError;
};

} // namespace sched
