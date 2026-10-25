#include "player/mpvitem.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QtTest>

#include <algorithm>
#include <cmath>

namespace {

QString fixturePath(const QString &name)
{
    return QStringLiteral(VAULT_FIXTURES_DIR) + QStringLiteral("/media/") + name;
}

struct WindowedPlayer final {
    QQuickWindow window;
    MpvItem *item{nullptr};

    WindowedPlayer()
    {
        item = new MpvItem(window.contentItem());
        item->setWidth(320);
        item->setHeight(240);
        item->setVolume(0);
        window.resize(320, 240);
        window.show();
    }
};

bool hasEvent(const QSignalSpy &spy, const QString &kind, qint64 entryId,
              quint64 generation)
{
    for (const auto &args : spy) {
        if (args.size() == 7 && args.at(0).toString() == kind
            && args.at(1).toLongLong() == entryId
            && args.at(4).toULongLong() == generation) {
            return true;
        }
    }
    return false;
}

bool hasSeekReply(const QSignalSpy &spy, quint64 serial, bool success,
                  quint64 generation)
{
    for (const auto &args : spy) {
        if (args.size() == 4 && args.at(0).toULongLong() == serial
            && args.at(1).toBool() == success
            && args.at(2).toULongLong() == generation) {
            return true;
        }
    }
    return false;
}

} // namespace

class tst_mpvitem_native_events final : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void nativeEntryAndSeekEventsCarryTheLoadGeneration();
};

void tst_mpvitem_native_events::nativeEntryAndSeekEventsCarryTheLoadGeneration()
{
    WindowedPlayer player;
    QVERIFY(QTest::qWaitForWindowExposed(&player.window));

    QSignalSpy nativeEvents(player.item, &MpvItem::nativePlaybackEvent);
    QSignalSpy seekReplies(player.item, &MpvItem::trackedSeekReply);
    QVERIFY(nativeEvents.isValid());
    QVERIFY(seekReplies.isValid());

    const quint64 beforeLoad = player.item->playbackGeneration();
    player.item->loadFile(fixturePath(QStringLiteral("tiny.mp4")));
    const quint64 generation = player.item->playbackGeneration();
    QVERIFY(generation > beforeLoad);

    QTRY_VERIFY_WITH_TIMEOUT(
        std::any_of(nativeEvents.cbegin(), nativeEvents.cend(), [generation](const auto &args) {
            return args.size() == 7 && args.at(0).toString() == QLatin1String("loaded")
                && args.at(1).toLongLong() >= 0
                && args.at(4).toULongLong() == generation;
        }), 5000);

    qint64 entryId = -1;
    for (const auto &args : nativeEvents) {
        if (args.size() == 7 && args.at(0).toString() == QLatin1String("loaded")
            && args.at(4).toULongLong() == generation) {
            entryId = args.at(1).toLongLong();
            QVERIFY(std::isfinite(args.at(2).toDouble()));
            QVERIFY(std::isfinite(args.at(3).toDouble()));
            QVERIFY(args.at(3).toDouble() > 0.0);
            QVERIFY(args.at(5).toLongLong() > 0);
            QVERIFY(args.at(6).toULongLong() > 0);
            break;
        }
    }
    QVERIFY(entryId >= 0);

    player.item->seekExactTracked(0.25, 73);
    QTRY_VERIFY_WITH_TIMEOUT(hasSeekReply(seekReplies, 73, true, generation), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(hasEvent(nativeEvents, QStringLiteral("seek-start"),
                                      entryId, generation), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(hasEvent(nativeEvents, QStringLiteral("restart"),
                                      entryId, generation), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(hasEvent(nativeEvents, QStringLiteral("ended-eof"),
                                      entryId, generation), 7000);
}

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication app(argc, argv);
    tst_mpvitem_native_events test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_mpvitem_native_events.moc"
