#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QObject>
#include <QTest>

class KeyEventProbe final : public QObject
{
    Q_OBJECT

public:
    struct EventRecord {
        QEvent::Type type;
        bool autoRepeat;
    };
    QList<EventRecord> events;

protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
            events.append({event->type(), static_cast<QKeyEvent *>(event)->isAutoRepeat()});
        return QObject::event(event);
    }
};

class tst_keyboard_key_events final : public QObject
{
    Q_OBJECT

private slots:
    void autoRepeatAndReleaseAreDistinctInputEvents();
};

void tst_keyboard_key_events::autoRepeatAndReleaseAreDistinctInputEvents()
{
    KeyEventProbe probe;
    QKeyEvent initialPress(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier,
                           QString(), false, 1);
    QKeyEvent repeatedPress(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier,
                            QString(), true, 1);
    QKeyEvent finalRelease(QEvent::KeyRelease, Qt::Key_Down, Qt::NoModifier,
                           QString(), false, 1);

    QCoreApplication::sendEvent(&probe, &initialPress);
    QCoreApplication::sendEvent(&probe, &repeatedPress);
    QCoreApplication::sendEvent(&probe, &finalRelease);
    QCOMPARE(probe.events.size(), 3);
    QCOMPARE(probe.events.at(0).type, QEvent::KeyPress);
    QVERIFY(!probe.events.at(0).autoRepeat);
    QCOMPARE(probe.events.at(1).type, QEvent::KeyPress);
    QVERIFY(probe.events.at(1).autoRepeat);
    QCOMPARE(probe.events.at(2).type, QEvent::KeyRelease);
    QVERIFY(!probe.events.at(2).autoRepeat);
}

QTEST_MAIN(tst_keyboard_key_events)
#include "tst_keyboard_key_events.moc"
