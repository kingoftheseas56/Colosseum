# Background-Work Spine Groundwork Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the shared infrastructure both upcoming arcs sit on — A1's panel-aware guided comics and A2's audiobook-text alignment — so the two brothers can run in parallel without colliding on shared files or double-building the same engine.

**Architecture:** Both arcs are the same machine wearing two faces: a resumable, pausable, priority-driven background worker that runs bundled offline ML over downloaded assets and reports into one unified activity surface. This plan ships that machine once, as four neutral components (`work::BackgroundWorkCoordinator`, `work::BackgroundActivityRegistry`, `models::ModelManifest`, the ONNX Runtime build seam), reconciles the diverged repo first, and ends by amending the guided-reader plan + commissioning A2's plan so both arcs consume the spine instead of reinventing it.

**Tech Stack:** C++17, Qt 6.11.1 (Core/Quick/Qml), std::thread + condition_variable, SQLite (downstream, not here), ONNX Runtime 1.25.0 CPU x64 (seam only — nothing links it yet), QML, PowerShell harnesses.

**Provenance:** Extracted from [Agent 0 (Codex)]'s `2026-07-21-panel-aware-guided-reader.md` (Tasks 1, 2, 6 — the pieces already marked "Agent 0, shared architecture") and the `2026-07-21-audiobook-epub-read-along-design.md` spec (whose `AlignmentScheduler` describes the identical scheduler). Assessment: Agent 0 (Claude) on Fable, 2026-07-22.

---

## Global Constraints

- **This is the NESTED Colosseum repo** (`C:\Users\Suprabha\Desktop\Brotherhood\Colosseum`) — its own origin. `cd` into it before every git command. The haven repo above it is a different repo.
- **Ownership:** Everything in this plan is Agent 0's lane (shared scheduling, build, dependency, `main.cpp`, `native/CMakeLists.txt`). After this plan, A1 owns `native/guided/` + `qml/guided/`; A2 owns the alignment/Reader2 lane; only Agent 0 touches `main.cpp`, `native/CMakeLists.txt`, `qml/DownloadsPage.qml`, and packaging.
- **Commit discipline:** shared repo — stage explicitly, verify `git diff --cached --stat`, commit with explicit pathspec (`git commit -m "..." -- <paths>`), push after every commit (standing order).
- **Hemanth-owned dirt is sacred:** modified `wallpapers.ini` and the untracked piles (`_wanolive/`, `artifacts/`, `enrichment/`, probe qml files, etc.) must survive untouched.
- **Rule 1:** kill any running `colosseum.exe` before building (running exe locks binaries).
- **No concurrent builds** in `native/build-msvc`.
- **Design rules for the QML row:** gray/black/white only, no color, no emoji, inline metadata, `font.pixelSize` must be an integer.
- **Priority convention** (shared across both arcs, so their jobs interleave sensibly on the one worker): current unit = 100, next units = 90 down, previous = 80, remainder = 10.

## File Structure

```
native/work/BackgroundWorkCoordinator.h/.cpp    domain-neutral resumable single-worker scheduler
native/work/BackgroundActivityRegistry.h/.cpp   app-owned activity bulletin board for QML
native/models/ModelManifest.h/.cpp              generic bundled-model manifest + SHA-256 validation
native/cmake/OnnxRuntime.cmake                  imported ONNX Runtime target (dormant behind option)
scripts/native/fetch_onnxruntime.ps1            verified developer bootstrap for the runtime
qml/BackgroundActivitySection.qml               prop-driven activity rows component
qml/DownloadsPage.qml                           one-snippet integration of the section
native/main.cpp                                 registry + shared coordinator construction
native/CMakeLists.txt                           new sources, harness targets, ONNX option
tests/background_work_coordinator_harness.cpp   scheduler contract harness
tests/background_activity_registry_harness.cpp  registry contract harness
tests/background_activity_section_harness.qml   QML rows harness
tests/test_background_activity.ps1              QML harness runner + grep contracts
tests/model_manifest_harness.cpp                manifest/checksum harness
tests/onnx_seam_probe/CMakeLists.txt            configure-only probe for the ONNX seam
docs/superpowers/plans/2026-07-21-panel-aware-guided-reader.md   amended (Task 6)
docs/superpowers/plans/2026-07-22-a2-alignment-plan-commission.md  new commission prompt (Task 6)
../agents/chat.md (HAVEN repo)                  cross-lane announcement (Task 6)
```

---

### Task 0: Reconcile the diverged repo and back up Player 2

The repo is forked: 13 local-only commits vs 10 remote-only commits. Ground truth (verified 2026-07-22): `git cherry origin/master master` shows the 3 newest local commits (`055092f`, `e02310a`, `fa55cfd` — guided design/plan docs + genre blurbs) are **patch-equivalent** to remote commits (they were pushed from an isolated worktree and landed rebased). The 10 unique local commits are all Agent 4 (Codex) Player 2 work (`3264f65`..`e139cca`) touching only `native/player2/`, `tests/player2/`, and player2 docs — no file overlap with the remote-only biblio docs commits. A rebase will drop the 3 duplicates automatically and replay the 10 cleanly.

**Files:** none created; repo state only.

- [ ] **Step 1: Kill any running app and confirm the divergence is still exactly as mapped**

```bash
cd "C:/Users/Suprabha/Desktop/Brotherhood/Colosseum"
taskkill //IM colosseum.exe //F 2>/dev/null; true
git fetch origin
git status -sb | head -1          # expect: ## master...origin/master [ahead 13, behind 10]
git cherry origin/master master   # expect: exactly 10 "+" (all player2) and 3 "-"
```

If the counts differ from 13/10 or any `+` commit is NOT a Player 2 / groundwork commit, **STOP and report to Hemanth** — someone moved the repo since this plan was written. Do not improvise a merge.

- [ ] **Step 2: Stash Hemanth's tracked dirt, rebase, restore**

```bash
git stash push -m "groundwork-reconcile" -- wallpapers.ini
git rebase origin/master
git stash pop
```

Expected: rebase reports the 3 equivalent commits skipped, replays 10. If ANY conflict appears: `git rebase --abort`, `git stash pop`, stop and report — the plan's ground truth said clean.

- [ ] **Step 3: Verify the reconciled state**

```bash
git status -sb | head -1                      # expect: ahead 10, behind 0 — nothing lost
git log --oneline origin/master..master | wc -l   # expect: 10
git log --oneline -3                          # newest should be e-something "Player 2 Claude carryover" content
git stash list | head -2                      # expect: no groundwork-reconcile entry left
```

- [ ] **Step 4: Incremental build to verify the committed artifact**

Remote-only commits were docs-only, so this should be a near-no-op, but we verify the committed tree, never assume:

```bash
cmake --build native/build-msvc --target colosseum 2>&1 | tail -5
```

