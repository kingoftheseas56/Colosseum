// tests/alignment_service_harness.cpp
//
// TDD harness for alignment::AudioTextAlignmentService — the QML facade that schedules
// resumable per-chapter audiobook↔EPUB alignment jobs on the app-owned
// work::BackgroundWorkCoordinator, with a FAKE ChapterProcessor proving the whole
// scheduling / pause / failure / retry / restart machinery before any real speech AI
// exists. A real coordinator + real registry + real store (temp FILE db) + a real
// AudioPairingStore + a fake localFiles + the fake processor drive six scenarios.
//
// DETERMINISM: BackgroundWorkCoordinator::runnableAvailableLocked() returns false under
// Pressure::Suspended, so the single worker does NOT dequeue while Suspended (verified
// in BackgroundWorkCoordinator.cpp::workerLoop). The priority-order test therefore holds
// the worker with setPressure(Suspended), submits + prioritizes ALL chapters (so every
// job carries its final priority before anything runs), then setPressure(Normal) and lets
// the one worker drain in exact priority order — the same technique the panel-analysis
// harness uses. Completion is synced on workFinished/workFailed inside a bounded pump
// loop (never hangs; fails on timeout).
//
// Fake Ready cues: one contiguous Aligned sentence covering the full nominal chapter
// bounds → coverage 1.0 ≥ the store's 0.80 gate → the chapter publishes Ready.
//
// Exit code is the verdict. Run from native/build-msvc so the deployed
// sqldrivers/qsqlite.dll (+ Qt DLLs) beside the exe are found.

#include "alignment/AudioTextAlignmentService.h"
#include "alignment/AlignmentStore.h"
#include "alignment/AlignmentTypes.h"
#include "AudioPairingStore.h"
#include "work/BackgroundActivityRegistry.h"
#include "work/BackgroundWorkCoordinator.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

using namespace alignment;

static int g_fails = 0;
#define CHECK(cond, name) do { \
    if (cond) { std::printf("PASS %s\n", (name)); } \
    else { std::printf("FAIL %s\n", (name)); ++g_fails; } \
} while (0)

// ── A configurable, thread-safe fake ChapterProcessor ─────────────────────────
// process() runs on the worker thread. It records entry order (for the priority
// proof), can stall a chosen chapter mid-Transcribing on a latch (to observe partial
// state and pause-preservation), and can return a terminal failure for a chosen
// chapter. On the happy path it emits one full-coverage aligned cue so the store's
// Ready gate passes.
class FakeChapterProcessor {
public:
    std::set<int> failIndices;   // → ChapterResult.failure = ChapterMatchMissing
    std::set<int> blockIndices;  // → stall after reporting Transcribing until releaseAll()

    work::WorkResult process(const ChapterInput& in, work::WorkContext& ctx,
                             const ChapterReport& report, ChapterResult& out) {
        { std::lock_guard<std::mutex> g(m_mutex); m_order.push_back(in.chapterIndex); }
        m_entered.fetch_add(1);

        if (!ctx.checkpoint()) return work::WorkResult::Cancelled;
        report(Stage::Preparing, 0.25);
        if (!ctx.checkpoint()) return work::WorkResult::Cancelled;
        report(Stage::Transcribing, 0.5);

        if (blockIndices.count(in.chapterIndex)) {
            m_atBlock.fetch_add(1);
            std::unique_lock<std::mutex> lk(m_blockMutex);
            m_blockCv.wait(lk, [this] { return m_released; });
        }

        if (!ctx.checkpoint()) return work::WorkResult::Cancelled; // honors pause if still held
        report(Stage::Aligning, 0.85);

        if (failIndices.count(in.chapterIndex)) {
            out.failure = FailureCode::ChapterMatchMissing;
            return work::WorkResult::Completed; // service maps out.failure → failChapter
        }

        SentenceCue s;
        s.ordinal = 0;
        s.startMs = in.audioStartMs;
        s.endMs = in.audioEndMs;
        s.spineHref = QStringLiteral("chap-%1").arg(in.chapterIndex);
        s.canonicalStart = 0;
        s.canonicalEnd = 100;
        s.sentenceHash = QStringLiteral("hash-%1").arg(in.chapterIndex);
        s.confidence = 0.95;
        s.regionKind = RegionKind::Aligned;
        out.sentences.append(s);
        out.confidence = 0.95;
        return work::WorkResult::Completed;
    }

