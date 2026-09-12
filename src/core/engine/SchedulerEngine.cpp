#include "SchedulerEngine.h"

#include "EngineCore.h"

#include <QMutex>
#include <QMutexLocker>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <atomic>

namespace sched {

namespace engine {

// 线程间共享的运行状态
struct SharedState {
    std::atomic<bool> cancel{false};
    std::atomic<int> runsDone{0};
    std::atomic<long> iterationsDone{0};
    long iterationsTotal = 1;
    QMutex resultMutex;
    QVector<QPair<QualityReport, Schedule>> results; // 每轮 (报告, 课表)
};

class RunTask : public QRunnable
{
public:
    RunTask(const School &school, const EngineOptions &options, quint32 seed, SharedState *state)
        : m_school(school)
        , m_options(options)
        , m_seed(seed)
        , m_state(state)
    {
    }

    void run() override
    {
        Solver solver(m_school, m_options, m_seed, &m_state->cancel, &m_state->iterationsDone);
        solver.run();
        QMutexLocker locker(&m_state->resultMutex);
        m_state->results.append({solver.bestReport(), solver.bestSchedule()});
        m_state->runsDone.fetch_add(1);
    }

private:
    School m_school;
    EngineOptions m_options;
    quint32 m_seed;
    SharedState *m_state;
};

} // namespace engine

SchedulerEngine::SchedulerEngine(School school, EngineOptions options, QObject *parent)
    : QObject(parent)
    , m_school(std::move(school))
    , m_options(options)
{
    qRegisterMetaType<sched::QualityReport>("sched::QualityReport");
    qRegisterMetaType<sched::Schedule>("sched::Schedule");
}

SchedulerEngine::~SchedulerEngine()
{
    if (m_timer)
        m_timer->stop();
    cancel();
    if (m_pool)
        m_pool->waitForDone();
    delete m_state;
    m_state = nullptr;
}

void SchedulerEngine::start()
{
    if (m_running)
        return;
    m_running = true;

    m_state = new engine::SharedState;
    m_state->iterationsTotal = std::max<long>(1, long(m_options.runs) * m_options.maxIterationsPerRun);

    m_pool = new QThreadPool(this);
    m_pool->setMaxThreadCount(std::max(1, std::min(QThread::idealThreadCount(), m_options.runs)));

    const int runs = std::max(1, m_options.runs);
    for (int i = 0; i < runs; ++i) {
        // 每轮独立种子，保证可复现且互不相同
        m_pool->start(new engine::RunTask(m_school, m_options, m_options.seed + quint32(i), m_state));
    }

    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, [this] { pollProgress(); });
    m_timer->start();
}

void SchedulerEngine::cancel()
{
    if (m_state)
        m_state->cancel.store(true);
}

void SchedulerEngine::pollProgress()
{
    if (!m_state)
        return;

    const int done = m_state->runsDone.load();
    QualityReport current;
    {
        QMutexLocker locker(&m_state->resultMutex);
        bool first = true;
        for (const auto &r : m_state->results) {
            // 当前最优：硬违例少者优先，其次软罚分低者
            if (first || r.first.hardViolations < current.hardViolations
                || (r.first.hardViolations == current.hardViolations
                    && r.first.softScore < current.softScore)) {
                current = r.first;
                first = false;
            }
        }
    }

    if (done >= std::max(1, m_options.runs)) {
        finalize();
        return;
    }

    const int percent = int(std::min<long>(
        99, m_state->iterationsDone.load() * 100 / m_state->iterationsTotal));
    Q_EMIT progress(percent, current);
}

void SchedulerEngine::finalize()
{
    if (m_timer)
        m_timer->stop();

    Schedule best;
    QualityReport bestReport;
    {
        QMutexLocker locker(&m_state->resultMutex);
        bool first = true;
        for (const auto &r : m_state->results) {
            if (first || r.first.hardViolations < bestReport.hardViolations
                || (r.first.hardViolations == bestReport.hardViolations
                    && r.first.softScore < bestReport.softScore)) {
                bestReport = r.first;
                best = r.second;
                first = false;
            }
        }
    }

    Q_EMIT progress(100, bestReport);
    Q_EMIT finished(best, bestReport);

    m_running = false;
    if (m_pool)
        m_pool->waitForDone();
    delete m_state;
    m_state = nullptr;
}

} // namespace sched
