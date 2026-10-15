#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QUrl>
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
    // Metadata-only probe: this intentionally does not claim navigation-owner
    // settlement. Qt Quick owner coverage belongs to the QML suites.
    void autoRepeatAndReleaseMetadataProbe();
    void realQmlOwnerReceivesPressRepeatRelease();
};

void tst_keyboard_key_events::autoRepeatAndReleaseMetadataProbe()
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

void tst_keyboard_key_events::realQmlOwnerReceivesPressRepeatRelease()
{
    QQmlApplicationEngine engine;
    const QUrl fixtureUrl = QUrl::fromLocalFile(
        QStringLiteral(COLOSSEUM_TEST_SOURCE_DIR "/auto/keyboard/tst_keyboard_key_events_fixture.qml"));
    engine.load(fixtureUrl);
    QVERIFY(!engine.rootObjects().isEmpty());

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window);
    window->show();
    window->requestActivate();
    auto *accountPage = window->findChild<QQuickItem *>(QStringLiteral("accountPageFrame"));
    QVERIFY(accountPage);
    auto *accountScroll = window->findChild<QQuickItem *>(QStringLiteral("accountPageFrameScrollRegion"));
    QVERIFY(accountScroll);
    auto *accountActionA = window->findChild<QQuickItem *>(QStringLiteral("accountActionA"));
    QVERIFY(accountActionA);
    auto *accountActionB = window->findChild<QQuickItem *>(QStringLiteral("accountActionB"));
    QVERIFY(accountActionB);
    QTRY_VERIFY_WITH_TIMEOUT(accountScroll->property("contentHeight").toReal()
                                 > accountScroll->property("height").toReal(),
                             2000);
    accountActionA->forceActiveFocus(Qt::OtherFocusReason);
    QVERIFY(accountActionA->hasActiveFocus());
    QCOMPARE(window->activeFocusItem(), accountActionA);

    QTest::keyPress(window, Qt::Key_Down);
    QTRY_VERIFY_WITH_TIMEOUT(accountScroll->property("contentY").toReal() > 0.0, 2000);
    const qreal afterInitial = accountScroll->property("contentY").toReal();
    QKeyEvent accountRepeat(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier,
                            QString(), true, 1);
    QCoreApplication::sendEvent(window, &accountRepeat);
    QTRY_VERIFY_WITH_TIMEOUT(accountScroll->property("contentY").toReal() > afterInitial, 2000);
    QKeyEvent accountRelease(QEvent::KeyRelease, Qt::Key_Down, Qt::NoModifier,
                             QString(), false, 1);
    QCoreApplication::sendEvent(window, &accountRelease);
    const qreal afterRelease = accountScroll->property("contentY").toReal();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QCOMPARE(accountScroll->property("contentY").toReal(), afterRelease);

    auto *deferredOwner = window->findChild<QQuickItem *>(QStringLiteral("deferredKeyboardOwner"));
    QVERIFY(deferredOwner);
    deferredOwner->setVisible(true);
    accountPage->setVisible(false);
    deferredOwner->forceActiveFocus(Qt::OtherFocusReason);
    QVERIFY(deferredOwner->hasActiveFocus());
    QKeyEvent deferredPress(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier,
                            QString(), false, 1);
    QCoreApplication::sendEvent(deferredOwner, &deferredPress);
    QKeyEvent deferredRepeat(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier,
                             QString(), true, 1);
    QCoreApplication::sendEvent(deferredOwner, &deferredRepeat);
    QTRY_COMPARE_WITH_TIMEOUT(deferredOwner->property("landedId").toString(), QStringLiteral("B"), 2000);
    QVERIFY(deferredOwner->property("repeatGeneration").toInt()
            > deferredOwner->property("pressGeneration").toInt());
    QCOMPARE(deferredOwner->property("aLandingCount").toInt(), 0);
    QCOMPARE(deferredOwner->property("bLandingCount").toInt(), 1);
    QCOMPARE(deferredOwner->property("movementCount").toInt(), 1);
    const int releaseCountBefore = deferredOwner->property("releaseCount").toInt();
    QKeyEvent deferredRelease(QEvent::KeyRelease, Qt::Key_Down, Qt::NoModifier,
                              QString(), false, 1);
    QCoreApplication::sendEvent(deferredOwner, &deferredRelease);
    QTRY_COMPARE_WITH_TIMEOUT(deferredOwner->property("releaseCount").toInt(), releaseCountBefore + 1, 2000);
    auto *deferredNavigator = window->findChild<QQuickItem *>(QStringLiteral("deferredNavigator"));
    QVERIFY(deferredNavigator);
    QVERIFY(!deferredNavigator->property("navigationActive").toBool());
    QVERIFY(deferredNavigator->property("pendingNavigation").isNull());
    const QString landedAfterRelease = deferredOwner->property("landedId").toString();
    const int movementAfterRelease = deferredOwner->property("movementCount").toInt();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QCOMPARE(deferredOwner->property("landedId").toString(), landedAfterRelease);
    QCOMPARE(deferredOwner->property("movementCount").toInt(), movementAfterRelease);
}

QTEST_MAIN(tst_keyboard_key_events)
#include "tst_keyboard_key_events.moc"
