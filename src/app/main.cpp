#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
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

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("SmartScheduler"));
    // 注意：不要设置 applicationDisplayName —— Qt 会把它追加到窗口标题，造成重复
    app.setOrganizationName(QStringLiteral("SmartScheduler"));
    app.setWindowIcon(QIcon(QStringLiteral(":/qt/qml/SmartScheduler/icon.png")));

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

    return app.exec();
}
