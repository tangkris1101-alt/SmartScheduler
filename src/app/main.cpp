#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <algorithm>

#include "AdjustController.h"
#include "AppController.h"
#include "CombinedModel.h"
#include "GroupModel.h"
#include "PlanModels.h"
#include "PrintController.h"
#include "RosterModels.h"
#include "Theme.h"

namespace {

QMutex g_logMutex;
QString g_logPath;

// 统一日志：写入 AppDataLocation/logs/smartscheduler.log，供设置页导出诊断
void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    const char *level = "DEBUG";
    switch (type) {
    case QtDebugMsg: level = "DEBUG"; break;
    case QtInfoMsg: level = "INFO"; break;
    case QtWarningMsg: level = "WARN"; break;
    case QtCriticalMsg: level = "ERROR"; break;
    case QtFatalMsg: level = "FATAL"; break;
    }
    QMutexLocker locker(&g_logMutex);
    QFile f(g_logPath);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&f);
        out << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
            << " [" << level << "] " << msg << '\n';
    }
    if (type == QtFatalMsg)
        abort();
}

void initLogging()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/logs");
    QDir().mkpath(dir);
    g_logPath = dir + QStringLiteral("/smartscheduler.log");
    // 超过 2MB 轮转为 .1，避免无限增长
    if (QFileInfo(g_logPath).size() > 2 * 1024 * 1024) {
        QFile::remove(g_logPath + QStringLiteral(".1"));
        QFile::rename(g_logPath, g_logPath + QStringLiteral(".1"));
    }
    // 确保日志文件存在（设置页可立即导出/打开目录）
    if (!QFile::exists(g_logPath)) {
        QFile touch(g_logPath);
        touch.open(QIODevice::WriteOnly);
    }
    qInstallMessageHandler(messageHandler);
    qInfo("SmartScheduler %s 启动", APP_VERSION);
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("SmartScheduler"));
    // 注意：不要设置 applicationDisplayName —— Qt 会把它追加到窗口标题，造成重复
    app.setOrganizationName(QStringLiteral("SmartScheduler"));
    app.setWindowIcon(QIcon(QStringLiteral(":/qt/qml/SmartScheduler/icon.png")));
    initLogging(); // 须在设置组织/应用名之后：AppDataLocation 依赖它们

    AppController appController;
    Theme theme;
    ClassListModel classModel(&appController);
    CourseListModel courseModel(&appController);
    TeacherListModel teacherModel(&appController);
    ClassroomListModel classroomModel(&appController);
    GroupModel groupModel(&appController);
    PlanListModel planModel(&appController);
    DutyModel dutyModel(&appController);
    CombinedModel combinedModel(&appController);
    AdjustController adjustController(&appController);
    PrintController printController(&appController);

    QQmlApplicationEngine engine;
    QQmlContext *ctx = engine.rootContext();
    ctx->setContextProperty(QStringLiteral("app"), &appController);
    ctx->setContextProperty(QStringLiteral("Theme"), &theme);
    ctx->setContextProperty(QStringLiteral("classModel"), &classModel);
    ctx->setContextProperty(QStringLiteral("courseModel"), &courseModel);
    ctx->setContextProperty(QStringLiteral("teacherModel"), &teacherModel);
    ctx->setContextProperty(QStringLiteral("classroomModel"), &classroomModel);
    ctx->setContextProperty(QStringLiteral("groupModel"), &groupModel);
    ctx->setContextProperty(QStringLiteral("planModel"), &planModel);
    ctx->setContextProperty(QStringLiteral("dutyModel"), &dutyModel);
    ctx->setContextProperty(QStringLiteral("combinedModel"), &combinedModel);
    ctx->setContextProperty(QStringLiteral("adjust"), &adjustController);
    ctx->setContextProperty(QStringLiteral("printer"), &printController);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("SmartScheduler"), QStringLiteral("Main"));

    // 自检用：--screenshot=<file> 启动 2.5s 后抓取主窗口截图并退出（配合 QT_QPA_PLATFORM=offscreen）
    // 可选 --page=N 先切换到指定导航页再截图
    const QStringList args = app.arguments();
    const auto shotIt = std::find_if(args.cbegin(), args.cend(), [](const QString &a) {
        return a.startsWith(QStringLiteral("--screenshot="));
    });
    const auto pageIt = std::find_if(args.cbegin(), args.cend(), [](const QString &a) {
        return a.startsWith(QStringLiteral("--page="));
    });
    if (shotIt != args.cend()) {
        const QString path = shotIt->mid(13);
        const int page = pageIt != args.cend() ? pageIt->mid(7).toInt() : -1;
        QTimer::singleShot(2500, &app, [&app, path, page]() {
            int rc = 2;
            const auto windows = QGuiApplication::allWindows();
            if (!windows.isEmpty()) {
                if (auto *w = qobject_cast<QQuickWindow *>(windows.first())) {
                    if (page >= 0) {
                        w->setProperty("currentPage", page);
                        // 等一页切换动画结束再抓
                        QTimer::singleShot(400, w, [w, path, &app]() {
                            const QImage img = w->grabWindow();
                            app.exit(!img.isNull() && img.save(path) ? 0 : 2);
                        });
                        return;
                    }
                    const QImage img = w->grabWindow();
                    if (!img.isNull() && img.save(path))
                        rc = 0;
                }
            }
            app.exit(rc);
        });
    }

    // 回归自检：--smoke=reopen-dirty 模拟「脏项目状态下点继续上次」——示例学校（含完整数据、
    // 脏标记）下直接 openProject 最近文件，随后逐页切换触发所有绑定重算；崩溃即失败，退出码 0 通过
    const auto smokeIt = std::find_if(args.cbegin(), args.cend(), [](const QString &a) {
        return a.startsWith(QStringLiteral("--smoke="));
    });
    if (smokeIt != args.cend() && smokeIt->mid(8) == QLatin1StringView("reopen-dirty")) {
        QTimer::singleShot(1200, &app, [&appController, &app]() {
            // 确保最近文件里有一个可打开的项目
            QString prev = appController.recentFiles().value(0);
            if (prev.isEmpty()) {
                appController.newProject(QStringLiteral("烟测旧校"), 5, 7, 4, 20);
                prev = QDir::temp().filePath(QStringLiteral("smoke_prev.spk"));
                if (!appController.saveProjectAs(prev)) {
                    qWarning("smoke: 预存项目失败 %s", qPrintable(appController.lastError()));
                    app.exit(3);
                    return;
                }
            }
            appController.createSampleSchool(false); // 6班/11课/14师 + 约束，脏
            fprintf(stderr, "[smoke] sample school created\n");
            if (!appController.projectDirty()) {
                app.exit(4);
                return;
            }
            fprintf(stderr, "[smoke] reopening %s\n", qPrintable(prev));
            if (!appController.openProject(prev)) {
                qWarning("smoke: openProject 失败 %s", qPrintable(appController.lastError()));
                app.exit(5);
                return;
            }
            fprintf(stderr, "[smoke] reopened ok\n");
            // 逐页切换，让所有页面的绑定都重算一遍
            const auto windows = QGuiApplication::allWindows();
            auto *w = windows.isEmpty() ? nullptr : qobject_cast<QQuickWindow *>(windows.first());
            if (!w) {
                app.exit(6);
                return;
            }
            auto *page = new int(0);
            QTimer *timer = new QTimer(&app);
            QObject::connect(timer, &QTimer::timeout, &app, [&app, w, page, timer]() {
                if (*page > 9) {
                    timer->stop();
                    app.exit(0);
                    return;
                }
                w->setProperty("currentPage", *page);
                ++(*page);
            });
            timer->start(150);
        });
    }

    return app.exec();
}