    void releaseAll() {
        { std::lock_guard<std::mutex> g(m_blockMutex); m_released = true; }
        m_blockCv.notify_all();
    }
    int entered() const { return m_entered.load(); }
    int atBlock() const { return m_atBlock.load(); }
    std::vector<int> order() { std::lock_guard<std::mutex> g(m_mutex); return m_order; }

private:
    mutable std::mutex m_mutex;
    std::vector<int> m_order;
    std::atomic<int> m_entered{0};
    std::atomic<int> m_atBlock{0};
    std::mutex m_blockMutex;
    std::condition_variable m_blockCv;
    bool m_released = false;
};

// ── The rig: real deps + the service, ordered so teardown is race-free ────────
// Destruction is reverse declaration order: service (cancels all work) → coordinator
// (joins the idle worker) → fake → readStore → pairing → registry. Every scenario
// drains to terminal before letting the rig go out of scope, so no work fn is ever
// mid-run when the service/fake are destroyed.
struct Rig {
    work::BackgroundActivityRegistry registry;
    AudioPairingStore pairing;
    std::unique_ptr<AlignmentStore> readStore;
    FakeChapterProcessor fake;
    work::BackgroundWorkCoordinator coordinator{1};
    std::unique_ptr<AudioTextAlignmentService> service;
    std::atomic<int> terminal{0};

    Rig(const QString& dbPath, int nChapters) {
        readStore = std::make_unique<AlignmentStore>(dbPath);
        auto localFiles = [nChapters](QString) {
            QStringList l;
            for (int i = 0; i < nChapters; ++i)
                l << QStringLiteral("audio-%1.mp3").arg(i);
            return l;
        };
        service = std::make_unique<AudioTextAlignmentService>(
            &coordinator, &registry, readStore.get(), dbPath, &pairing, localFiles,
            [this](const ChapterInput& in, work::WorkContext& ctx,
                   const ChapterReport& report, ChapterResult& out) {
                return fake.process(in, ctx, report, out);
            });
        QObject::connect(&coordinator, &work::BackgroundWorkCoordinator::workFinished,
                         service.get(), [this](const QString&) { terminal.fetch_add(1); },
                         Qt::DirectConnection);
        QObject::connect(&coordinator, &work::BackgroundWorkCoordinator::workFailed,
                         service.get(), [this](const QString&, const QString&) { terminal.fetch_add(1); },
                         Qt::DirectConnection);
    }
};

// Pump the event loop until pred() holds or the timeout elapses. Returns pred()'s final
// value (so callers can assert it, turning a timeout into a FAIL rather than a hang).
static bool waitFor(const std::function<bool()>& pred, int timeoutMs = 15000) {
    QElapsedTimer t; t.start();
    while (!pred() && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    return pred();
}

// The single audio-alignment activity row (there is one pair per rig).
static QVariantMap alignRow(work::BackgroundActivityRegistry& reg) {
    const QVariantList acts = reg.activities();
    for (const QVariant& v : acts) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toString().startsWith(QStringLiteral("audio-align:")))
            return m;
    }
    return {};
}

// ── Scenario 1: deterministic priority order + full drain to Ready + idempotency ──
static void scenarioPriorityOrder(const QString& db) {
    const int N = 6;
    Rig rig(db, N);
    const QString bookId = QStringLiteral("book-prio");
    const QString pairKey = QStringLiteral("Priority Tale|Author");

    rig.coordinator.setPressure(work::Pressure::Suspended);
    rig.service->ensurePair(bookId, QStringLiteral("C:/books/prio.epub"), pairKey);
    rig.service->prioritize(bookId, 2);                 // focus chapter 2
    rig.coordinator.setPressure(work::Pressure::Normal);

    CHECK(waitFor([&] { return rig.terminal.load() >= N; }),
          "priority: all 6 chapters reached a terminal result within timeout");

    const std::vector<int> order = rig.fake.order();
    const int want[N] = {2, 3, 4, 5, 1, 0};             // current → next-4 → previous → remainder
    bool orderOk = static_cast<int>(order.size()) >= N;
    for (int i = 0; orderOk && i < N; ++i) orderOk = (order[i] == want[i]);
    CHECK(orderOk, "priority: worker visited chapters in focus order {2,3,4,5,1,0}");

    const QVariantMap st = rig.service->statusFor(bookId);
    CHECK(st.value(QStringLiteral("ready")).toInt() == N
          && st.value(QStringLiteral("total")).toInt() == N
          && st.value(QStringLiteral("stage")).toString() == QStringLiteral("ready"),
          "priority: statusFor reports 6/6 ready");

    // Idempotency: re-ensure upgrades/re-syncs but re-runs no already-Ready chapter and
    // never duplicates a chapter row.
    const int enteredAfterDrain = rig.fake.entered();
    rig.service->ensurePair(bookId, QStringLiteral("C:/books/prio.epub"), pairKey);
    waitFor([] { return false; }, 60);                  // give any (unexpected) re-run a chance to start
    CHECK(rig.fake.entered() == enteredAfterDrain,
          "priority: re-ensure re-ran no already-Ready chapter");
    CHECK(rig.service->chaptersFor(bookId).size() == N,
          "priority: re-ensure kept exactly 6 chapters (no duplicates)");
}

