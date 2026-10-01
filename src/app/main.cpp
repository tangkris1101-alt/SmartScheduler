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
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTest>
#include <QTextStream>
#include <QTimer>

#include <algorithm>

#include "AdjustController.h"
#include "AiController.h"
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
bool g_logToStderr = false; // 冒烟自检时同时打到 stderr，终端/CI 直接看得到链路日志

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
    if (g_logToStderr)
        fprintf(stderr, "[%s] %s\n", level, msg.toUtf8().constData());
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
    const QStringList launchArgs = QCoreApplication::arguments();
    g_logToStderr = std::any_of(launchArgs.cbegin(), launchArgs.cend(), [](const QString &a) {
        return a.startsWith(QStringLiteral("--smoke="));
    });
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
    AiController aiController(&appController);
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
    ctx->setContextProperty(QStringLiteral("ai"), &aiController);
    ctx->setContextProperty(QStringLiteral("adjust"), &adjustController);
    ctx->setContextProperty(QStringLiteral("printer"), &printController);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    // 冒烟自检模式：抑制新手引导弹窗，避免模态对话框吃掉注入的点击
    const bool smokeMode = std::any_of(app.arguments().cbegin(), app.arguments().cend(),
                                       [](const QString &a) {
                                           return a.startsWith(QStringLiteral("--smoke="));
                                       });
    if (smokeMode)
        appController.setOnboardingShown(true);
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
            // 确保最近文件里有一个可打开的项目（列表可能指向已被删除的文件）
            QString prev = appController.recentFiles().value(0);
            if (prev.isEmpty() || !QFile::exists(prev)) {
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
                if (*page > 11) { // 0..11 共 12 页，含质量报告、AI 助手与设置
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

    // 回归自检：--smoke=groups 走「归类分组页选名单 → 真实点击成员开关 → 点击类型下拉选第二项」
    // 的完整鼠标路径；0 通过，7=建组失败，8=页面/候选异常，9=成员勾选无效，10=下拉自更新无效，
    // 14=开关视觉状态与模型不一致（AppSwitch 是受控组件，checked 必须由外部驱动）
    if (smokeIt != args.cend() && smokeIt->mid(8) == QLatin1StringView("groups")) {
        // Repeater/Loader 创建的项只在可视化 Item 树里，QObject::children() 找不到 → childItems 递归
        auto findAllItems = [](QQuickItem *root, const QString &name, QList<QQuickItem *> &out,
                               auto &&self) -> void {
            if (!root)
                return;
            if (root->objectName() == name)
                out.append(root);
            const auto items = root->childItems();
            for (auto *c : items)
                self(c, name, out, self);
        };
        QTimer::singleShot(1200, &app, [&appController, &groupModel, &app, findAllItems]() mutable {
            appController.createSampleSchool(false); // 6 班 / 11 课 / 14 师
            const qint64 gid = groupModel.addGroup(0, QStringLiteral("测试年级"));
            if (gid < 0) {
                qWarning("smoke-groups: addGroup 失败");
                app.exit(7);
                return;
            }
            // 找带 currentPage 属性的主窗口（offscreen 下可能有多个窗口）
            QQuickWindow *w = nullptr;
            const auto windows = QGuiApplication::allWindows();
            for (auto *win : windows) {
                auto *qw = qobject_cast<QQuickWindow *>(win);
                if (qw && qw->property("currentPage").isValid())
                    w = qw;
            }
            if (!w) {
                app.exit(6);
                return;
            }
            w->setProperty("currentPage", 2); // 归类分组
            QTimer::singleShot(700, &app, [&app, &groupModel, w, gid, findAllItems]() mutable {
                QList<QQuickItem *> pages;
                findAllItems(w->contentItem(), QStringLiteral("groupsPage"), pages, findAllItems);
                if (pages.isEmpty()) {
                    qWarning("smoke-groups: 未找到 groupsPage");
                    app.exit(8);
                    return;
                }
                QQuickItem *page = pages.first();
                // 探针 1：真实点击左侧分组行，验证鼠标事件投递链路本身是否工作
                QList<QQuickItem *> rows;
                findAllItems(page, QStringLiteral("groupRow"), rows, findAllItems);
                if (rows.isEmpty()) {
                    qWarning("smoke-groups: 未找到 groupRow");
                    app.exit(8);
                    return;
                }
                const QPoint rpos =
                    rows.first()->mapToScene(QPointF(rows.first()->width() / 2,
                                                     rows.first()->height() / 2)).toPoint();
                QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, rpos);
                QTimer::singleShot(400, &app,
                                   [&app, &groupModel, w, page, gid, findAllItems]() mutable {
                    const int sel = page->property("selectedRow").toInt();
                    qWarning("smoke-groups: 点击分组行后 selectedRow=%d", sel);
                    if (sel != 0) {
                        qWarning("smoke-groups: 分组行点击无效 —— 事件投递链路不通");
                        app.exit(8);
                        return;
                    }
                    const QVariantList cands = groupModel.candidates(gid);
                    QList<QQuickItem *> switches;
                    findAllItems(page, QStringLiteral("memberSwitch"), switches, findAllItems);
                    qWarning("smoke-groups: 候选=%lld 开关=%lld", (long long)cands.size(),
                             (long long)switches.size());
                    if (cands.size() != 6 || switches.isEmpty()) {
                        app.exit(8);
                        return;
                    }
                    // 注意：selectedRow=0 是示例学校自带的年级组（已含全部班级），
                    // 校验「点击后成员状态翻转」而非固定为 true
                    const qint64 selGid = groupModel.idAt(0);
                    const qint64 firstId =
                        cands.first().toMap().value(QStringLiteral("id")).toLongLong();
                    const bool wasMember = groupModel.isMember(selGid, firstId);
                    auto *sw = switches.first();
                    const QPoint pos =
                        sw->mapToScene(QPointF(sw->width() / 2, sw->height() / 2)).toPoint();
                    qWarning("smoke-groups: 点击开关 scene(%d,%d)", pos.x(), pos.y());
                    QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, pos);
                    QTimer::singleShot(
                        500, &app,
                        [&app, &groupModel, w, page, selGid, firstId, wasMember,
                         findAllItems]() mutable {
                            if (groupModel.isMember(selGid, firstId) == wasMember) {
                                qWarning("smoke-groups: 点击后成员状态未翻转 —— 勾选路径不通");
                                app.exit(9);
                                return;
                            }
                            qWarning("smoke-groups: 成员勾选 OK");
                            // 视觉断言：开关 checked 必须与模型成员状态一致
                            // （refreshCandidates 会重建代理，旧指针可能悬空，必须重新查找）
                            const bool nowMember = groupModel.isMember(selGid, firstId);
                            QList<QQuickItem *> switches2;
                            findAllItems(page, QStringLiteral("memberSwitch"), switches2,
                                         findAllItems);
                            const int visChecked = switches2.isEmpty()
                                                       ? -1
                                                       : int(switches2.first()
                                                                 ->property("checked")
                                                                 .toBool());
                            qWarning("smoke-groups: 开关视觉 checked=%d 模型=%d", visChecked,
                                     int(nowMember));
                            if (visChecked != int(nowMember)) {
                                qWarning("smoke-groups: 开关视觉状态与模型不一致");
                                app.exit(14);
                                return;
                            }
                            // 第二步：验证下拉弹窗点击后 currentIndex 自更新
                            QList<QQuickItem *> combos;
                            findAllItems(page, QStringLiteral("typeCombo"), combos, findAllItems);
                            if (combos.isEmpty()) {
                                app.exit(10);
                                return;
                            }
                            auto *combo = combos.first();
                            const QPoint cpos =
                                combo->mapToScene(QPointF(combo->width() / 2, combo->height() / 2))
                                    .toPoint();
                            QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, cpos);
                            QTimer::singleShot(500, &app,
                                               [&app, w, page, findAllItems]() mutable {
                                // 弹窗选项挂在 Overlay 下；其它下拉弹窗关闭、委托不可见
                                QList<QQuickItem *> opts;
                                findAllItems(w->contentItem(), QStringLiteral("comboOption"), opts,
                                             findAllItems);
                                QQuickItem *target = nullptr;
                                int visibleCount = 0;
                                for (auto *o : opts) {
                                    if (!o->isVisible())
                                        continue;
                                    ++visibleCount;
                                    if (o->property("index").toInt() == 1)
                                        target = o;
                                }
                                qWarning("smoke-groups: 可见下拉选项=%lld",
                                         (long long)visibleCount);
                                if (!target) {
                                    qWarning("smoke-groups: 弹窗未打开或缺少第 2 项");
                                    app.exit(10);
                                    return;
                                }
                                const QPoint tpos =
                                    target
                                        ->mapToScene(QPointF(target->width() / 2,
                                                             target->height() / 2))
                                        .toPoint();
                                QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, tpos);
                                QTimer::singleShot(400, &app, [&app, page, findAllItems]() mutable {
                                    QList<QQuickItem *> combos2;
                                    findAllItems(page, QStringLiteral("typeCombo"), combos2,
                                                 findAllItems);
                                    const int idx = combos2.isEmpty()
                                                        ? -1
                                                        : combos2.first()
                                                              ->property("currentIndex")
                                                              .toInt();
                                    qWarning("smoke-groups: 选项点击后 currentIndex=%d", idx);
                                    app.exit(idx == 1 ? 0 : 10);
                                });
                                               });
                        });
                                   });
            });
        });
    }

    // 回归自检：--smoke=close-dirty 脏项目关闭窗口 → 弹确认框 → 点「不保存退出」应真正退出。
    // 通过=进程随 Qt.quit() 退出（exit 0）；15=确认框未弹出，16=点击后进程仍在（看门狗判定退出失败）
    if (smokeIt != args.cend() && smokeIt->mid(8) == QLatin1StringView("close-dirty")) {
        auto findAllItems = [](QQuickItem *root, const QString &name, QList<QQuickItem *> &out,
                               auto &&self) -> void {
            if (!root)
                return;
            if (root->objectName() == name)
                out.append(root);
            const auto items = root->childItems();
            for (auto *c : items)
                self(c, name, out, self);
        };
        QTimer::singleShot(1200, &app, [&appController, &app, findAllItems]() mutable {
            appController.createSampleSchool(false); // 内存中的脏项目
            QQuickWindow *w = nullptr;
            const auto windows = QGuiApplication::allWindows();
            for (auto *win : windows) {
                auto *qw = qobject_cast<QQuickWindow *>(win);
                if (qw && qw->property("currentPage").isValid())
                    w = qw;
            }
            if (!w) {
                app.exit(6);
                return;
            }
            w->close(); // 触发 onClosing 守卫 → 应弹出确认框而非直接关闭
            QTimer::singleShot(600, &app, [&app, w, findAllItems]() mutable {
                QList<QQuickItem *> btns;
                findAllItems(w->contentItem(), QStringLiteral("dlgThirdBtn"), btns, findAllItems);
                QQuickItem *target = nullptr;
                for (auto *b : btns) {
                    if (b->isVisible()
                        && b->property("text").toString() == QStringLiteral("不保存退出"))
                        target = b;
                }
                if (!target) {
                    qWarning("smoke-close-dirty: 未找到「不保存退出」按钮（确认框未弹出？）");
                    app.exit(15);
                    return;
                }
                const QPoint pos =
                    target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint();
                QTest::mouseClick(w, Qt::LeftButton, Qt::NoModifier, pos);
                // 看门狗：1.5s 后进程仍在 → 退出失败
                QTimer::singleShot(1500, &app, [&app]() mutable {
                    qWarning("smoke-close-dirty: 点击「不保存退出」后进程仍在运行 —— 退出失败");
                    app.exit(16);
                });
            });
        });
    }

    // 回归自检：--smoke=report 质量报告页：示例学校排课后报告应有 10 项指标与老师明细，
    // 并截图存档供人工检查。0 通过，31=指标数量不对，32=老师明细为空，33=截图保存失败
    if (smokeIt != args.cend() && smokeIt->mid(8) == QLatin1StringView("report")) {
        QTimer::singleShot(1200, &app, [&appController]() {
            appController.createSampleSchool(true); // 排课完成后自动应用结果
        });
        // 等排课结束（示例排课约数秒），再切到报告页检查
        auto *poll = new QTimer(&app);
        QObject::connect(poll, &QTimer::timeout, &app, [&appController, &app, poll]() {
            if (!appController.hasSchedule())
                return;
            poll->stop();
            const QVariantMap rep = appController.qualityReport();
            if (rep.value(QStringLiteral("indicators")).toList().size() != 10) {
                qWarning("smoke-report: 指标数量不是 10（%lld）",
                         (long long)rep.value(QStringLiteral("indicators")).toList().size());
                app.exit(31);
                return;
            }
            if (rep.value(QStringLiteral("teachers")).toList().isEmpty()) {
                qWarning("smoke-report: 老师明细为空");
                app.exit(32);
                return;
            }
            QQuickWindow *w = nullptr;
            const auto windows = QGuiApplication::allWindows();
            for (auto *win : windows) {
                auto *qw = qobject_cast<QQuickWindow *>(win);
                if (qw && qw->property("currentPage").isValid())
                    w = qw;
            }
            if (!w) {
                app.exit(31);
                return;
            }
            w->setProperty("currentPage", 9); // 质量报告
            QTimer::singleShot(900, &app, [&appController, &app, w]() {
                const QString shot = QDir::temp().filePath(QStringLiteral("smoke_quality_report.png"));
                const QImage img = w->grabWindow();
                if (img.isNull() || !img.save(shot)) {
                    qWarning("smoke-report: 截图保存失败");
                    app.exit(33);
                    return;
                }
                qWarning("smoke-report: 指标 10 项、老师明细非空、截图 %s", qPrintable(shot));
                const QVariantMap rep = appController.qualityReport();
                for (const auto &v : rep.value(QStringLiteral("indicators")).toList()) {
                    const QVariantMap m = v.toMap();
                    qWarning("smoke-report: %s = %g / %g %s", qPrintable(m.value(QStringLiteral("name")).toString()),
                             m.value(QStringLiteral("value")).toDouble(),
                             m.value(QStringLiteral("required")).toDouble(),
                             qPrintable(m.value(QStringLiteral("unit")).toString()));
                }
                app.exit(0);
            });
        });
        poll->start(300);
    }


    // 回归自检：--smoke=ai 真实走「一句话 → 中继 → 大模型工具调用 → 待办 → 应用 → 撤销」
    // 0 通过，51=超时或未产生待办，52=应用后数据不符，53=撤销失败
    if (smokeIt != args.cend() && smokeIt->mid(8) == QLatin1StringView("ai")) {
        auto *ticks = new int(0);
        QTimer *timer = new QTimer(&app);
        QObject::connect(timer, &QTimer::timeout, &app,
                         [&appController, &aiController, &app, ticks, timer]() {
            if (++(*ticks) > 240) { // 最多等 120 秒
                timer->stop();
                qWarning("smoke-ai: 超时（busy=%d pending=%d err=%s）", aiController.busy(),
                         aiController.pendingCount(), qPrintable(aiController.lastError()));
                app.exit(51);
                return;
            }
            if (*ticks == 1) {
                appController.createSampleSchool(false);
                qWarning("smoke-ai: 发送一句话需求");
                aiController.sendPrompt(QStringLiteral("把全年级每天第7节都排成自习"));
                return;
            }
            if (aiController.busy())
                return;
            timer->stop();
            if (aiController.pendingCount() < 1) {
                qWarning("smoke-ai: 未产生待办（err=%s）", qPrintable(aiController.lastError()));
                app.exit(51);
                return;
            }
            const int before = appController.school()->preAssignments.size();
            const QVariantMap card = aiController.pending().first().toMap();
            qWarning("smoke-ai: 待办=%s | %s", qPrintable(card.value(QStringLiteral("tool")).toString()),
                     qPrintable(card.value(QStringLiteral("summary")).toString()));
            if (card.value(QStringLiteral("tool")).toString() != QLatin1String("batch_preassign")) {
                qWarning("smoke-ai: 工具不是批量预排");
                app.exit(51);
                return;
            }
            const QVariantMap r = aiController.applyPending({});
            const int after = appController.school()->preAssignments.size();
            qWarning("smoke-ai: 应用结果 applied=%d changed=%d 预排 %d → %d",
                     r.value(QStringLiteral("applied")).toInt(),
                     r.value(QStringLiteral("changed")).toBool() ? 1 : 0, before, after);
            if (after - before != 30) { // 6 个班 × 5 天
                qWarning("smoke-ai: 预排数量不符");
                app.exit(52);
                return;
            }
            if (!aiController.undo()) {
                qWarning("smoke-ai: 撤销失败");
                app.exit(53);
                return;
            }
            if (appController.school()->preAssignments.size() != before) {
                qWarning("smoke-ai: 撤销后数量不符");
                app.exit(53);
                return;
            }
            qWarning("smoke-ai: 全链路通过（%d → %d → %d）", before, after,
                     appController.school()->preAssignments.size());
            app.exit(0);
        });
        timer->start(500);
    }

    // 回归自检：--smoke=batch 批量操作（预排 / 不排 / 改课时 / 撤销）
    // 0 通过，41=批量预排异常，42=批量不排或改课时异常，43=撤销异常
    if (smokeIt != args.cend() && smokeIt->mid(8) == QLatin1StringView("batch")) {
        QTimer::singleShot(1200, &app, [&appController, &app]() {
            appController.createSampleSchool(false); // 不跑引擎，快速校验批量接口
            auto *school = appController.school();
            qint64 zixi = -1;
            for (const auto &c : school->courses) {
                if (c.name == QStringLiteral("自习"))
                    zixi = c.id;
            }
            if (zixi < 0 || school->classes.isEmpty()) {
                qWarning("smoke-batch: 示例学校缺少自习课程或班级");
                app.exit(41);
                return;
            }
            QVariantList classIds;
            for (const auto &c : school->classes)
                classIds.append(c.id);
            const int planTotal = school->planEntries.size();

            // 1) 批量预排：全年级每天第 7 节（0 基 6）→ 6 班 × 5 天 = 30 条
            QVariantList daySlots;
            for (int d = 0; d < 5; ++d)
                daySlots.append(d * 7 + 6);
            const int preBefore = school->preAssignments.size();
            const QVariantMap pre = appController.batchSetPreassignments(zixi, classIds, daySlots, 0);
            if (pre.value(QStringLiteral("set")).toInt() != 30
                || school->preAssignments.size() != preBefore + 30) {
                qWarning("smoke-batch: 批量预排异常 set=%d", pre.value(QStringLiteral("set")).toInt());
                app.exit(41);
                return;
            }
            // 2) 批量清除
            const QVariantMap clr = appController.batchClearPreassignments(zixi, classIds, daySlots);
            if (clr.value(QStringLiteral("cleared")).toInt() != 30
                || school->preAssignments.size() != preBefore) {
                qWarning("smoke-batch: 批量清除异常 cleared=%d",
                         clr.value(QStringLiteral("cleared")).toInt());
                app.exit(41);
                return;
            }
            // 3) 批量不排（老师，前两节）→ 恢复
            const qint64 teacherId = school->teachers.isEmpty() ? -1 : school->teachers.first().id;
            const QVariantMap ns = appController.batchSetNoSchedule(2, {teacherId}, {0, 1}, true);
            if (ns.value(QStringLiteral("set")).toInt() != 2
                || appController.noScheduleSlots(2, teacherId).size() != 2) {
                qWarning("smoke-batch: 批量不排异常");
                app.exit(42);
                return;
            }
            const QVariantMap nsBack = appController.batchSetNoSchedule(2, {teacherId}, {0, 1}, false);
            if (nsBack.value(QStringLiteral("cleared")).toInt() != 2
                || !appController.noScheduleSlots(2, teacherId).isEmpty()) {
                qWarning("smoke-batch: 批量恢复不排异常");
                app.exit(42);
                return;
            }
            // 4) 批量改课时：自习 → 5 节/周
            const QVariantMap wp = appController.batchSetWeeklyPeriods(zixi, classIds, 5);
            if (wp.value(QStringLiteral("updated")).toInt() != classIds.size()) {
                qWarning("smoke-batch: 批量改课时异常 updated=%d",
                         wp.value(QStringLiteral("updated")).toInt());
                app.exit(42);
                return;
            }
            // 5) 撤销本次 → 自习周节数还原
            if (!appController.undoLastBatch()) {
                qWarning("smoke-batch: 撤销失败");
                app.exit(43);
                return;
            }
            int zixiCount = 0;
            for (const auto &e : school->planEntries) {
                if (e.courseId == zixi)
                    ++zixiCount;
            }
            if (school->planEntries.size() != planTotal || zixiCount == 0) {
                qWarning("smoke-batch: 撤销后计划条目异常");
                app.exit(43);
                return;
            }
            qWarning("smoke-batch: 预排 30 条/清除/不排/改课时/撤销 全部通过");
            app.exit(0);
        });
    }

    // 回归自检：--smoke=plan-empty 空教学计划状态下，空状态提示应居中于卡片
    // 0 通过，11=页面/组件未找到，12=空状态未居中，13=截图保存失败
    if (smokeIt != args.cend() && smokeIt->mid(8) == QLatin1StringView("plan-empty")) {
        auto findAll = [](QQuickItem *root, const QString &name, QList<QQuickItem *> &out,
                          auto &&self) -> void {
            if (!root)
                return;
            if (root->objectName() == name)
                out.append(root);
            const auto items = root->childItems();
            for (auto *c : items)
                self(c, name, out, self);
        };
        QTimer::singleShot(
            1200, &app, [&appController, &classModel, &planModel, &app, findAll]() mutable {
                appController.newProject(QStringLiteral("烟测空校"), 5, 7, 4, 20);
                const qint64 cid = classModel.appendRow();
                classModel.set(0, QStringLiteral("name"), QStringLiteral("1班"));
                planModel.setCurrentClassId(cid);
                QQuickWindow *w = nullptr;
                const auto windows = QGuiApplication::allWindows();
                for (auto *win : windows) {
                    auto *qw = qobject_cast<QQuickWindow *>(win);
                    if (qw && qw->property("currentPage").isValid())
                        w = qw;
                }
                if (!w) {
                    app.exit(11);
                    return;
                }
                w->setProperty("currentPage", 4); // 教学计划
                QTimer::singleShot(700, &app, [&app, w, findAll]() mutable {
                    QList<QQuickItem *> states, cards;
                    findAll(w->contentItem(), QStringLiteral("planEmptyState"), states, findAll);
                    findAll(w->contentItem(), QStringLiteral("planTableCard"), cards, findAll);
                    if (states.isEmpty() || cards.isEmpty() || !states.first()->isVisible()) {
                        qWarning("smoke-plan-empty: 未找到可见的 planEmptyState/planTableCard");
                        app.exit(11);
                        return;
                    }
                    auto *st = states.first();
                    auto *card = cards.first();
                    const QPointF sc = st->mapToScene(QPointF(st->width() / 2, st->height() / 2));
                    const QPointF cc =
                        card->mapToScene(QPointF(card->width() / 2, card->height() / 2));
                    qWarning("smoke-plan-empty: 空状态中心(%.0f,%.0f) 卡片中心(%.0f,%.0f)", sc.x(),
                             sc.y(), cc.x(), cc.y());
                    const QString shot =
                        QDir::temp().filePath(QStringLiteral("smoke_plan_empty.png"));
                    const QImage img = w->grabWindow();
                    if (img.isNull() || !img.save(shot)) {
                        app.exit(13);
                        return;
                    }
                    if (qAbs(sc.x() - cc.x()) > 8 || qAbs(sc.y() - cc.y()) > 8) {
                        qWarning("smoke-plan-empty: 空状态未居中");
                        app.exit(12);
                        return;
                    }
                    app.exit(0);
                });
            });
    }

    // 回归自检：--smoke=tab 键盘可达性 —— 注入 Tab 键验证焦点能进入工具栏按钮与左侧导航，
    // 并在导航项上按 Space 验证页面切换。0 通过，14=导航未获得焦点，15=Space 切换失败，16=按钮未获得焦点
    if (smokeIt != args.cend() && smokeIt->mid(8) == QLatin1StringView("tab")) {
        QTimer::singleShot(1200, &app, [&appController, &app]() {
            // 导航项按「已打开项目」显示完整流程，先建一个项目
            appController.newProject(QStringLiteral("烟测校"), 5, 7, 4, 20);
            QQuickWindow *w = nullptr;
            const auto windows = QGuiApplication::allWindows();
            for (auto *win : windows) {
                auto *qw = qobject_cast<QQuickWindow *>(win);
                if (qw && qw->property("currentPage").isValid())
                    w = qw;
            }
            if (!w) {
                app.exit(14);
                return;
            }
            struct TabState { int tries = 0; bool sawNav = false; bool sawButton = false;
                              bool spaced = false; bool spaceOk = false; };
            auto *st = new TabState;
            QTimer *timer = new QTimer(&app);
            QObject::connect(timer, &QTimer::timeout, &app, [&app, w, st, timer]() {
                // 先检查上一次按键后的焦点，再发下一次按键
                if (st->tries > 0) {
                    auto *fi = w->activeFocusItem();
                    const QString name = fi ? fi->objectName() : QString();
                    qWarning("smoke-tab: 第%d次 Tab 焦点在 %s", st->tries, qPrintable(name));
                    if (name == QLatin1String("appButton"))
                        st->sawButton = true;
                    if (name == QLatin1String("navItem")) {
                        st->sawNav = true;
                        if (!st->spaced && fi->property("page").toInt() == 1) {
                            st->spaced = true;
                            QTest::keyClick(w, Qt::Key_Space);
                            st->spaceOk = (w->property("currentPage").toInt() == 1);
                        }
                    }
                }
                if (st->tries >= 40) {
                    timer->stop();
                    qWarning("smoke-tab: sawNav=%d sawButton=%d spaceOk=%d", st->sawNav,
                             st->sawButton, st->spaceOk);
                    if (!st->sawNav) { app.exit(14); return; }
                    if (!st->sawButton) { app.exit(16); return; }
                    if (!st->spaceOk) { app.exit(15); return; }
                    app.exit(0);
                    return;
                }
                ++st->tries;
                QTest::keyClick(w, Qt::Key_Tab);
            });
            timer->start(30);
        });
    }

    // 回归自检：--smoke=list-keys 名单表键盘导航 —— 列表应可聚焦，↓ 键应移动并选中下一行
    // 0 通过，17=列表/页面未找到或聚焦失败，18=↓ 键未生效
    if (smokeIt != args.cend() && smokeIt->mid(8) == QLatin1StringView("list-keys")) {
        auto findAll = [](QQuickItem *root, const QString &name, QList<QQuickItem *> &out,
                          auto &&self) -> void {
            if (!root)
                return;
            if (root->objectName() == name)
                out.append(root);
            const auto items = root->childItems();
            for (auto *c : items)
                self(c, name, out, self);
        };
        QTimer::singleShot(
            1200, &app, [&appController, &app, findAll]() mutable {
                appController.createSampleSchool(false); // 6 班
                QQuickWindow *w = nullptr;
                const auto windows = QGuiApplication::allWindows();
                for (auto *win : windows) {
                    auto *qw = qobject_cast<QQuickWindow *>(win);
                    if (qw && qw->property("currentPage").isValid())
                        w = qw;
                }
                if (!w) {
                    app.exit(17);
                    return;
                }
                w->setProperty("currentPage", 1); // 名单管理
                QTimer::singleShot(700, &app, [&app, w, findAll]() mutable {
                    QList<QQuickItem *> tables;
                    findAll(w->contentItem(), QStringLiteral("rosterTable"), tables, findAll);
                    if (tables.isEmpty()) {
                        qWarning("smoke-list-keys: 未找到 rosterTable");
                        app.exit(17);
                        return;
                    }
                    auto *t = tables.first();
                    QMetaObject::invokeMethod(t, "forceActiveFocus");
                    if (!t->hasActiveFocus()) {
                        qWarning("smoke-list-keys: 列表聚焦失败");
                        app.exit(17);
                        return;
                    }
                    QTest::keyClick(w, Qt::Key_Down);
                    QTimer::singleShot(300, &app, [&app, w, t, findAll]() mutable {
                        QList<QQuickItem *> pages;
                        findAll(w->contentItem(), QStringLiteral("rosterPage"), pages, findAll);
                        const int cur = t->property("currentIndex").toInt();
                        const int sel =
                            pages.isEmpty() ? -1 : pages.first()->property("selectedRow").toInt();
                        qWarning("smoke-list-keys: ↓ 后 currentIndex=%d selectedRow=%d", cur, sel);
                        app.exit(cur == 1 && sel == 1 ? 0 : 18);
                    });
                });
            });
    }

    // 回归自检：--smoke=update 只跑「检查更新」（不下载安装包），验证多通道探测能收敛到下结论：
    // 缓存通道（反代/CDN）可能返回旧版本，必须继续问到实时通道（自建中继 / GitHub API）才罢休。
    // 0 通过（2=已最新、3=有新版本都算通过），21=检查失败，23=超时
    if (smokeIt != args.cend() && smokeIt->mid(8) == QLatin1StringView("update")) {
        auto *ticks = new int(0);
        QTimer *timer = new QTimer(&app);
        QObject::connect(timer, &QTimer::timeout, &app, [&appController, &app, ticks, timer]() {
            if (++(*ticks) > 60) {
                timer->stop();
                fprintf(stderr, "[smoke] smoke-update: 超时\n");
                app.exit(23);
                return;
            }
            if (*ticks == 1) {
                appController.checkUpdate();
                return;
            }
            const int us = appController.updateStatus();
            if (us == 1 || us == 0)
                return; // 仍在检查
            timer->stop();
            fprintf(stderr, "[smoke] smoke-update: status=%d 远端=%s 说明=%s\n", us,
                    appController.latestVersion().toUtf8().constData(),
                    appController.updateMessage().toUtf8().constData());
            app.exit(us == 4 ? 21 : 0);
        });
        timer->start(500);
    }

    // 回归自检：--smoke=download 真实走「检查更新 → 镜像链下载安装包」全流程。
    // 版本相等也照下（只验证下载链路本身），文件落在 %TEMP%/SmartScheduler-update。
    // 0 通过，21=检查更新失败，22=下载失败，23=超时，24=落盘文件异常
    if (smokeIt != args.cend() && smokeIt->mid(8) == QLatin1StringView("download")) {
        auto *stage = new int(0);
        auto *ticks = new int(0);
        QTimer *timer = new QTimer(&app);
        QObject::connect(timer, &QTimer::timeout, &app, [&appController, &app, stage, ticks, timer]() {
            if (++(*ticks) > 180) {
                timer->stop();
                qWarning("smoke-download: 超时 stage=%d", *stage);
                app.exit(23);
                return;
            }
            if (*stage == 0) {
                if (*ticks == 1) {
                    appController.checkUpdate();
                    return;
                }
                const int us = appController.updateStatus();
                if (us == 1 || us == 0)
                    return; // 仍在检查
                if (us == 4) {
                    timer->stop();
                    qWarning("smoke-download: 检查更新失败 %s",
                             qPrintable(appController.updateMessage()));
                    app.exit(21);
                    return;
                }
                qWarning("smoke-download: 最新版 %s，开始下载",
                         qPrintable(appController.latestVersion()));
                appController.downloadUpdate();
                *stage = 1;
                return;
            }
            const int ds = appController.updateDlState();
            if (ds == 1)
                return; // 下载中
            timer->stop();
            if (ds == 3) {
                qWarning("smoke-download: 下载失败 %s",
                         qPrintable(appController.updateDlMessage()));
                app.exit(22);
                return;
            }
            // ds == 2：验证落盘文件
            const QDir d(QDir::temp().filePath(QStringLiteral("SmartScheduler-update")));
            const auto exes = d.entryList({QStringLiteral("*.exe")}, QDir::Files);
            qint64 fs = 0;
            if (!exes.isEmpty())
                fs = QFileInfo(d.filePath(exes.first())).size();
            qWarning("smoke-download: 就绪，文件=%s 大小=%lld", qPrintable(exes.value(0)),
                     (long long)fs);
            app.exit(!exes.isEmpty() && fs > 5 * 1024 * 1024 ? 0 : 24);
        });
        timer->start(1000);
    }

    return app.exec();
}
