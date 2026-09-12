#pragma once

#include "EngineTypes.h"
#include "model/Schedule.h"
#include "model/School.h"

#include <QObject>

class QThreadPool;
class QTimer;

namespace sched {

namespace engine {
struct SharedState;
}

// 自动排课引擎（QObject 形式，供 QML 桥接）。
// start() 后在内部线程池执行 best-of-N 求解；相同 输入+seed 结果可复现。
// 注意：必须在有事件循环的线程中使用（依赖 QTimer 轮询进度）。
class SchedulerEngine : public QObject
{
    Q_OBJECT

public:
    SchedulerEngine(School school, EngineOptions options, QObject *parent = nullptr);
    ~SchedulerEngine() override;

    void start(); // 异步启动；求解中重复调用无效
    void cancel(); // 请求取消；各轮收敛当前最优后照常发出 finished
    bool isRunning() const { return m_running; }

Q_SIGNALS:
    void progress(int percent, const sched::QualityReport &current);
    void finished(const sched::Schedule &best, const sched::QualityReport &report);

private:
    void pollProgress();
    void finalize();

    School m_school;
    EngineOptions m_options;
    engine::SharedState *m_state = nullptr;
    QThreadPool *m_pool = nullptr;
    QTimer *m_timer = nullptr;
    bool m_running = false;
};

} // namespace sched

Q_DECLARE_METATYPE(sched::QualityReport)
Q_DECLARE_METATYPE(sched::Schedule)