// Read one chapter's current stage string through the façade (which reads the store).
static QString stageOf(AudioTextAlignmentService& svc, const QString& bookId, int idx) {
    const QVariantList cs = svc.chaptersFor(bookId);
    if (idx < 0 || idx >= cs.size()) return QString();
    return cs[idx].toMap().value(QStringLiteral("stage")).toString();
}

// ── Scenario 2: chapter-local Ready — an early chapter is Ready while later ones wait ──
static void scenarioChapterLocalReady(const QString& db) {
    const int N = 4;
    Rig rig(db, N);
    rig.fake.blockIndices = {1};                        // ch1 stalls mid-transcribe
    const QString bookId = QStringLiteral("book-local");
    const QString pairKey = QStringLiteral("Local Tale|Author");

    rig.coordinator.setPressure(work::Pressure::Suspended);
    rig.service->ensurePair(bookId, QStringLiteral("C:/books/local.epub"), pairKey);
    rig.coordinator.setPressure(work::Pressure::Normal); // focus 0 → order 0,1,2,3

    const bool reached = waitFor([&] {
        return stageOf(*rig.service, bookId, 0) == QStringLiteral("ready")
            && rig.fake.atBlock() >= 1;
    });
    CHECK(reached, "chapter-local: ch0 Ready while ch1 is stalled in-progress");
    CHECK(stageOf(*rig.service, bookId, 0) == QStringLiteral("ready"),
          "chapter-local: ch0 store status is Ready");
    CHECK(stageOf(*rig.service, bookId, 1) == QStringLiteral("transcribing"),
          "chapter-local: ch1 is still in-progress (transcribing)");
    CHECK(stageOf(*rig.service, bookId, 2) == QStringLiteral("waiting")
          && stageOf(*rig.service, bookId, 3) == QStringLiteral("waiting"),
          "chapter-local: later chapters 2 and 3 are still Waiting");

    rig.fake.releaseAll();
    CHECK(waitFor([&] { return rig.terminal.load() >= N; }),
          "chapter-local: all 4 chapters drain to terminal after release");
    CHECK(rig.service->statusFor(bookId).value(QStringLiteral("ready")).toInt() == N,
          "chapter-local: all 4 chapters end Ready");
}

// ── Scenario 3: visible failures — one chapter fails, later chapter still reaches Ready ──
static void scenarioVisibleFailure(const QString& db) {
    const int N = 3;
    Rig rig(db, N);
    rig.fake.failIndices = {1};
    const QString bookId = QStringLiteral("book-fail");
    const QString pairKey = QStringLiteral("Fail Tale|Author");

    rig.coordinator.setPressure(work::Pressure::Suspended);
    rig.service->ensurePair(bookId, QStringLiteral("C:/books/fail.epub"), pairKey);
    rig.coordinator.setPressure(work::Pressure::Normal);

    CHECK(waitFor([&] { return rig.terminal.load() >= N; }),
          "visible-failure: all 3 chapters reached a terminal result");

    const QVariantList cs = rig.service->chaptersFor(bookId);
    const QVariantMap c0 = cs.value(0).toMap();
    const QVariantMap c1 = cs.value(1).toMap();
    const QVariantMap c2 = cs.value(2).toMap();
    CHECK(c0.value(QStringLiteral("stage")).toString() == QStringLiteral("ready"),
          "visible-failure: ch0 is Ready");
    CHECK(c1.value(QStringLiteral("stage")).toString() == QStringLiteral("couldnt_sync")
          && c1.value(QStringLiteral("failureCode")).toString() == QStringLiteral("chapter_match_missing"),
          "visible-failure: ch1 is CouldntSync + chapter_match_missing (a visible failure)");
    CHECK(c2.value(QStringLiteral("stage")).toString() == QStringLiteral("ready"),
          "visible-failure: ch2 still reaches Ready (failure did not block a later chapter)");
}