Expected: exit 0. If the environment lacks the MSVC toolchain on PATH, run the repo's own wrapper instead: `cmd //c "C:\\Users\\Suprabha\\Desktop\\Brotherhood\\Colosseum\\native\\build-msvc.bat"` (absolute path — relative invocation is a known failure).

- [ ] **Step 5: Push — Player 2 finally has an off-laptop copy**

```bash
git push origin master
git status -sb | head -1   # expect: ahead 0, behind 0
```

- [ ] **Step 6: Commit this plan file itself**

```bash
git add docs/superpowers/plans/2026-07-22-background-work-spine-groundwork.md
git diff --cached --stat   # exactly 1 file
git commit -m "[Agent 0 (Claude), governance] docs: plan background-work spine groundwork" -- docs/superpowers/plans/2026-07-22-background-work-spine-groundwork.md
git push origin master
```

### Task 1: `work::BackgroundWorkCoordinator` — the shared scheduler

This is Codex's guided-plan Task 2, built here once so both arcs consume it. Domain-neutral: knows nothing about comics or audiobooks. One worker thread, priority dequeue, pause/resume/cancel tokens, pressure yielding, checkpoint discipline.

**Files:**
- Create: `native/work/BackgroundWorkCoordinator.h`
- Create: `native/work/BackgroundWorkCoordinator.cpp`
- Create: `tests/background_work_coordinator_harness.cpp`
- Modify: `native/CMakeLists.txt` (new harness target, after the existing harness block that starts near line 224)

