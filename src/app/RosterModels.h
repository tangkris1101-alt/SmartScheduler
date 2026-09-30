#pragma once

#include "model/School.h"

#include <QAbstractListModel>

class AppController;

// 具名名单模型基类：班级/课程/老师/教室共用。
// 内置过滤（filter 属性按 名称/简称/拼音码 模糊匹配），视图行 → 源行映射。
// 子类实现学校名单的存取；扩展字段通过 extraData/extraSet/extraRoleNames 挂钩。
class NamedEntityModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        LongNameRole,
        ShortNameRole,
        CodeRole,
        PriorityRole,
        RemarkRole,
        ExtraRoleBase // 子类扩展角色从这里开始
    };

    explicit NamedEntityModel(AppController *app, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QHash<int, QByteArray> roleNames() const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    int count() const { return m_view.size(); }

    // QML 友好的按角色名读写
    Q_INVOKABLE QVariant get(int row, const QString &roleName) const;
    Q_INVOKABLE bool set(int row, const QString &roleName, const QVariant &value);
    Q_INVOKABLE qint64 appendRow(); // 追加一行（默认名），返回新 id
    // 批量追加：忽略空行、名称去首尾空白；返回实际新增行数
    Q_INVOKABLE int appendRows(const QStringList &names);
    Q_INVOKABLE bool removeRow(int row); // 走 School 级联删除
    Q_INVOKABLE qint64 idAt(int row) const;

Q_SIGNALS:
    void filterChanged();
    void countChanged();

protected:
    virtual int sourceCount() const = 0;
    virtual sched::NamedEntity *entityAt(int sourceRow) const = 0;
    virtual sched::NamedEntity *createEntity() = 0; // 追加到学校名单并返回指针
    virtual bool removeEntity(qint64 id) = 0;
    virtual QVariant extraData(int role, const sched::NamedEntity &e) const;
    virtual bool extraSet(int role, sched::NamedEntity &e, const QVariant &value);
    virtual QHash<int, QByteArray> extraRoleNames() const { return {}; }
    virtual bool matchesFilter(const sched::NamedEntity &e) const;

    sched::School *school() const;
    int roleForName(const QString &name) const;
    void fullReset();

    AppController *m_app;
    QString m_filter;
    QVector<int> m_view; // 可见行 → 源行
};

class ClassListModel : public NamedEntityModel
{
    Q_OBJECT
public:
    enum ExtraRole { GradeIdRole = ExtraRoleBase, GradeNameRole };
    explicit ClassListModel(AppController *app, QObject *parent = nullptr);

protected:
    int sourceCount() const override;
    sched::NamedEntity *entityAt(int sourceRow) const override;
    sched::NamedEntity *createEntity() override;
    bool removeEntity(qint64 id) override;
    QVariant extraData(int role, const sched::NamedEntity &e) const override;
    bool extraSet(int role, sched::NamedEntity &e, const QVariant &value) override;
    QHash<int, QByteArray> extraRoleNames() const override;
};

class CourseListModel : public NamedEntityModel
{
    Q_OBJECT
public:
    enum ExtraRole {
        GroupIdRole = ExtraRoleBase,
        IsMajorRole,
        IsScienceRole,
        EvenSpreadRole,
        ProgressAlignCountRole,
        AmPmPolicyRole,
        AmPmTextRole
    };
    explicit CourseListModel(AppController *app, QObject *parent = nullptr);

protected:
    int sourceCount() const override;
    sched::NamedEntity *entityAt(int sourceRow) const override;
    sched::NamedEntity *createEntity() override;
    bool removeEntity(qint64 id) override;
    QVariant extraData(int role, const sched::NamedEntity &e) const override;
    bool extraSet(int role, sched::NamedEntity &e, const QVariant &value) override;
    QHash<int, QByteArray> extraRoleNames() const override;
};

class TeacherListModel : public NamedEntityModel
{
    Q_OBJECT
public:
    enum ExtraRole {
        GroupIdRole = ExtraRoleBase,
        MaxPeriodsPerDayRole,
        MutualExclusionGroupRole
    };
    explicit TeacherListModel(AppController *app, QObject *parent = nullptr);

protected:
    int sourceCount() const override;
    sched::NamedEntity *entityAt(int sourceRow) const override;
    sched::NamedEntity *createEntity() override;
    bool removeEntity(qint64 id) override;
    QVariant extraData(int role, const sched::NamedEntity &e) const override;
    bool extraSet(int role, sched::NamedEntity &e, const QVariant &value) override;
    QHash<int, QByteArray> extraRoleNames() const override;
};

class ClassroomListModel : public NamedEntityModel
{
    Q_OBJECT
public:
    enum ExtraRole { CapacityRole = ExtraRoleBase };
    explicit ClassroomListModel(AppController *app, QObject *parent = nullptr);

protected:
    int sourceCount() const override;
    sched::NamedEntity *entityAt(int sourceRow) const override;
    sched::NamedEntity *createEntity() override;
    bool removeEntity(qint64 id) override;
    QVariant extraData(int role, const sched::NamedEntity &e) const override;
    bool extraSet(int role, sched::NamedEntity &e, const QVariant &value) override;
    QHash<int, QByteArray> extraRoleNames() const override;
};
