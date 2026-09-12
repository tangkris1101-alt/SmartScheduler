#pragma once

#include "model/School.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class AppController;

// 打印与导出控制器：QPainter 渲染课表，输出 QPrinter / PDF / PNG / Excel
// scope: "class" | "teacher" | "room" | "master"；ids 为空 = 该类型全部
// options: landscape(bool) / tablesPerPage(1|2) / fontSize(int) / showTeachers(bool)
//          lunchLine(bool) / showLock(bool) / headerNote(QString)
class PrintController : public QObject
{
    Q_OBJECT

public:
    explicit PrintController(AppController *app, QObject *parent = nullptr);

    Q_INVOKABLE bool exportPdf(const QString &pathOrUrl, const QString &scope,
                               const QList<qint64> &ids, const QVariantMap &options);
    Q_INVOKABLE bool printNative(const QString &scope, const QList<qint64> &ids,
                                 const QVariantMap &options);
    Q_INVOKABLE bool exportPngs(const QString &dirOrUrl, const QString &scope,
                                const QList<qint64> &ids, const QVariantMap &options);
    Q_INVOKABLE bool exportExcel(const QString &pathOrUrl, const QString &scope,
                                 const QList<qint64> &ids);
    // 渲染第一页预览为临时 PNG，返回带时间戳的 file:// URL（QML Image 直接加载）
    Q_INVOKABLE QString renderPreview(const QString &scope, const QList<qint64> &ids,
                                      const QVariantMap &options);
    Q_INVOKABLE QString lastError() const { return m_lastError; }

private:
    AppController *m_app;
    QString m_lastError;
};