**Interfaces (frozen — both arcs' plans reference these exact names):**
- `work::BackgroundWorkCoordinator::submit(const WorkSpec&, WorkFn)`
- `pause(id)`, `resume(id)`, `cancel(id)`, `reprioritize(id, priority)`, `setPressure(Pressure)`, `status(id)`
- `work::WorkContext::checkpoint()` and `shouldYield()` inside work functions
- Signals: `workStarted`, `workFinished`, `workPaused`, `workFailed`

**Semantics (locked):**
- No job is dequeued while pressure is `Suspended` (this also makes tests deterministic: suspend → submit → unsuspend).
- `checkpoint()` returns `false` when cancelled; blocks while paused or `Suspended`; sleeps one 25 ms beat under `LatencySensitive` so tight loops naturally yield to media playback; wakes immediately on cancel/resume.
- Highest `priority` int wins among queued jobs. Never preempts a running work function — pressure reaches it only at its next checkpoint.
- A work function returning `Paused` goes back in the queue paused; `resume` requeues it. Exceptions inside a work function become `Failed`, never a crash.
- `workFinished` fires only for `Completed`.

- [ ] **Step 1: Write the failing harness**

```cpp
// tests/background_work_coordinator_harness.cpp
#include "work/BackgroundWorkCoordinator.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <iostream>
#include <mutex>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    work::BackgroundWorkCoordinator q(1);

    // Hold the worker so all three jobs are queued before any runs — deterministic.
    q.setPressure(work::Pressure::Suspended);

    std::mutex orderMutex;
    QStringList order;
    auto fn = [&](QString name) {
        return [&, name](work::WorkContext &c) {
            if (!c.checkpoint())
                return work::WorkResult::Cancelled;
            std::lock_guard<std::mutex> g(orderMutex);
            order << name;
            return work::WorkResult::Completed;
        };
    };
    q.submit({QStringLiteral("remainder"), 10}, fn(QStringLiteral("remainder")));
    q.submit({QStringLiteral("current"), 100}, fn(QStringLiteral("current")));
    q.submit({QStringLiteral("next"), 90}, fn(QStringLiteral("next")));

    require(q.status(QStringLiteral("current")) == work::Status::Queued,
            "suspended pressure holds the queue");

    q.pause(QStringLiteral("next"));
    require(q.status(QStringLiteral("next")) == work::Status::Paused, "pause visible");
    q.resume(QStringLiteral("next"));
    require(q.status(QStringLiteral("next")) == work::Status::Queued, "resume requeues");

    QEventLoop loop;
    int finished = 0;
    QObject::connect(&q, &work::BackgroundWorkCoordinator::workFinished, &app,
                     [&](const QString &) {
                         if (++finished == 3)
                             loop.quit();
                     });
    q.setPressure(work::Pressure::Normal);
    QTimer::singleShot(10000, &loop, [&] { loop.quit(); }); // watchdog — never hang
    loop.exec();

    require(finished == 3, "all three jobs completed (watchdog fired = scheduling bug)");
    {
        std::lock_guard<std::mutex> g(orderMutex);
        require(order == QStringList({QStringLiteral("current"), QStringLiteral("next"),
                                      QStringLiteral("remainder")}),
                "priority order current > next > remainder");
    }

    // Cancel a queued job before it runs.
    q.setPressure(work::Pressure::Suspended);
    q.submit({QStringLiteral("doomed"), 5}, fn(QStringLiteral("doomed")));
    q.cancel(QStringLiteral("doomed"));
    require(q.status(QStringLiteral("doomed")) == work::Status::Cancelled,
            "cancel visible on queued job");

    // Pressure reaches a running worker through shouldYield().
    std::atomic_bool sawYield{false};
    QEventLoop loop2;
    QObject::connect(&q, &work::BackgroundWorkCoordinator::workFinished, &app,
                     [&](const QString &id) {
                         if (id == QStringLiteral("yieldprobe"))
                             loop2.quit();
                     });
    q.setPressure(work::Pressure::Normal);
    q.submit({QStringLiteral("yieldprobe"), 1}, [&](work::WorkContext &c) {
        q.setPressure(work::Pressure::LatencySensitive);
        sawYield = c.shouldYield();
        q.setPressure(work::Pressure::Normal);
        return work::WorkResult::Completed;
    });
    QTimer::singleShot(10000, &loop2, [&] { loop2.quit(); });
    loop2.exec();
    require(sawYield.load(), "video/decode pressure reaches worker");

    std::cout << "BACKGROUND_WORK_OK\n";
    return 0;
}
```

- [ ] **Step 2: Register the harness target and verify it fails**

Add to `native/CMakeLists.txt`, adjacent to the existing harness targets (the block starting near `add_executable(search_history_store_harness` at ~line 224):

```cmake
add_executable(background_work_coordinator_harness
    ../tests/background_work_coordinator_harness.cpp
    work/BackgroundWorkCoordinator.cpp
    work/BackgroundWorkCoordinator.h)
target_include_directories(background_work_coordinator_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(background_work_coordinator_harness PRIVATE Qt6::Core)
```

Run: `cmake --build native/build-msvc --target background_work_coordinator_harness`

Expected: FAIL — `work/BackgroundWorkCoordinator.h` does not exist.

- [ ] **Step 3: Implement the header**

```cpp
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
```

- [ ] **Step 4: Implement the coordinator**

```cpp
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
```

- [ ] **Step 5: Build and run until green**

```bash
cmake --build native/build-msvc --target background_work_coordinator_harness
native/build-msvc/background_work_coordinator_harness.exe
```

Expected: `BACKGROUND_WORK_OK`, exit 0. Run it 5 times — a scheduler harness that passes once and hangs on run 3 is a real finding, not flake.

- [ ] **Step 6: Commit and push**

```bash
git add native/work/BackgroundWorkCoordinator.h native/work/BackgroundWorkCoordinator.cpp tests/background_work_coordinator_harness.cpp native/CMakeLists.txt
git diff --cached --stat   # exactly 4 files
git commit -m "[Agent 0 (Claude), foundation] feat(work): add shared resumable background coordinator" -- native/work tests/background_work_coordinator_harness.cpp native/CMakeLists.txt
git push origin master
```

### Task 2: `work::BackgroundActivityRegistry` + app wiring in `main.cpp`

The one bulletin board both arcs publish into, so the Downloads page shows every background job through a single seam and neither brother edits the other's rows. Native services `publish()` presentation-shaped state; QML reads `activities` and requests pause/resume; services listen on the request signals.

**Files:**
- Create: `native/work/BackgroundActivityRegistry.h`
- Create: `native/work/BackgroundActivityRegistry.cpp`
- Create: `tests/background_activity_registry_harness.cpp`
- Modify: `native/main.cpp` (construct registry + shared coordinator; register context property — insert after the `Live` registration, find it with `grep -n 'QStringLiteral("Live")' native/main.cpp`)
- Modify: `native/CMakeLists.txt` (add the two new sources to the `colosseum` target's list right after `AudioPairingStore.h`; add harness target)

**Interfaces:**
- C++ producer side: `publish(id, state)`, `remove(id)`; signals `pauseRequested(id)` / `resumeRequested(id)`.
- QML consumer side: context property `BackgroundActivity` with `activities` list property, `requestPause(id)`, `requestResume(id)`.
- Required state keys: `title` (QString), `stage` (QString), `progress` (double 0..1), `paused` (bool), `canPause` (bool). `id` is injected.
- Threading contract: GUI-thread only. Services on worker threads marshal via `QMetaObject::invokeMethod(registry, ..., Qt::QueuedConnection)`.

- [ ] **Step 1: Write the failing harness**

```cpp
// tests/background_activity_registry_harness.cpp
#include "work/BackgroundActivityRegistry.h"

#include <QCoreApplication>
#include <QVariantMap>

#include <iostream>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    work::BackgroundActivityRegistry registry;

    int changes = 0;
    QObject::connect(&registry, &work::BackgroundActivityRegistry::activitiesChanged,
                     [&] { ++changes; });
    QString pausedId;
    QObject::connect(&registry, &work::BackgroundActivityRegistry::pauseRequested,
                     [&](const QString &id) { pausedId = id; });

    QVariantMap guided{{QStringLiteral("title"), QStringLiteral("Analyzing One Piece pages")},
                       {QStringLiteral("stage"), QStringLiteral("Detecting panels")},
                       {QStringLiteral("progress"), 0.4},
                       {QStringLiteral("paused"), false},
                       {QStringLiteral("canPause"), true}};
    registry.publish(QStringLiteral("guided:onepiece"), guided);
    require(registry.activities().size() == 1, "publish adds a row");
    require(changes == 1, "publish notifies");

    guided[QStringLiteral("progress")] = 0.6;
    registry.publish(QStringLiteral("guided:onepiece"), guided);
    require(registry.activities().size() == 1, "re-publish updates in place, no duplicate");
    require(registry.activities().first().toMap().value(QStringLiteral("progress")).toDouble() == 0.6,
            "re-publish carries new state");
    require(registry.activities().first().toMap().value(QStringLiteral("id")).toString()
                == QStringLiteral("guided:onepiece"),
            "id injected into the row");

    registry.publish(QStringLiteral("align:dune"),
                     QVariantMap{{QStringLiteral("title"), QStringLiteral("Syncing Dune")},
                                 {QStringLiteral("stage"), QStringLiteral("Aligning words")},
                                 {QStringLiteral("progress"), 0.1},
                                 {QStringLiteral("paused"), false},
                                 {QStringLiteral("canPause"), true}});
    require(registry.activities().size() == 2, "second domain coexists");
    require(registry.activities().at(0).toMap().value(QStringLiteral("id")).toString()
                == QStringLiteral("guided:onepiece"),
            "insertion order preserved");

    registry.requestPause(QStringLiteral("align:dune"));
    require(pausedId == QStringLiteral("align:dune"), "pause request reaches producer side");

    registry.remove(QStringLiteral("guided:onepiece"));
    require(registry.activities().size() == 1, "remove drops the row");
    registry.remove(QStringLiteral("ghost"));
    require(registry.activities().size() == 1, "removing unknown id is a no-op");

    std::cout << "BACKGROUND_ACTIVITY_REGISTRY_OK\n";
    return 0;
}
```

- [ ] **Step 2: Register the harness target and verify failure**

```cmake
add_executable(background_activity_registry_harness
    ../tests/background_activity_registry_harness.cpp
    work/BackgroundActivityRegistry.cpp
    work/BackgroundActivityRegistry.h)
target_include_directories(background_activity_registry_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(background_activity_registry_harness PRIVATE Qt6::Core)
```

Run: `cmake --build native/build-msvc --target background_activity_registry_harness`
Expected: FAIL — header missing.

- [ ] **Step 3: Implement**

```cpp
// native/work/BackgroundActivityRegistry.h
#pragma once

#include <QObject>
#include <QPair>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace work {

// App-owned bulletin board for long-running background jobs (guided comic
// analysis, audiobook text sync, whatever comes next). Native services publish
// presentation-shaped state; QML renders rows and requests pause/resume.
// GUI-thread only: worker threads marshal publish() via queued invokeMethod.
class BackgroundActivityRegistry final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList activities READ activities NOTIFY activitiesChanged)
public:
    explicit BackgroundActivityRegistry(QObject *parent = nullptr);

    QVariantList activities() const;

    // Required state keys: title, stage, progress (0..1), paused, canPause.
    // Publishing an existing id updates that row in place.
    void publish(const QString &id, const QVariantMap &state);
    void remove(const QString &id);

    Q_INVOKABLE void requestPause(const QString &id);
    Q_INVOKABLE void requestResume(const QString &id);

signals:
    void activitiesChanged();
    void pauseRequested(const QString &id);
    void resumeRequested(const QString &id);

private:
    QVector<QPair<QString, QVariantMap>> m_rows; // insertion order = display order
};

} // namespace work
```

```cpp
// native/work/BackgroundActivityRegistry.cpp
#include "work/BackgroundActivityRegistry.h"

namespace work {

BackgroundActivityRegistry::BackgroundActivityRegistry(QObject *parent)
    : QObject(parent)
{
}

QVariantList BackgroundActivityRegistry::activities() const
{
    QVariantList list;
    list.reserve(m_rows.size());
    for (const auto &row : m_rows) {
        QVariantMap entry = row.second;
        entry.insert(QStringLiteral("id"), row.first);
        list.append(entry);
    }
    return list;
}

void BackgroundActivityRegistry::publish(const QString &id, const QVariantMap &state)
{
    for (auto &row : m_rows) {
        if (row.first == id) {
            row.second = state;
            emit activitiesChanged();
            return;
        }
    }
    m_rows.append(qMakePair(id, state));
    emit activitiesChanged();
}

void BackgroundActivityRegistry::remove(const QString &id)
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).first == id) {
            m_rows.removeAt(i);
            emit activitiesChanged();
            return;
        }
    }
}

void BackgroundActivityRegistry::requestPause(const QString &id)
{
    emit pauseRequested(id);
}

void BackgroundActivityRegistry::requestResume(const QString &id)
{
    emit resumeRequested(id);
}

} // namespace work
```

- [ ] **Step 4: Run the harness until green**

```bash
cmake --build native/build-msvc --target background_activity_registry_harness
native/build-msvc/background_activity_registry_harness.exe
```

Expected: `BACKGROUND_ACTIVITY_REGISTRY_OK`, exit 0.

- [ ] **Step 5: Wire both shared objects into the app**

In `native/main.cpp`, add the include near the other local includes at the top:

```cpp
#include "work/BackgroundActivityRegistry.h"
#include "work/BackgroundWorkCoordinator.h"
```

After the `Live` context-property registration (`grep -n 'QStringLiteral("Live")' native/main.cpp`), add:

```cpp
    // Shared background-work spine: ONE coordinator (one worker) for every
    // offline-analysis domain — guided comics, audiobook alignment. Services
    // receive it by injection so heavy inference never runs two-wide on
    // laptop-class hardware. The registry is the unified activity surface.
    auto *backgroundWork = new work::BackgroundWorkCoordinator(1, &app);
    Q_UNUSED(backgroundWork); // consumed by guided/alignment services as they land
    auto *backgroundActivity = new work::BackgroundActivityRegistry(&app);
    engine.rootContext()->setContextProperty(QStringLiteral("BackgroundActivity"),
                                             backgroundActivity);
```

In `native/CMakeLists.txt`, add to the `colosseum` target source list directly after the `AudioPairingStore.h` line:

```cmake
    work/BackgroundWorkCoordinator.cpp
    work/BackgroundWorkCoordinator.h
    work/BackgroundActivityRegistry.cpp
    work/BackgroundActivityRegistry.h
```

- [ ] **Step 6: Build the app to verify the committed artifact will boot**

```bash
taskkill //IM colosseum.exe //F 2>/dev/null; true
cmake --build native/build-msvc --target colosseum 2>&1 | tail -5
```

Expected: exit 0, links clean.

- [ ] **Step 7: Commit and push**

```bash
git add native/work/BackgroundActivityRegistry.h native/work/BackgroundActivityRegistry.cpp tests/background_activity_registry_harness.cpp native/main.cpp native/CMakeLists.txt
git diff --cached --stat   # exactly 5 files
git commit -m "[Agent 0 (Claude), foundation] feat(work): add unified background-activity registry and app wiring" -- native/work/BackgroundActivityRegistry.h native/work/BackgroundActivityRegistry.cpp tests/background_activity_registry_harness.cpp native/main.cpp native/CMakeLists.txt
git push origin master
```

### Task 3: `BackgroundActivitySection.qml` + Downloads page integration

One prop-driven QML component renders every registry row; the Downloads page embeds it once. After this, neither arc ever edits `DownloadsPage.qml` — they publish into the registry and their row appears.

**Files:**
- Create: `qml/BackgroundActivitySection.qml`
- Modify: `qml/DownloadsPage.qml` (one snippet inside the `col` Column, directly after the `groupsCol` Column block closes — find the anchor with `grep -n 'id: groupsCol' qml/DownloadsPage.qml`, then match braces to its close)
- Create: `tests/background_activity_section_harness.qml`
- Create: `tests/test_background_activity.ps1`

**Design constraints:** gray/white/black only, no emoji, inline metadata, integer `font.pixelSize`, hidden entirely when no activities exist (zero footprint until an arc ships).

- [ ] **Step 1: Write the failing QML harness**

```qml
// tests/background_activity_section_harness.qml
import QtQuick
import "../qml"

Item {
    id: root
    width: 400
    height: 300

    function pass(message) { console.log(message); Qt.exit(0) }
    function fail(message) { console.error("FAIL: " + message); Qt.exit(1) }

    QtObject {
        id: fakeRegistry
        property var activities: [
            { id: "guided:onepiece", title: "Analyzing One Piece pages",
              stage: "Detecting panels", progress: 0.4, paused: false, canPause: true },
            { id: "align:dune", title: "Syncing Dune audiobook",
              stage: "Aligning words", progress: 0.75, paused: true, canPause: true }
        ]
        property var pauseCalls: []
        property var resumeCalls: []
        function requestPause(id) { pauseCalls = pauseCalls.concat([id]) }
        function requestResume(id) { resumeCalls = resumeCalls.concat([id]) }
    }

    QtObject {
        id: emptyRegistry
        property var activities: []
        function requestPause(id) {}
        function requestResume(id) {}
    }

    BackgroundActivitySection {
        id: section
        width: 360
        registry: fakeRegistry
    }

    BackgroundActivitySection {
        id: emptySection
        width: 360
        registry: emptyRegistry
    }

    Component.onCompleted: {
        if (section.rowCount !== 2)
            fail("expected 2 rows, got " + section.rowCount)
        if (!section.visible)
            fail("section with rows must be visible")
        if (emptySection.visible)
            fail("empty section must vanish entirely")
        pass("BACKGROUND_ACTIVITY_SECTION_OK")
    }
}
```

- [ ] **Step 2: Write the runner (harness + grep contracts) and verify it fails**

```powershell
# tests/test_background_activity.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$qmlExe = "C:\Qt\6.11.1\msvc2022_64\bin\qml.exe"
if (!(Test-Path $qmlExe)) { throw "qml.exe not found at $qmlExe - update the Qt path in this test." }
$harness = Join-Path $root "tests\background_activity_section_harness.qml"
$out = cmd /c "`"$qmlExe`" -platform offscreen `"$harness`" 2>&1" | Out-String
if ($out -notlike "*BACKGROUND_ACTIVITY_SECTION_OK*") { Write-Host $out; throw "section harness failed" }

# Wiring contracts (shape, not behavior — pixels are Hemanth's eyes).
$section = Get-Content (Join-Path $root "qml\BackgroundActivitySection.qml") -Raw
if ($section -notlike "*requestPause(modelData.id)*") { throw "Pause control must call requestPause with the row id" }
if ($section -notlike "*requestResume(modelData.id)*") { throw "Resume control must call requestResume with the row id" }

$dl = Get-Content (Join-Path $root "qml\DownloadsPage.qml") -Raw
if ($dl -notlike "*BackgroundActivitySection*") { throw "DownloadsPage must embed BackgroundActivitySection" }
if ($dl -notlike "*typeof BackgroundActivity*") { throw "DownloadsPage must guard the context property for harness loads" }

Write-Host "TEST_BACKGROUND_ACTIVITY_OK"
```

Run: `powershell -NoProfile -File tests/test_background_activity.ps1`
Expected: FAIL — `qml/BackgroundActivitySection.qml` missing.

- [ ] **Step 3: Implement the section component**

```qml
// qml/BackgroundActivitySection.qml
import QtQuick

// Unified rows for long-running background jobs published into
// BackgroundActivityRegistry (context property `BackgroundActivity`):
// guided comic analysis, audiobook text sync, whatever lands next.
// Prop-driven (registry injected) so harnesses can feed a fake.
Column {
    id: root
    property var registry: null
    readonly property var rows: registry ? registry.activities : []
    readonly property int rowCount: rows.length
    visible: rows.length > 0
    spacing: 6

    Text {
        text: "Background activity"
        color: "#9a9a9a"
        font.pixelSize: 12
    }

    Repeater {
        model: root.rows
        delegate: Item {
            width: root.width
            height: 44

            Column {
                anchors.left: parent.left
                anchors.right: controlT.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    text: modelData.title + "  ·  " + modelData.stage
                    color: "#e8e8e8"
                    font.pixelSize: 13
                }
                Rectangle {
                    width: parent.width
                    height: 3
                    radius: 1
                    color: "#2a2a2a"
                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, modelData.progress))
                        height: parent.height
                        radius: parent.radius
                        color: "#c9c9c9"
                    }
                }
            }

            Text {
                id: controlT
                visible: modelData.canPause === true
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: modelData.paused ? "Resume" : "Pause"
                color: "#bdbdbd"
                font.pixelSize: 12

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -8
                    onClicked: modelData.paused
                               ? root.registry.requestResume(modelData.id)
                               : root.registry.requestPause(modelData.id)
                }
            }
        }
    }
}
```

- [ ] **Step 4: Integrate into DownloadsPage**

Inside the `col` Column of `qml/DownloadsPage.qml`, directly after the `groupsCol` Column block closes, add:

```qml
                BackgroundActivitySection {
                    width: parent.width
                    registry: (typeof BackgroundActivity !== "undefined") ? BackgroundActivity : null
                }
