#include "account/ProfilePreferencesStore.h"

#include <QDir>
#include <QFile>
#include <QMetaProperty>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
bool invokeSnapshotSetter(
    QObject *store,
    const QString &snapshot,
    bool *committed) {
    bool result = false;
    const bool invoked = QMetaObject::invokeMethod(
        store,
        "setVinylFeedbackSnapshot",
        Qt::DirectConnection,
        Q_RETURN_ARG(bool, result),
        Q_ARG(QString, snapshot));
    if (committed)
        *committed = result;
    return invoked;
}

QString snapshotValue(const QObject *store) {
    return store->property("vinylFeedbackSnapshot").toString();
}

QString persistenceError(const QObject *store) {
    return store->property("vinylPersistenceError").toString();
}
}

class tst_vinyl_feedback_snapshot : public QObject {
    Q_OBJECT

private slots:
    void snapshotIsExposedAndPersists();
    void snapshotUsesUtf8ByteLimitAndRejectsNul();
    void malformedStoredSnapshotIsNotPublished();
    void failedSyncRetainsPreviousSnapshot();
};

void tst_vinyl_feedback_snapshot::snapshotIsExposedAndPersists() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("preferences.ini"));
    const QString value = QStringLiteral("{\"version\":1,\"profile\":\"account-a\"}");

    ProfilePreferencesStore store(path);
    const int propertyIndex =
        store.metaObject()->indexOfProperty("vinylFeedbackSnapshot");
    QVERIFY2(propertyIndex >= 0, "feedback snapshot must be a Qt property");
    const QMetaProperty snapshotProperty = store.metaObject()->property(propertyIndex);
    QVERIFY(snapshotProperty.isReadable());
    QVERIFY(snapshotProperty.hasNotifySignal());
    QVERIFY(store.metaObject()->indexOfMethod(
                "setVinylFeedbackSnapshot(QString)") >= 0);

    QSignalSpy changed(&store, SIGNAL(vinylFeedbackChanged()));
    QVERIFY(changed.isValid());
    bool committed = false;
    QVERIFY(invokeSnapshotSetter(&store, value, &committed));
    QVERIFY(committed);
    QCOMPARE(snapshotValue(&store), value);
    QCOMPARE(changed.count(), 1);

    {
        QSettings settings(path, QSettings::IniFormat);
        QCOMPARE(settings.value(QStringLiteral("vinyl/feedbackSnapshot")).toString(), value);
    }
    {
        ProfilePreferencesStore reloaded(path);
        QCOMPARE(snapshotValue(&reloaded), value);
        QVERIFY(persistenceError(&reloaded).isEmpty());
        QVERIFY(invokeSnapshotSetter(&reloaded, QString(), &committed));
        QVERIFY(committed);
    }
    {
        ProfilePreferencesStore cleared(path);
        QVERIFY(snapshotValue(&cleared).isEmpty());
    }
}

void tst_vinyl_feedback_snapshot::snapshotUsesUtf8ByteLimitAndRejectsNul() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    ProfilePreferencesStore store(
        temporary.filePath(QStringLiteral("preferences.ini")));

    const QString atLimit = QString(1398101, QChar(0x20ac)) + QLatin1Char('x');
    const QString overLimit(1398102, QChar(0x20ac));
    QCOMPARE(atLimit.toUtf8().size(), 4194304);
    QCOMPARE(overLimit.toUtf8().size(), 4194306);

    bool committed = false;
    QVERIFY(invokeSnapshotSetter(&store, atLimit, &committed));
    QVERIFY(committed);
    QCOMPARE(snapshotValue(&store), atLimit);

    QVERIFY(invokeSnapshotSetter(&store, overLimit, &committed));
    QVERIFY(!committed);
    QCOMPARE(snapshotValue(&store), atLimit);
    QVERIFY(!persistenceError(&store).isEmpty());

    QString withNul = QStringLiteral("feedback snapshot");
    withNul.insert(8, QChar::Null);
    QVERIFY(invokeSnapshotSetter(&store, withNul, &committed));
    QVERIFY(!committed);
    QCOMPARE(snapshotValue(&store), atLimit);
}

void tst_vinyl_feedback_snapshot::malformedStoredSnapshotIsNotPublished() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString wrongTypePath = temporary.filePath(QStringLiteral("wrong-type.ini"));
    {
        QSettings settings(wrongTypePath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("vinyl/feedbackSnapshot"),
                          QVariantMap{{QStringLiteral("unexpected"), true}});
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
    }
    {
        ProfilePreferencesStore store(wrongTypePath);
        QVERIFY(snapshotValue(&store).isEmpty());
        QVERIFY(!persistenceError(&store).isEmpty());
    }

    const QString oversizedPath = temporary.filePath(QStringLiteral("oversized.ini"));
    {
        QSettings settings(oversizedPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("vinyl/feedbackSnapshot"),
                          QString(4194305, QLatin1Char('x')));
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
    }
    {
        ProfilePreferencesStore store(oversizedPath);
        QVERIFY(snapshotValue(&store).isEmpty());
        QVERIFY(!persistenceError(&store).isEmpty());
    }

    const QString nulPath = temporary.filePath(QStringLiteral("embedded-nul.ini"));
    QString withNul = QStringLiteral("stored feedback");
    withNul.insert(7, QChar::Null);
    {
        QSettings settings(nulPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral("vinyl/feedbackSnapshot"), withNul);
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
    }
    {
        ProfilePreferencesStore store(nulPath);
        QVERIFY(snapshotValue(&store).isEmpty());
        QVERIFY(!persistenceError(&store).isEmpty());
    }
}

void tst_vinyl_feedback_snapshot::failedSyncRetainsPreviousSnapshot() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString directory = temporary.filePath(QStringLiteral("live"));
    const QString backup = directory + QStringLiteral("-backup");
    QVERIFY(QDir().mkdir(directory));
    const QString path = directory + QStringLiteral("/preferences.ini");

    {
        ProfilePreferencesStore store(path);
        bool committed = false;
        QVERIFY(invokeSnapshotSetter(
            &store, QStringLiteral("retained snapshot"), &committed));
        QVERIFY(committed);
        QVERIFY(QDir().rename(directory, backup));

        {
            QFile blocker(directory);
            QVERIFY(blocker.open(QIODevice::WriteOnly));
            QVERIFY(invokeSnapshotSetter(
                &store, QStringLiteral("replacement snapshot"), &committed));
            QVERIFY(!committed);
            QCOMPARE(snapshotValue(&store), QStringLiteral("retained snapshot"));
            QVERIFY(!persistenceError(&store).isEmpty());
        }

        QVERIFY(QFile::remove(directory));
        QVERIFY(QDir().rename(backup, directory));
    }

    ProfilePreferencesStore reloaded(path);
    QCOMPARE(snapshotValue(&reloaded), QStringLiteral("retained snapshot"));
}

QTEST_GUILESS_MAIN(tst_vinyl_feedback_snapshot)
#include "tst_vinyl_feedback_snapshot.moc"
