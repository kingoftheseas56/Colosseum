// native/work/BackgroundWorkCoordinator.cpp
#include "work/BackgroundWorkCoordinator.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace work {

using namespace std::chrono_literals;

bool WorkContext::checkpoint()
{
    if (cancelled->load())
        return false;
    if (blockWhileHeld)
        blockWhileHeld(); // pause / Suspended; returns on resume or cancel
    if (cancelled->load())
        return false;
    if (pressure && pressure() == Pressure::LatencySensitive)
        std::this_thread::sleep_for(25ms);
    return !cancelled->load();
}

bool WorkContext::shouldYield() const
{
    return pressure && pressure() != Pressure::Normal;
}

BackgroundWorkCoordinator::BackgroundWorkCoordinator(int maxWorkers, QObject *parent)
    : QObject(parent)
{
    const int n = std::max(1, maxWorkers);
    m_workers.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        m_workers.emplace_back([this] { workerLoop(); });
}

BackgroundWorkCoordinator::~BackgroundWorkCoordinator()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
        for (const auto &job : std::as_const(m_jobs))
            job->cancelled->store(true);
    }
    m_cv.notify_all();
    for (auto &worker : m_workers)
        if (worker.joinable())
            worker.join();
}

void BackgroundWorkCoordinator::submit(const WorkSpec &spec, WorkFn fn)
{
    auto job = std::make_shared<Job>();
    job->spec = spec;
    job->fn = std::move(fn);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_jobs.insert(spec.id, job);
        m_queue.push_back(job);
    }
    m_cv.notify_one();
}

void BackgroundWorkCoordinator::pause(const QString &id)
{
    bool announce = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (auto job = m_jobs.value(id)) {
            job->paused->store(true);
            if (job->status == Status::Queued) {
                job->status = Status::Paused;
                announce = true;
            }
            // A Running job blocks at its next checkpoint; its status stays
            // Running honestly (it is mid-stage) until the stage ends.
        }
    }
    if (announce)
        emit workPaused(id);
}

void BackgroundWorkCoordinator::resume(const QString &id)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (auto job = m_jobs.value(id)) {
            job->paused->store(false);
            if (job->status == Status::Paused)
                job->status = Status::Queued;
        }
    }
    m_cv.notify_all();
}

void BackgroundWorkCoordinator::cancel(const QString &id)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (auto job = m_jobs.value(id)) {
            job->cancelled->store(true);
            if (job->status == Status::Queued || job->status == Status::Paused)
                job->status = Status::Cancelled;
        }
    }
    m_cv.notify_all();
}

void BackgroundWorkCoordinator::reprioritize(const QString &id, int priority)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (auto job = m_jobs.value(id))
        job->spec.priority = priority;
}

void BackgroundWorkCoordinator::setPressure(Pressure pressure)
{
    m_pressure.store(static_cast<int>(pressure));
    m_cv.notify_all();
}

Status BackgroundWorkCoordinator::status(const QString &id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (auto job = m_jobs.value(id))
        return job->status;
    return Status::Unknown;
}

bool BackgroundWorkCoordinator::runnableAvailableLocked() const
{
    if (static_cast<Pressure>(m_pressure.load()) == Pressure::Suspended)
        return false;
    return std::any_of(m_queue.begin(), m_queue.end(),
                       [](const auto &job) { return job->status == Status::Queued; });
}

void BackgroundWorkCoordinator::workerLoop()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    for (;;) {
        m_cv.wait(lock, [this] { return m_stopping || runnableAvailableLocked(); });
        if (m_stopping)
            return;

        // Drop finished bookkeeping entries, then take the highest-priority queued job.
        m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(),
                                     [](const auto &job) {
                                         return job->status == Status::Cancelled
                                                || job->status == Status::Completed
                                                || job->status == Status::Failed;
                                     }),
                      m_queue.end());
        auto best = m_queue.end();
        for (auto it = m_queue.begin(); it != m_queue.end(); ++it) {
            if ((*it)->status != Status::Queued)
                continue;
            if (best == m_queue.end() || (*it)->spec.priority > (*best)->spec.priority)
                best = it;
        }
        if (best == m_queue.end())
            continue;

        auto job = *best;
        m_queue.erase(best);
        job->status = Status::Running;

        WorkContext ctx;
        ctx.cancelled = job->cancelled;
        ctx.paused = job->paused;
        ctx.pressure = [this] { return static_cast<Pressure>(m_pressure.load()); };
        ctx.blockWhileHeld = [this, job] {
            std::unique_lock<std::mutex> waitLock(m_mutex);
            m_cv.wait(waitLock, [this, &job] {
                return job->cancelled->load()
                       || (!job->paused->load()
                           && static_cast<Pressure>(m_pressure.load()) != Pressure::Suspended);
            });
        };

        lock.unlock();
        emit workStarted(job->spec.id);
        WorkResult result = WorkResult::Failed;
        QString failReason;
        try {
            result = job->fn(ctx);
        } catch (const std::exception &e) {
            failReason = QString::fromUtf8(e.what());
        } catch (...) {
            failReason = QStringLiteral("unknown exception in work function");
        }
        lock.lock();

        switch (result) {
        case WorkResult::Completed:
            job->status = Status::Completed;
            break;
        case WorkResult::Paused:
            job->status = Status::Paused;
            m_queue.push_back(job); // resume() flips it back to Queued
            break;
        case WorkResult::Cancelled:
            job->status = Status::Cancelled;
            break;
        case WorkResult::Failed:
            job->status = Status::Failed;
            break;
        }

        const QString id = job->spec.id;
        const Status finalStatus = job->status;
        lock.unlock();
        if (finalStatus == Status::Completed)
            emit workFinished(id);
        else if (finalStatus == Status::Paused)
            emit workPaused(id);
        else if (finalStatus == Status::Failed)
            emit workFailed(id, failReason.isEmpty()
                                    ? QStringLiteral("work function reported failure")
                                    : failReason);
        lock.lock();
    }
}

} // namespace work
