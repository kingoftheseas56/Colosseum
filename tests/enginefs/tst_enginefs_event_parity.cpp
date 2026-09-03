#include "EngineFsControlPlane.h"

#include <QtTest>

#include <limits>
#include <memory>

using namespace Colosseum::Server::EngineFs;

namespace {

class FakeScheduler final : public IEngineFsTimerScheduler
{
public:
    TimerId schedule(std::chrono::milliseconds delay, std::function<void()> callback) override
    {
        const TimerId id = ++nextId_;
        tasks_.push_back({id, nowMs_ + delay.count(), std::move(callback), false});
        return id;
    }

    void cancel(TimerId id) override
    {
        for (auto& task : tasks_)
            if (task.id == id)
                task.cancelled = true;
    }

    void advance(std::chrono::milliseconds amount)
    {        const qint64 target = nowMs_ + amount.count();
        for (;;) {
            int chosen = -1;
            qint64 due = std::numeric_limits<qint64>::max();
            for (int i = 0; i < tasks_.size(); ++i) {
                const auto& task = tasks_.at(i);
                if (!task.cancelled && task.dueMs <= target && task.dueMs < due) {
                    chosen = i;
                    due = task.dueMs;
                }
            }
            if (chosen < 0)
                break;
            nowMs_ = due;
            auto callback = std::move(tasks_[chosen].callback);
            tasks_[chosen].cancelled = true;
            callback();
        }
        nowMs_ = target;
    }

private:
    struct Task {
        TimerId id;
        qint64 dueMs;
        std::function<void()> callback;
        bool cancelled;
    };
    QVector<Task> tasks_;
    TimerId nextId_ = 0;
    qint64 nowMs_ = 0;
};

class FakeBackend final : public IEngineFsBackend
{
public:
    void resumeSwarm() override {}
    void pauseSwarm() override {}
    void destroy(std::function<void()> done) override { if (done) done(); }
    void whenReady(std::function<void(const QVariant&)> callback) override
    {
        if (callback)
            callback({});
    }
    void setCallbacks(EngineFsBackendCallbacks callbacks) override
    {
        callbacks_ = std::move(callbacks);
    }
    EngineFsBackendSnapshot statisticsSnapshot() const override { return {}; }

private:
    EngineFsBackendCallbacks callbacks_;
};

class FakeFactory final : public IEngineFsBackendFactory
{
public:
    std::shared_ptr<IEngineFsBackend> create(const QString&,
                                             const QJsonObject&) override
    {
        return std::make_shared<FakeBackend>();
    }
};

QStringList eventNames(const QVector<EngineFsEvent>& events)
{
    QStringList names;
    for (const auto& event : events)
        names.push_back(event.name);
    return names;
}

} // namespace

class EngineFsEventParityTest : public QObject
{
    Q_OBJECT

private slots:
    void rawStreamEventsPrecedeDerivedActivity();
    void removalDuringGraceSuppressesInactiveEvents();
};

void EngineFsEventParityTest::rawStreamEventsPrecedeDerivedActivity()
{
    FakeFactory factory;
    FakeScheduler scheduler;
    QVector<EngineFsEvent> events;
    EngineFsControlPlane plane(factory, scheduler, [&](const EngineFsEvent& event) {
        events.push_back(event);
    });

    const QString hash = QStringLiteral("abc123");
    plane.createEngine(hash, {});
    events.clear();

    plane.noteStreamOpen(hash, 5);
    QCOMPARE(eventNames(events),
             QStringList({QStringLiteral("stream-open"),
                          QStringLiteral("stream-active"),
                          QStringLiteral("stream-active:abc123:5"),
                          QStringLiteral("engine-active"),
                          QStringLiteral("engine-active:abc123")}));

    events.clear();
    plane.noteStreamClose(hash, 5);
    QCOMPARE(eventNames(events), QStringList{QStringLiteral("stream-close")});
}

void EngineFsEventParityTest::removalDuringGraceSuppressesInactiveEvents()
{
    FakeFactory factory;
    FakeScheduler scheduler;    QVector<EngineFsEvent> events;
    EngineFsControlPlane plane(factory, scheduler, [&](const EngineFsEvent& event) {
        events.push_back(event);
    });

    const QString hash = QStringLiteral("deadbeef");
    plane.createEngine(hash, {});
    events.clear();
    plane.noteStreamOpen(hash, 1);
    plane.noteStreamClose(hash, 1);
    events.clear();

    plane.removeEngine(hash);
    events.clear();
    scheduler.advance(std::chrono::seconds(121));

    QVERIFY(events.isEmpty());
    QVERIFY(!plane.exists(hash));
}

QTEST_APPLESS_MAIN(EngineFsEventParityTest)
#include "tst_enginefs_event_parity.moc"
