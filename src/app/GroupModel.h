#pragma once

#include "model/School.h"

#include <QAbstractListModel>
#include <QVariantList>

class AppController;

// 归类名单（年级 / 老师教研组 / 课程教研组）模型：CRUD + 成员增删
class GroupModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        TypeRole,
        TypeNameRole,
        MemberCountRole,
        MemberNamesRole
    };

    explicit GroupModel(AppController *app, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;

    Q_INVOKABLE qint64 addGroup(int type, const QString &name);
    Q_INVOKABLE bool removeRow(int row);
    Q_INVOKABLE QVariant get(int row, const QString &roleName) const;
    Q_INVOKABLE bool set(int row, const QString &roleName, const QVariant &value);
    Q_INVOKABLE qint64 idAt(int row) const;
    Q_INVOKABLE QString typeName(int type) const;

    // 成员管理：按组类型返回候选实体 [{id, name}]
    Q_INVOKABLE QVariantList candidates(qint64 groupId) const;
    Q_INVOKABLE bool isMember(qint64 groupId, qint64 memberId) const;
    Q_INVOKABLE void toggleMember(qint64 groupId, qint64 memberId);

Q_SIGNALS:
    void countChanged();

private:
    void fullReset();
    int rowOfGroup(qint64 groupId) const;
    QString memberNames(const sched::Group &g) const;

    AppController *m_app;
};
