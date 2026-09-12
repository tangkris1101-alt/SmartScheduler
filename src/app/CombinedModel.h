#pragma once

#include "model/School.h"

#include <QAbstractListModel>
#include <QVariantList>

class AppController;

// 合班组模型：合班组列表 + 校验的选入/移出/解散
// 校验规则：组内成员课程必须一致，且老师有交集（否则阻止并返回错误文案）
class CombinedModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        MemberCountRole,
        MemberTextRole,   // "1班·体育、2班·体育"
        CourseNameRole,
        TeacherNamesRole, // 成员老师交集的姓名
        MemberListRole,   // [{classId, courseId, label}] 逐成员（供移出操作）
        ValidRole         // 组当前是否满足校验
    };

    explicit CombinedModel(AppController *app, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;

    Q_INVOKABLE qint64 addGroup(const QString &name);
    Q_INVOKABLE bool removeRow(int row); // 解散组（条目保留，仅解除关联）
    Q_INVOKABLE qint64 idAt(int row) const;
    Q_INVOKABLE QVariant get(int row, const QString &roleName) const;
    // 选入课程进组；成功返回空串，失败返回错误原因
    Q_INVOKABLE QString addEntry(qint64 groupId, qint64 entryId);
    Q_INVOKABLE void removeEntry(qint64 groupId, qint64 classId, qint64 courseId);
    // 未关联的合班课条目（isCombined=true 且不在任何组）：[{entryId, text, courseId}]
    Q_INVOKABLE QVariantList unlinkedEntries() const;

Q_SIGNALS:
    void countChanged();

private:
    void fullReset();
    QString validateAdd(const sched::CombinedGroup &g, const sched::PlanEntry &e, QString *error) const;

    AppController *m_app;
};