```

(The `typeof` guard keeps every existing DownloadsPage harness loading the file without the context property alive.)

- [ ] **Step 5: Run the runner until green**

```bash
powershell -NoProfile -File tests/test_background_activity.ps1
```

Expected: `BACKGROUND_ACTIVITY_SECTION_OK` then `TEST_BACKGROUND_ACTIVITY_OK`.

- [ ] **Step 6: Commit and push**

```bash
git add qml/BackgroundActivitySection.qml qml/DownloadsPage.qml tests/background_activity_section_harness.qml tests/test_background_activity.ps1
git diff --cached --stat   # exactly 4 files
git commit -m "[Agent 0 (Claude), foundation] feat(work): unified background-activity rows on the Downloads page" -- qml/BackgroundActivitySection.qml qml/DownloadsPage.qml tests/background_activity_section_harness.qml tests/test_background_activity.ps1
git push origin master
```

### Task 4: `models::ModelManifest` — generic bundled-model validation

Both arcs bundle offline models described by a `manifest.json` and must reject missing/corrupt files with stable error codes (`model_missing`, `model_checksum_failed`) before creating any inference session. Built once here, neutrally; domain fields (tensor shapes, classes, thresholds, languages) ride along untouched in `extra`.

**Files:**
- Create: `native/models/ModelManifest.h`
- Create: `native/models/ModelManifest.cpp`
- Create: `tests/model_manifest_harness.cpp`
- Modify: `native/CMakeLists.txt` (harness target)

- [ ] **Step 1: Write the failing harness**

```cpp
// tests/model_manifest_harness.cpp
#include "models/ModelManifest.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    require(f.open(QIODevice::WriteOnly), "fixture file writable");
    f.write(bytes);
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    require(dir.isValid(), "temp dir");

    const QByteArray modelBytes = QByteArrayLiteral("fake model weights, deterministic");
    const QString modelPath = dir.filePath(QStringLiteral("tiny.onnx"));
    writeFile(modelPath, modelBytes);
    const QByteArray sha = QCryptographicHash::hash(modelBytes, QCryptographicHash::Sha256).toHex();

    const QString manifestPath = dir.filePath(QStringLiteral("manifest.json"));
    writeFile(manifestPath, QByteArray("{\n"
                                       "  \"schema\": 1,\n"
                                       "  \"modelId\": \"tiny-test\",\n"
                                       "  \"modelVersion\": \"abc1234\",\n"
                                       "  \"file\": \"tiny.onnx\",\n"
                                       "  \"sha256\": \"" + sha + "\",\n"
                                       "  \"license\": \"MIT\",\n"
                                       "  \"classes\": {\"0\": \"panel\", \"1\": \"text\"}\n"
                                       "}\n"));

    models::ManifestError error = models::ManifestError::None;
    auto manifest = models::ModelManifest::load(manifestPath, &error);
    require(manifest.has_value(), "valid manifest loads");
    require(error == models::ManifestError::None, "no error on valid load");
    require(manifest->modelId == QStringLiteral("tiny-test"), "modelId parsed");
    require(manifest->filePath() == modelPath, "filePath resolves beside manifest");
    require(manifest->extra.contains(QStringLiteral("classes")), "domain fields ride in extra");
    require(manifest->validateChecksum() == models::ManifestError::None, "checksum passes");

    // One flipped byte must fail closed.
    writeFile(modelPath, QByteArray("Fake model weights, deterministic"));
    require(manifest->validateChecksum() == models::ManifestError::ChecksumFailed,
            "flipped byte detected");

    QFile::remove(modelPath);
    require(manifest->validateChecksum() == models::ManifestError::FileMissing,
            "missing model file detected");

    models::ManifestError badError = models::ManifestError::None;
    require(!models::ModelManifest::load(dir.filePath(QStringLiteral("ghost.json")), &badError)
                .has_value(),
            "missing manifest rejected");
    require(badError == models::ManifestError::ManifestMissing, "missing manifest code");

    const QString brokenPath = dir.filePath(QStringLiteral("broken.json"));
    writeFile(brokenPath, QByteArrayLiteral("{ not json"));
    require(!models::ModelManifest::load(brokenPath, &badError).has_value(),
            "broken manifest rejected");
    require(badError == models::ManifestError::ManifestInvalid, "broken manifest code");

    require(models::toCode(models::ManifestError::FileMissing) == QStringLiteral("model_missing"),
            "stable code model_missing");
    require(models::toCode(models::ManifestError::ChecksumFailed)
                == QStringLiteral("model_checksum_failed"),
            "stable code model_checksum_failed");

    std::cout << "MODEL_MANIFEST_OK\n";
    return 0;
}
```

- [ ] **Step 2: Register the harness target and verify failure**

```cmake
add_executable(model_manifest_harness
    ../tests/model_manifest_harness.cpp
    models/ModelManifest.cpp
    models/ModelManifest.h)
