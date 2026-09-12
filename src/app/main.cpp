#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "AppController.h"
#include "CombinedModel.h"
#include "GroupModel.h"
#include "PlanModels.h"
#include "RosterModels.h"
#include "Theme.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("SmartScheduler"));
    app.setApplicationDisplayName(QStringLiteral("智排课系统"));
    app.setOrganizationName(QStringLiteral("SmartScheduler"));

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

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("SmartScheduler"), QStringLiteral("Main"));

    return app.exec();
}