// ── Scenario 4: pause preservation — a paused chapter keeps its stage, resumes not restarts ──
static void scenarioPausePreservation(const QString& db) {
    const int N = 1;
    Rig rig(db, N);
    rig.fake.blockIndices = {0};                        // hold ch0 at transcribing so we can pause it there
    const QString bookId = QStringLiteral("book-pause");
    const QString pairKey = QStringLiteral("Pause Tale|Author");

    rig.coordinator.setPressure(work::Pressure::Normal);
    rig.service->ensurePair(bookId, QStringLiteral("C:/books/pause.epub"), pairKey);

    CHECK(waitFor([&] {
        return rig.fake.atBlock() >= 1 && stageOf(*rig.service, bookId, 0) == QStringLiteral("transcribing");
    }), "pause: ch0 reached transcribing (mid-work) before pausing");

    rig.service->pause(bookId);
    CHECK(rig.service->statusFor(bookId).value(QStringLiteral("paused")).toBool(),
          "pause: statusFor paused==true after pause(bookId)");
    CHECK(alignRow(rig.registry).value(QStringLiteral("paused")).toBool(),
          "pause: activity row paused==true matches the service");
    CHECK(stageOf(*rig.service, bookId, 0) == QStringLiteral("transcribing"),
          "pause: paused chapter keeps its stage (still transcribing, not reset)");

    rig.service->resume(bookId);
    CHECK(!rig.service->statusFor(bookId).value(QStringLiteral("paused")).toBool(),
          "resume: statusFor paused==false after resume");
    CHECK(!alignRow(rig.registry).value(QStringLiteral("paused")).toBool(),
          "resume: activity row paused==false matches");
    CHECK(stageOf(*rig.service, bookId, 0) == QStringLiteral("transcribing"),
          "resume: stage still preserved (resumes from transcribing, not restarted to waiting)");

    rig.fake.releaseAll();
    CHECK(waitFor([&] { return rig.terminal.load() >= N; }),
          "resume: chapter runs to a terminal result after resume");
    CHECK(stageOf(*rig.service, bookId, 0) == QStringLiteral("ready"),
          "resume: chapter completes to Ready (resumed to completion, never restarted)");
}

// ── Scenario 5: activity/service pause parity — registry row and service.pause agree ──
static void scenarioActivityParity(const QString& db) {
    const int N = 2;
    Rig rig(db, N);
    const QString bookId = QStringLiteral("book-parity");
    const QString pairKey = QStringLiteral("Parity Tale|Author");

    rig.coordinator.setPressure(work::Pressure::Suspended); // hold so chapters stay Waiting
    rig.service->ensurePair(bookId, QStringLiteral("C:/books/parity.epub"), pairKey);

    const QString rowId = alignRow(rig.registry).value(QStringLiteral("id")).toString();
    CHECK(!rowId.isEmpty(), "parity: an audio-align activity row exists after ensurePair");

    rig.registry.requestPause(rowId);                   // drive pause via the global activity row
    const bool svcPausedViaRow = rig.service->statusFor(bookId).value(QStringLiteral("paused")).toBool();
    const bool rowPausedViaRow = alignRow(rig.registry).value(QStringLiteral("paused")).toBool();
    CHECK(svcPausedViaRow, "parity: registry.requestPause drove the service into paused");
    CHECK(rowPausedViaRow, "parity: registry row shows paused after requestPause");

    rig.registry.requestResume(rowId);
    CHECK(!rig.service->statusFor(bookId).value(QStringLiteral("paused")).toBool(),
          "parity: requestResume cleared service paused");
    CHECK(!alignRow(rig.registry).value(QStringLiteral("paused")).toBool(),
          "parity: requestResume cleared row paused");

    rig.service->pause(bookId);                         // now drive the SAME state via the service
    CHECK(rig.service->statusFor(bookId).value(QStringLiteral("paused")).toBool() == svcPausedViaRow,
          "parity: service.pause yields the same service paused state as the registry row");
    CHECK(alignRow(rig.registry).value(QStringLiteral("paused")).toBool() == rowPausedViaRow,
          "parity: service.pause yields the same row paused state as the registry row");

    rig.service->resume(bookId);
    rig.coordinator.setPressure(work::Pressure::Normal);
    CHECK(waitFor([&] { return rig.terminal.load() >= N; }),
          "parity: chapters drain to terminal after resume");
}

