// native/work/BackgroundWorkCoordinator.h
#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace work {

enum class Pressure { Normal, LatencySensitive, Suspended };
enum class WorkResult { Completed, Paused, Cancelled, Failed };
enum class Status { Unknown, Queued, Running, Paused, Completed, Cancelled, Failed };

struct WorkSpec {
    QString id;
    int priority = 0; // convention: current=100, next=90.., previous=80, remainder=10
};

class BackgroundWorkCoordinator;

class WorkContext {
public:
    // False when cancelled. Blocks while the job is paused or global pressure is
    // Suspended (wakes immediately on cancel/resume). Sleeps one 25 ms beat under
    // LatencySensitive so heavy loops naturally yield to media playback.
    bool checkpoint();
    bool shouldYield() const;

private:
    friend class BackgroundWorkCoordinator;
    std::shared_ptr<std::atomic_bool> cancelled;
    std::shared_ptr<std::atomic_bool> paused;
    std::function<Pressure()> pressure;
    std::function<void()> blockWhileHeld;
};

using WorkFn = std::function<WorkResult(WorkContext &)>;

// Domain-neutral resumable background scheduler. One (by default) worker thread,
// priority dequeue at unit boundaries, pause/cancel tokens, pressure yielding.
// Shared by guided comic analysis and audiobook alignment — one instance, one
// worker, both domains' jobs interleave by priority so a single background CPU
// lane never fights media playback. Bump maxWorkers only with evidence.
class BackgroundWorkCoordinator final : public QObject {
    Q_OBJECT
public:
    explicit BackgroundWorkCoordinator(int maxWorkers = 1, QObject *parent = nullptr);
    ~BackgroundWorkCoordinator() override;

    void submit(const WorkSpec &spec, WorkFn fn);
    Q_INVOKABLE void pause(const QString &id);
    Q_INVOKABLE void resume(const QString &id);
    Q_INVOKABLE void cancel(const QString &id);
    void reprioritize(const QString &id, int priority);
    void setPressure(Pressure pressure);
    Status status(const QString &id) const;

signals:
    void workStarted(const QString &id);
    void workFinished(const QString &id); // Completed only
    void workPaused(const QString &id);
    void workFailed(const QString &id, const QString &reason);

private:
    struct Job {
        WorkSpec spec;
        WorkFn fn;
        Status status = Status::Queued;
        std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
        std::shared_ptr<std::atomic_bool> paused = std::make_shared<std::atomic_bool>(false);
    };

    void workerLoop();
    bool runnableAvailableLocked() const;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::shared_ptr<Job>> m_queue;
    QHash<QString, std::shared_ptr<Job>> m_jobs;
    std::atomic<int> m_pressure{static_cast<int>(Pressure::Normal)};
    std::vector<std::thread> m_workers;
    bool m_stopping = false;
};

} // namespace work