target_include_directories(model_manifest_harness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(model_manifest_harness PRIVATE Qt6::Core)
```

Run: `cmake --build native/build-msvc --target model_manifest_harness`
Expected: FAIL — header missing.

- [ ] **Step 3: Implement**

```cpp
// native/models/ModelManifest.h
#pragma once

#include <QJsonObject>
#include <QString>

#include <optional>

namespace models {

enum class ManifestError { None, ManifestMissing, ManifestInvalid, FileMissing, ChecksumFailed };

// Stable wire codes shared by every offline-ML feature's failure surface.
QString toCode(ManifestError error);

// Generic bundled-model descriptor (guided comics detector, alignment speech
// models). Core identity + integrity live here; domain-specific fields (tensor
// shapes, classes, thresholds, languages) ride along untouched in `extra`.
class ModelManifest {
public:
    int schema = 0;
    QString modelId;
    QString modelVersion;
    QString file;   // model filename relative to the manifest's directory
    QString sha256; // lowercase hex digest of the model file
    QString license;
    QJsonObject extra; // the full manifest object, for domain readers
    QString dir;       // directory containing manifest.json (set by load)

    QString filePath() const;

    static std::optional<ModelManifest> load(const QString &manifestPath,
                                             ManifestError *error = nullptr);
    ManifestError validateChecksum() const; // streams the file through SHA-256
};

} // namespace models
```

```cpp
// native/models/ModelManifest.cpp
#include "models/ModelManifest.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

namespace models {

QString toCode(ManifestError error)
{
    switch (error) {
    case ManifestError::None:
        return QString();
    case ManifestError::ManifestMissing:
        return QStringLiteral("manifest_missing");
    case ManifestError::ManifestInvalid:
        return QStringLiteral("manifest_invalid");
    case ManifestError::FileMissing:
        return QStringLiteral("model_missing");
    case ManifestError::ChecksumFailed:
        return QStringLiteral("model_checksum_failed");
    }
    return QString();
}

QString ModelManifest::filePath() const
{
    return QDir(dir).filePath(file);
}

std::optional<ModelManifest> ModelManifest::load(const QString &manifestPath,
                                                 ManifestError *error)
{
    auto fail = [&](ManifestError code) -> std::optional<ModelManifest> {
        if (error)
            *error = code;
        return std::nullopt;
    };

    QFile f(manifestPath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return fail(ManifestError::ManifestMissing);

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return fail(ManifestError::ManifestInvalid);

    const QJsonObject obj = doc.object();
    ModelManifest manifest;
    manifest.schema = obj.value(QStringLiteral("schema")).toInt();
    manifest.modelId = obj.value(QStringLiteral("modelId")).toString();
    manifest.modelVersion = obj.value(QStringLiteral("modelVersion")).toString();
    manifest.file = obj.value(QStringLiteral("file")).toString();
    manifest.sha256 = obj.value(QStringLiteral("sha256")).toString().toLower();
    manifest.license = obj.value(QStringLiteral("license")).toString();
    manifest.extra = obj;
    manifest.dir = QFileInfo(manifestPath).absolutePath();

    if (manifest.schema < 1 || manifest.modelId.isEmpty() || manifest.file.isEmpty()
        || manifest.sha256.size() != 64)
        return fail(ManifestError::ManifestInvalid);

    if (error)
        *error = ManifestError::None;
    return manifest;
}

ManifestError ModelManifest::validateChecksum() const
{
    QFile f(filePath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return ManifestError::FileMissing;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f))
        return ManifestError::ChecksumFailed;
    return hash.result().toHex() == sha256.toUtf8() ? ManifestError::None
                                                    : ManifestError::ChecksumFailed;
}

} // namespace models
```

- [ ] **Step 4: Run until green**

```bash
cmake --build native/build-msvc --target model_manifest_harness
native/build-msvc/model_manifest_harness.exe
```

Expected: `MODEL_MANIFEST_OK`, exit 0.

- [ ] **Step 5: Commit and push**

```bash
git add native/models/ModelManifest.h native/models/ModelManifest.cpp tests/model_manifest_harness.cpp native/CMakeLists.txt
git diff --cached --stat   # exactly 4 files
git commit -m "[Agent 0 (Claude), foundation] feat(models): generic bundled-model manifest with checksum gate" -- native/models tests/model_manifest_harness.cpp native/CMakeLists.txt
git push origin master
```

### Task 5: ONNX Runtime seam — dormant until an arc flips it on

Codex's pinned dependency work (guided plan Task 1, dependency half), landed neutrally: the fetch script and imported target exist, gated behind `COLOSSEUM_ENABLE_ONNX` (default OFF) so a fresh clone builds without the ~300 MB runtime staged — the same pattern Player 2 uses with `COLOSSEUM_BUILD_PLAYER2`.

**Files:**
- Create: `scripts/native/fetch_onnxruntime.ps1`
- Create: `native/cmake/OnnxRuntime.cmake`
- Create: `tests/onnx_seam_probe/CMakeLists.txt`
- Modify: `native/CMakeLists.txt` (option + gated include, inserted after the MpvQt block that ends with `find_package(MpvQt REQUIRED)` near line 33)

- [ ] **Step 1: Create the verified fetch script (Codex's pins, verbatim)**

```powershell
# scripts/native/fetch_onnxruntime.ps1
# Developer-time bootstrap for the shared offline-ML seam (guided comics +
# audiobook alignment). Stages ONNX Runtime CPU x64 1.25.0 into C:\tools.
# The installed app never downloads anything; this runs once per dev machine.
$ErrorActionPreference = 'Stop'
$version = '1.25.0'
$expected = 'da753f762bf2400e7191ec594086b186a7051d5af8dc886f6e2020c2403df738'
$zip = Join-Path $env:TEMP "onnxruntime-win-x64-$version.zip"
$dest = "C:\tools\onnxruntime-win-x64-$version"
if (Test-Path "$dest\lib\onnxruntime.lib") { Write-Host "ONNXRUNTIME_READY $dest (already staged)"; exit 0 }
Invoke-WebRequest "https://github.com/microsoft/onnxruntime/releases/download/v$version/onnxruntime-win-x64-$version.zip" -OutFile $zip
if ((Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw 'ONNX Runtime checksum mismatch' }
Expand-Archive -LiteralPath $zip -DestinationPath 'C:\tools' -Force
if (!(Test-Path "$dest\lib\onnxruntime.lib")) { throw 'onnxruntime.lib missing after extraction' }
Write-Host "ONNXRUNTIME_READY $dest"
```

- [ ] **Step 2: Create the imported target (Codex's content, verbatim)**

```cmake
# native/cmake/OnnxRuntime.cmake
set(ONNXRUNTIME_ROOT "C:/tools/onnxruntime-win-x64-1.25.0" CACHE PATH "ONNX Runtime CPU x64 root")
if(NOT EXISTS "${ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h" OR
   NOT EXISTS "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
    message(FATAL_ERROR "ONNX Runtime 1.25.0 missing; run scripts/native/fetch_onnxruntime.ps1")
endif()
add_library(onnxruntime::onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(onnxruntime::onnxruntime PROPERTIES
    IMPORTED_IMPLIB "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib"
    IMPORTED_LOCATION "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_ROOT}/include")
```

- [ ] **Step 3: Gate it in the main build**

In `native/CMakeLists.txt`, after `find_package(MpvQt REQUIRED)`:

```cmake
# --- shared offline-ML seam: ONNX Runtime (guided comics + audiobook alignment) ---
# Dormant by default so a fresh clone builds without the ~300 MB runtime staged.
# Arcs that link it configure with -DCOLOSSEUM_ENABLE_ONNX=ON after running
# scripts/native/fetch_onnxruntime.ps1 once per machine.
option(COLOSSEUM_ENABLE_ONNX "Enable targets that link ONNX Runtime" OFF)
if(COLOSSEUM_ENABLE_ONNX)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/OnnxRuntime.cmake)
endif()
```

- [ ] **Step 4: Create the configure-only probe**

```cmake
# tests/onnx_seam_probe/CMakeLists.txt
# Configure-only proof that the imported ONNX target resolves. Not part of the
# app build; run manually after staging the runtime.
cmake_minimum_required(VERSION 3.16)
project(onnx_seam_probe LANGUAGES CXX)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../native/cmake/OnnxRuntime.cmake)
get_target_property(_onnx_dll onnxruntime::onnxruntime IMPORTED_LOCATION)
message(STATUS "ONNX_SEAM_OK ${_onnx_dll}")
```

- [ ] **Step 5: Stage the runtime and prove the seam both ways**

```bash
powershell -NoProfile -File scripts/native/fetch_onnxruntime.ps1
```

Expected: `ONNXRUNTIME_READY C:\tools\onnxruntime-win-x64-1.25.0` (~300 MB download; allow up to 10 minutes; if offline, note it in the commit message and leave the probe for the first arc to run — the seam is still correct, just unproven on this machine).

```bash
cmake -S tests/onnx_seam_probe -B native/build-onnx-probe 2>&1 | grep ONNX_SEAM_OK
```

Expected: `ONNX_SEAM_OK C:/tools/onnxruntime-win-x64-1.25.0/lib/onnxruntime.dll`

Then prove the main build is untouched with the option OFF (default):

```bash
cmake --build native/build-msvc --target colosseum 2>&1 | tail -3
```

Expected: exit 0, no ONNX mention.

- [ ] **Step 6: Commit and push (probe build dir stays untracked)**

```bash
git add scripts/native/fetch_onnxruntime.ps1 native/cmake/OnnxRuntime.cmake tests/onnx_seam_probe/CMakeLists.txt native/CMakeLists.txt
git diff --cached --stat   # exactly 4 files
git commit -m "[Agent 0 (Claude), foundation] feat(build): dormant ONNX Runtime seam behind COLOSSEUM_ENABLE_ONNX" -- scripts/native/fetch_onnxruntime.ps1 native/cmake/OnnxRuntime.cmake tests/onnx_seam_probe/CMakeLists.txt native/CMakeLists.txt
git push origin master
```

### Task 6: Amend the guided plan, commission A2's plan, announce the fences

The spine exists; now point both arcs at it so neither rebuilds it.

**Files:**
- Modify: `docs/superpowers/plans/2026-07-21-panel-aware-guided-reader.md`
- Create: `docs/superpowers/plans/2026-07-22-a2-alignment-plan-commission.md`
- Modify: `../agents/chat.md` — **the HAVEN repo** (`C:\Users\Suprabha\Desktop\Brotherhood`), a different git repo from Colosseum.

- [ ] **Step 1: Insert the GROUNDWORK APPLIED block into the guided plan**

Directly after the `> **For agentic workers:** ...` line of `docs/superpowers/plans/2026-07-21-panel-aware-guided-reader.md`, insert:

```markdown
> **GROUNDWORK APPLIED (2026-07-22, Agent 0 — see `2026-07-22-background-work-spine-groundwork.md`):**
> The shared spine this plan assumed now EXISTS. Before executing, apply these deltas:
> - **Task 1:** the ONNX dependency half is DONE — `native/cmake/OnnxRuntime.cmake` and the fetch
>   script exist (fetch script lives at `scripts/native/fetch_onnxruntime.ps1`, NOT `scripts/guided/`).
>   Configure ONNX-linking targets with `-DCOLOSSEUM_ENABLE_ONNX=ON`. Task 1 shrinks to
>   `GuidedTypes.h/.cpp` + its harness only.
> - **Task 2:** DONE ENTIRELY — `work::BackgroundWorkCoordinator` is live in `native/work/` with a
>   green harness. Do not recreate it; consume it. Semantics addition: no job is dequeued while
>   pressure is `Suspended`.
> - **Task 6:** do NOT create `native/guided/ModelManifest.h/.cpp`. Use `models::ModelManifest`
>   from `native/models/` (generic core + `extra` for detector fields). Its error codes already
>   emit `model_missing` / `model_checksum_failed`.
> - **Task 7:** do NOT construct a private coordinator in `main.cpp`. One app-owned
>   `work::BackgroundWorkCoordinator *backgroundWork` already exists there (one worker, shared
>   with audiobook alignment) — inject THAT into `PanelAnalysisService`.
> - **DownloadsPage (Task 61 / file-structure entry):** the unified activity row already exists.
>   Do not edit `qml/DownloadsPage.qml`. Publish job state into the `BackgroundActivity` context
>   property (`work::BackgroundActivityRegistry`: `publish/remove` + `pauseRequested/resumeRequested`
>   signals) and your row appears. Required keys: title, stage, progress, paused, canPause.
> - **Priority convention** (shared): current=100, next=90.., previous=80, remainder=10.
```

- [ ] **Step 2: Write the A2 commission prompt file**

Create `docs/superpowers/plans/2026-07-22-a2-alignment-plan-commission.md`:

```markdown
# Commission: task-level implementation plan for Audiobook-EPUB Read-Along (A2 arc)

**To be handed as a PROMPT to the planning substrate (Codex high-reasoning or Fable),
not fired as an MCP call. Paste everything below the line into the planner.**

---

Write the full task-level implementation plan (superpowers writing-plans format: bite-sized
TDD steps, exact paths, complete code, checkbox steps, frequent commits) for the approved
design `docs/superpowers/specs/2026-07-21-audiobook-epub-read-along-design.md` in the
Colosseum repo (`C:\Users\Suprabha\Desktop\Brotherhood\Colosseum`).

**The shared spine already EXISTS (built 2026-07-22 by Agent 0 — see
`docs/superpowers/plans/2026-07-22-background-work-spine-groundwork.md`). Your plan MUST
consume it, not rebuild it:**

1. **Scheduling:** the design's `AlignmentScheduler` is a THIN DOMAIN WRAPPER over the existing
   `work::BackgroundWorkCoordinator` (`native/work/BackgroundWorkCoordinator.h`). One chapter =
   one submitted work unit; use `WorkContext::checkpoint()` at safe stage boundaries and
   `shouldYield()` inside long loops. Do not create threads or a second scheduler.
   The app-owned instance `backgroundWork` in `native/main.cpp` is shared with guided comic
   analysis (one worker total, by design) — inject it into `AudioTextAlignmentService`.
   Priority convention: current chapter=100, next=90, previous=80, remainder=10.
2. **Status surfaces:** the "global background activity surface" from the design is DONE.
   Publish presentation-shaped state into `work::BackgroundActivityRegistry` (context property
   `BackgroundActivity`): keys title, stage, progress (0..1), paused, canPause; listen on
   `pauseRequested`/`resumeRequested`. Do NOT edit `qml/DownloadsPage.qml` — the row renders
   automatically. The Reader2 Audio-panel Text Sync row remains yours to build.
3. **Model bundling:** use `models::ModelManifest` (`native/models/ModelManifest.h`) for every
   bundled speech model (whisper base.en, wav2vec2 ONNX export). Its stable codes
   `model_missing`/`model_checksum_failed` match the design's failure table. Domain fields
   (language, engine compatibility, input requirements) go in the manifest JSON and are read
   from `.extra`.
4. **ONNX linking:** gate every ONNX-linking target behind the existing
   `COLOSSEUM_ENABLE_ONNX` option; the runtime stages via `scripts/native/fetch_onnxruntime.ps1`
   (pinned 1.25.0). whisper.cpp vendoring is yours to plan.
5. **Ownership fences:** you own the alignment domain dirs + Reader2 surfaces. You do NOT touch
   `native/main.cpp`, `native/CMakeLists.txt` shared regions, `qml/DownloadsPage.qml`, or
   `native/work/` / `native/models/` — service registration lines in `main.cpp` are written as
   an Agent 0 handoff step in your plan (mark them "Agent 0 applies").
6. Follow the repo's harness style: `require(cond, msg)` C++ harnesses printing a named sentinel,
   ps1 runners, grep contracts for QML wiring shape.

Deliver the plan to `docs/superpowers/plans/2026-07-22-audiobook-epub-read-along.md`.
```

- [ ] **Step 3: Commit and push the Colosseum side**

```bash
git add docs/superpowers/plans/2026-07-21-panel-aware-guided-reader.md docs/superpowers/plans/2026-07-22-a2-alignment-plan-commission.md
git diff --cached --stat   # exactly 2 files
git commit -m "[Agent 0 (Claude), governance] docs: point both arcs at the shared spine" -- docs/superpowers/plans/2026-07-21-panel-aware-guided-reader.md docs/superpowers/plans/2026-07-22-a2-alignment-plan-commission.md
git push origin master
```

- [ ] **Step 4: Announce on the haven cross-lane wire**

Append to `C:\Users\Suprabha\Desktop\Brotherhood\agents\chat.md` (haven repo — separate git):

```markdown
## [Agent 0 (Claude), governance] 2026-07-22 — Background-work spine SHIPPED; fences for parallel A1/A2 arcs

The one thing A1's guided comics and A2's audio-text alignment have in common is now built once,
in Colosseum master: `work::BackgroundWorkCoordinator` (single shared worker, priority
current=100/next=90/previous=80/remainder=10, pause/cancel/pressure), `work::BackgroundActivityRegistry`
+ `BackgroundActivitySection.qml` (publish into `BackgroundActivity`, your Downloads row appears —
do NOT edit DownloadsPage), `models::ModelManifest` (bundled-model checksum gate,
`model_missing`/`model_checksum_failed`), and a dormant ONNX Runtime 1.25.0 seam behind
`-DCOLOSSEUM_ENABLE_ONNX=ON` (`scripts/native/fetch_onnxruntime.ps1`).

**Fences while both arcs run:** A1 = `native/guided/` + `qml/guided/` (+ MangaReader.qml).
A2 = alignment domain + Reader2 surfaces. ONLY Agent 0 touches `native/main.cpp`,
`native/CMakeLists.txt` shared regions, `qml/DownloadsPage.qml`, `native/work/`, `native/models/`,
packaging. Service-registration lines in main.cpp: write them as "Agent 0 applies" handoff steps.

A1: your plan has a GROUNDWORK APPLIED block at the top now — read it before Task 1; Tasks 2 is
done for you and Task 1 halved. A2: your plan doesn't exist yet — the commission prompt is
`Colosseum/docs/superpowers/plans/2026-07-22-a2-alignment-plan-commission.md`.
Also: the repo divergence is healed — Player 2's 10 commits are pushed and safe.
```

```bash
cd "C:/Users/Suprabha/Desktop/Brotherhood"
git add agents/chat.md
git diff --cached --stat   # exactly 1 file
git commit -m "[Agent 0 (Claude), governance] chat: announce background-work spine + parallel-arc fences" -- agents/chat.md
git push
```

---

## Definition of Done

- Colosseum master reconciled: 0 ahead / 0 behind, Player 2 commits on origin, Hemanth's dirt intact.
- Four green harnesses: `BACKGROUND_WORK_OK`, `BACKGROUND_ACTIVITY_REGISTRY_OK`, `BACKGROUND_ACTIVITY_SECTION_OK` (+ `TEST_BACKGROUND_ACTIVITY_OK`), `MODEL_MANIFEST_OK`.
- `colosseum` target builds clean with the registry + coordinator wired and the ONNX option OFF; ONNX probe prints `ONNX_SEAM_OK` with it staged.
- Guided plan carries the GROUNDWORK APPLIED block; A2 commission file exists; haven chat.md announces the fences.
- Every commit pushed, explicit-pathspec, substrate-attributed.
- After this plan, A1 and A2 can be summoned in parallel with zero shared-file overlap.