// ── Scenario 6: restart re-schedules all chapters (the confirmation seam) ──
static void scenarioRestart(const QString& db) {
    const int N = 3;
    Rig rig(db, N);
    const QString bookId = QStringLiteral("book-restart");
    const QString pairKey = QStringLiteral("Restart Tale|Author");

    rig.coordinator.setPressure(work::Pressure::Suspended);
    rig.service->ensurePair(bookId, QStringLiteral("C:/books/restart.epub"), pairKey);
    rig.coordinator.setPressure(work::Pressure::Normal);
    CHECK(waitFor([&] { return rig.terminal.load() >= N; }),
          "restart: initial run reached 3 terminal results");
    CHECK(rig.service->statusFor(bookId).value(QStringLiteral("ready")).toInt() == N,
          "restart: initial run reached 3/3 ready");
    CHECK(rig.fake.entered() == N, "restart: 3 chapters ran exactly once before restart");

    rig.service->restart(bookId);                       // reset + reschedule ALL chapters
    CHECK(waitFor([&] { return rig.terminal.load() >= 2 * N; }),
          "restart: 3 more terminal results after restart (all re-scheduled)");
    CHECK(rig.fake.entered() == 2 * N,
          "restart: all 3 chapters were re-scheduled and re-ran (entered 6 total)");
    CHECK(rig.service->statusFor(bookId).value(QStringLiteral("ready")).toInt() == N,
          "restart: chapters re-reach 3/3 ready after restart");
}

// ── Scenario 7: retry — a failed chapter re-runs and can then reach Ready ──
// (Not in the spec's required-assertion list, but the retry() façade is otherwise
// unexercised; this proves store.retryChapter + resubmit works end to end.)
static void scenarioRetry(const QString& db) {
    const int N = 2;
    Rig rig(db, N);
    rig.fake.failIndices = {0};                         // ch0 fails on the first attempt
    const QString bookId = QStringLiteral("book-retry");
    const QString pairKey = QStringLiteral("Retry Tale|Author");

    rig.coordinator.setPressure(work::Pressure::Suspended);
    rig.service->ensurePair(bookId, QStringLiteral("C:/books/retry.epub"), pairKey);
    rig.coordinator.setPressure(work::Pressure::Normal);
    CHECK(waitFor([&] { return rig.terminal.load() >= N; }),
          "retry: initial run reached 2 terminal results");
    CHECK(stageOf(*rig.service, bookId, 0) == QStringLiteral("couldnt_sync"),
          "retry: ch0 starts as a visible failure (couldnt_sync)");

    rig.fake.failIndices.clear();                        // the retry will now succeed
    rig.service->retry(bookId, 0);
    CHECK(waitFor([&] { return rig.terminal.load() >= N + 1; }),
          "retry: retried chapter produced one more terminal result");
    CHECK(stageOf(*rig.service, bookId, 0) == QStringLiteral("ready"),
          "retry: ch0 reaches Ready after retry (reset to Waiting then re-run)");
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    // Test-scoped identity: AudioPairingStore uses default QSettings, which resolves via
    // the app org/app names. Distinct test names keep it off the user's live registry hive
    // (the alignment service never writes pairings, but this is belt-and-braces).
    app.setOrganizationName(QStringLiteral("BrotherhoodTest"));
    app.setApplicationName(QStringLiteral("ColosseumAlignmentServiceHarness"));

    QTemporaryDir dir;
    if (!dir.isValid()) { std::printf("FAIL temp dir invalid\n"); return 1; }

    scenarioPriorityOrder(dir.filePath(QStringLiteral("prio.db")));
    scenarioChapterLocalReady(dir.filePath(QStringLiteral("local.db")));
    scenarioVisibleFailure(dir.filePath(QStringLiteral("fail.db")));
    scenarioPausePreservation(dir.filePath(QStringLiteral("pause.db")));
    scenarioActivityParity(dir.filePath(QStringLiteral("parity.db")));
    scenarioRestart(dir.filePath(QStringLiteral("restart.db")));
    scenarioRetry(dir.filePath(QStringLiteral("retry.db")));

    if (g_fails) std::printf("\nVERDICT: FAIL (%d failing checks)\n", g_fails);
    else std::printf("\nALIGNMENT_SERVICE_OK\nVERDICT: PASS\n");
    return g_fails ? 1 : 0;
}
