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

bool invokeRecommendationExposureSnapshotSetter(
    QObject *store,
    const QString &snapshot,
    bool *committed) {
    bool result = false;
    const bool invoked = QMetaObject::invokeMethod(
        store,
        "setVinylRecommendationExposureSnapshot",
        Qt::DirectConnection,
        Q_RETURN_ARG(bool, result),
        Q_ARG(QString, snapshot));
    if (committed)
        *committed = result;
    return invoked;
}

QString recommendationExposureSnapshotValue(const QObject *store) {
    return store->property("vinylRecommendationExposureSnapshot").toString();
}

bool invokeHomeSelectionSnapshotSetter(
    QObject *store,
    const QString &snapshot,
    bool *committed) {
    bool result = false;
    const bool invoked = QMetaObject::invokeMethod(
        store,
        "setVinylHomeSelectionSnapshot",
        Qt::DirectConnection,
        Q_RETURN_ARG(bool, result),
        Q_ARG(QString, snapshot));
    if (committed)
        *committed = result;
    return invoked;
}

QString homeSelectionSnapshotValue(const QObject *store) {
    return store->property("vinylHomeSelectionSnapshot").toString();
}
}

class tst_vinyl_feedback_snapshot : public QObject {
    Q_OBJECT

private slots:
    void recommendationExposureSnapshotApiIsExposed();
    void recommendationExposureSnapshotPersistsAndClears();
    void recommendationExposureSnapshotUsesUtf8ByteLimitAndRejectsNul();
    void malformedStoredRecommendationExposureSnapshotIsNotPublished();
    void failedSyncRetainsPreviousRecommendationExposureSnapshot();
    void homeSelectionSnapshotApiIsExposed();
    void homeSelectionSnapshotPersistsAndClears();
    void homeSelectionSnapshotRejectsMalformedValues();
    void malformedStoredHomeSelectionSnapshotIsNotPublished();
    void failedSyncRetainsPreviousHomeSelectionSnapshot();
    void snapshotIsExposedAndPersists();
    void snapshotUsesUtf8ByteLimitAndRejectsNul();
    void malformedStoredSnapshotIsNotPublished();
    void failedSyncRetainsPreviousSnapshot();
};

void tst_vinyl_feedback_snapshot::recommendationExposureSnapshotApiIsExposed() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    ProfilePreferencesStore store(
        temporary.filePath(QStringLiteral("preferences.ini")));

    const int propertyIndex = store.metaObject()->indexOfProperty(
        "vinylRecommendationExposureSnapshot");
    QVERIFY2(propertyIndex >= 0,
             "recommendation exposure snapshot must be a Qt property");
    const QMetaProperty snapshotProperty =
        store.metaObject()->property(propertyIndex);
    QVERIFY(snapshotProperty.isReadable());
    QVERIFY(snapshotProperty.hasNotifySignal());
    QVERIFY(store.metaObject()->indexOfMethod(
                "setVinylRecommendationExposureSnapshot(QString)") >= 0);
}

void tst_vinyl_feedback_snapshot::
    recommendationExposureSnapshotPersistsAndClears() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("preferences.ini"));
    const QString value = QStringLiteral(
        "{\"version\":2,\"profileId\":\"account-a\",\"exposures\":[]}");

    ProfilePreferencesStore store(path);
    QSignalSpy changed(
        &store, SIGNAL(vinylRecommendationExposureChanged()));
    QVERIFY(changed.isValid());
    bool committed = false;
    QVERIFY(invokeRecommendationExposureSnapshotSetter(
        &store, value, &committed));
    QVERIFY(committed);
    QCOMPARE(recommendationExposureSnapshotValue(&store), value);
    QCOMPARE(changed.count(), 1);

    {
        QSettings settings(path, QSettings::IniFormat);
        QCOMPARE(settings.value(QStringLiteral(
                                  "vinyl/recommendationExposureSnapshot"))
                     .toString(),
                 value);
    }
    {
        ProfilePreferencesStore reloaded(path);
        QCOMPARE(recommendationExposureSnapshotValue(&reloaded), value);
        QVERIFY(persistenceError(&reloaded).isEmpty());
        QVERIFY(invokeRecommendationExposureSnapshotSetter(
            &reloaded, QString(), &committed));
        QVERIFY(committed);
    }
    {
        ProfilePreferencesStore cleared(path);
        QVERIFY(recommendationExposureSnapshotValue(&cleared).isEmpty());
    }
}

void tst_vinyl_feedback_snapshot::
    recommendationExposureSnapshotUsesUtf8ByteLimitAndRejectsNul() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    ProfilePreferencesStore store(
        temporary.filePath(QStringLiteral("preferences.ini")));

    const QString atLimit = QString(1398101, QChar(0x20ac)) + QLatin1Char('x');
    const QString overLimit(1398102, QChar(0x20ac));
    QCOMPARE(atLimit.toUtf8().size(), 4194304);
    QCOMPARE(overLimit.toUtf8().size(), 4194306);

    bool committed = false;
    QVERIFY(invokeRecommendationExposureSnapshotSetter(
        &store, atLimit, &committed));
    QVERIFY(committed);
    QCOMPARE(recommendationExposureSnapshotValue(&store), atLimit);

    QVERIFY(invokeRecommendationExposureSnapshotSetter(
        &store, overLimit, &committed));
    QVERIFY(!committed);
    QCOMPARE(recommendationExposureSnapshotValue(&store), atLimit);
    QVERIFY(!persistenceError(&store).isEmpty());

    QString withNul = QStringLiteral("exposure snapshot");
    withNul.insert(8, QChar::Null);
    QVERIFY(invokeRecommendationExposureSnapshotSetter(
        &store, withNul, &committed));
    QVERIFY(!committed);
    QCOMPARE(recommendationExposureSnapshotValue(&store), atLimit);
}

void tst_vinyl_feedback_snapshot::
    malformedStoredRecommendationExposureSnapshotIsNotPublished() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString wrongTypePath = temporary.filePath(
        QStringLiteral("wrong-type.ini"));
    {
        QSettings settings(wrongTypePath, QSettings::IniFormat);
        settings.setValue(QStringLiteral(
                              "vinyl/recommendationExposureSnapshot"),
                          QVariantMap{{QStringLiteral("unexpected"), true}});
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
    }
    {
        ProfilePreferencesStore store(wrongTypePath);
        QVERIFY(recommendationExposureSnapshotValue(&store).isEmpty());
        QVERIFY(!persistenceError(&store).isEmpty());
    }

    const QString oversizedPath = temporary.filePath(
        QStringLiteral("oversized.ini"));
    {
        QSettings settings(oversizedPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral(
                              "vinyl/recommendationExposureSnapshot"),
                          QString(4194305, QLatin1Char('x')));
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
    }
    {
        ProfilePreferencesStore store(oversizedPath);
        QVERIFY(recommendationExposureSnapshotValue(&store).isEmpty());
        QVERIFY(!persistenceError(&store).isEmpty());
    }

    const QString nulPath = temporary.filePath(
        QStringLiteral("embedded-nul.ini"));
    QString withNul = QStringLiteral("stored exposure");
    withNul.insert(7, QChar::Null);
    {
        QSettings settings(nulPath, QSettings::IniFormat);
        settings.setValue(QStringLiteral(
                              "vinyl/recommendationExposureSnapshot"),
                          withNul);
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
    }
    {
        ProfilePreferencesStore store(nulPath);
        QVERIFY(recommendationExposureSnapshotValue(&store).isEmpty());
        QVERIFY(!persistenceError(&store).isEmpty());
    }
}

void tst_vinyl_feedback_snapshot::
    failedSyncRetainsPreviousRecommendationExposureSnapshot() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString directory = temporary.filePath(QStringLiteral("live"));
    const QString backup = directory + QStringLiteral("-backup");
    QVERIFY(QDir().mkdir(directory));
    const QString path = directory + QStringLiteral("/preferences.ini");

    {
        ProfilePreferencesStore store(path);
        bool committed = false;
        QVERIFY(invokeRecommendationExposureSnapshotSetter(
            &store, QStringLiteral("retained snapshot"), &committed));
        QVERIFY(committed);
        QVERIFY(QDir().rename(directory, backup));

        {
            QFile blocker(directory);
            QVERIFY(blocker.open(QIODevice::WriteOnly));
            QVERIFY(invokeRecommendationExposureSnapshotSetter(
                &store, QStringLiteral("replacement snapshot"), &committed));
            QVERIFY(!committed);
            QCOMPARE(recommendationExposureSnapshotValue(&store),
                     QStringLiteral("retained snapshot"));
            QVERIFY(!persistenceError(&store).isEmpty());
        }

        QVERIFY(QFile::remove(directory));
        QVERIFY(QDir().rename(backup, directory));
    }

    ProfilePreferencesStore reloaded(path);
    QCOMPARE(recommendationExposureSnapshotValue(&reloaded),
             QStringLiteral("retained snapshot"));
}

void tst_vinyl_feedback_snapshot::homeSelectionSnapshotApiIsExposed() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    ProfilePreferencesStore store(
        temporary.filePath(QStringLiteral("preferences.ini")));

    const int propertyIndex = store.metaObject()->indexOfProperty(
        "vinylHomeSelectionSnapshot");
    QVERIFY2(propertyIndex >= 0,
             "home selection snapshot must be a Qt property");
    const QMetaProperty snapshotProperty =
        store.metaObject()->property(propertyIndex);
    QVERIFY(snapshotProperty.isReadable());
    QVERIFY(!snapshotProperty.isWritable());
    QVERIFY(snapshotProperty.hasNotifySignal());
    QVERIFY(store.metaObject()->indexOfMethod(
                "setVinylHomeSelectionSnapshot(QString)") >= 0);
}

void tst_vinyl_feedback_snapshot::homeSelectionSnapshotPersistsAndClears() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("preferences.ini"));
    const QString value = QStringLiteral(
        "{\"version\":1,\"profileId\":\"account-a\",\"day\":\"2026-10-30\","
        "\"seedHash\":\"seed-a\",\"refreshSerial\":3,\"keys\":[\"a\",\"b\"],"
        "\"previousKeys\":[\"c\"]}");

    ProfilePreferencesStore store(path);
    QSignalSpy changed(&store, SIGNAL(vinylHomeSelectionChanged()));
    QVERIFY(changed.isValid());
    bool committed = false;
    QVERIFY(invokeHomeSelectionSnapshotSetter(&store, value, &committed));
    QVERIFY(committed);
    QCOMPARE(homeSelectionSnapshotValue(&store), value);
    QCOMPARE(changed.count(), 1);

    {
        QSettings settings(path, QSettings::IniFormat);
        QCOMPARE(settings.value(QStringLiteral("vinyl/homeSelectionSnapshot"))
                     .toString(),
                 value);
    }
    {
        ProfilePreferencesStore reloaded(path);
        QCOMPARE(homeSelectionSnapshotValue(&reloaded), value);
        QVERIFY(persistenceError(&reloaded).isEmpty());
        QVERIFY(invokeHomeSelectionSnapshotSetter(
            &reloaded, QString(), &committed));
        QVERIFY(committed);
    }
    {
        ProfilePreferencesStore cleared(path);
        QVERIFY(homeSelectionSnapshotValue(&cleared).isEmpty());
    }
}

void tst_vinyl_feedback_snapshot::
    homeSelectionSnapshotRejectsMalformedValues() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    ProfilePreferencesStore store(
        temporary.filePath(QStringLiteral("preferences.ini")));
    const QString valid = QStringLiteral(
        "{\"version\":1,\"profileId\":\"account-a\",\"day\":\"2026-10-30\","
        "\"seedHash\":\"seed-a\",\"refreshSerial\":3,\"keys\":[\"a\"]}");
    const QStringList malformed{
        QStringLiteral("{}"),
        QStringLiteral(
            "{\"version\":2,\"profileId\":\"account-a\",\"day\":\"2026-10-30\","
            "\"seedHash\":\"seed-a\",\"refreshSerial\":3,\"keys\":[]}"),
        QStringLiteral(
            "{\"version\":1,\"profileId\":4,\"day\":\"2026-10-30\","
            "\"seedHash\":\"seed-a\",\"refreshSerial\":3,\"keys\":[]}"),
        QStringLiteral(
            "{\"version\":1,\"profileId\":\"account-a\",\"day\":\"2026-02-30\","
            "\"seedHash\":\"seed-a\",\"refreshSerial\":3,\"keys\":[]}"),
        QStringLiteral(
            "{\"version\":1,\"profileId\":\"account-a\",\"day\":\"2026-10-30\","
            "\"seedHash\":\"seed-a\",\"refreshSerial\":-1,\"keys\":[]}"),
        QStringLiteral(
            "{\"version\":1,\"profileId\":\"account-a\",\"day\":\"2026-10-30\","
            "\"seedHash\":\"seed-a\",\"refreshSerial\":1.5,\"keys\":[]}"),
        QStringLiteral(
            "{\"version\":1,\"profileId\":\"account-a\",\"day\":\"2026-10-30\","
            "\"seedHash\":\"seed-a\",\"refreshSerial\":3,\"keys\":[\"a\",\"a\"]}"),
        QStringLiteral(
            "{\"version\":1,\"profileId\":\"account-a\",\"day\":\"2026-10-30\","
            "\"seedHash\":\"seed-a\",\"refreshSerial\":3,\"keys\":[\"a\",\"b\",\"c\",\"d\",\"e\",\"f\"]}")};

    bool committed = false;
    QVERIFY(invokeHomeSelectionSnapshotSetter(&store, valid, &committed));
    QVERIFY(committed);
    for (const QString &snapshot : malformed) {
        QVERIFY(invokeHomeSelectionSnapshotSetter(
            &store, snapshot, &committed));
        QVERIFY(!committed);
        QCOMPARE(homeSelectionSnapshotValue(&store), valid);
        QVERIFY(!persistenceError(&store).isEmpty());
    }

    QString oversized = valid;
    oversized.append(QString(16 * 1024, QLatin1Char('x')));
    QVERIFY(invokeHomeSelectionSnapshotSetter(&store, oversized, &committed));
    QVERIFY(!committed);
    QCOMPARE(homeSelectionSnapshotValue(&store), valid);

    const QString longKeyPayload = QStringLiteral(
        "{\"version\":1,\"profileId\":\"account-a\",\"day\":\"2026-10-30\","
        "\"seedHash\":\"seed-a\",\"refreshSerial\":3,\"keys\":[\"")
        + QString(1025, QLatin1Char('x')) + QStringLiteral("\"]}");
    QVERIFY(invokeHomeSelectionSnapshotSetter(
        &store, longKeyPayload, &committed));
    QVERIFY(!committed);
    QCOMPARE(homeSelectionSnapshotValue(&store), valid);

    const QString tooManyPreviousKeys = QStringLiteral(
        "{\"version\":1,\"profileId\":\"account-a\",\"day\":\"2026-10-30\","
        "\"seedHash\":\"seed-a\",\"refreshSerial\":3,\"keys\":[],"
        "\"previousKeys\":[\"a\",\"b\",\"c\",\"d\",\"e\",\"f\",\"g\",\"h\",\"i\",\"j\",\"k\"]}");
    QVERIFY(invokeHomeSelectionSnapshotSetter(
        &store, tooManyPreviousKeys, &committed));
    QVERIFY(!committed);
    QCOMPARE(homeSelectionSnapshotValue(&store), valid);

    QString withNul = valid;
    withNul.insert(4, QChar::Null);
    QVERIFY(invokeHomeSelectionSnapshotSetter(&store, withNul, &committed));
    QVERIFY(!committed);
    QCOMPARE(homeSelectionSnapshotValue(&store), valid);
}

void tst_vinyl_feedback_snapshot::
    malformedStoredHomeSelectionSnapshotIsNotPublished() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("preferences.ini"));
    {
        QSettings settings(path, QSettings::IniFormat);
        settings.setValue(QStringLiteral("vinyl/homeSelectionSnapshot"),
                          QStringLiteral("{\"version\":1}"));
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
    }
    {
        ProfilePreferencesStore store(path);
        QVERIFY(homeSelectionSnapshotValue(&store).isEmpty());
        QVERIFY(!persistenceError(&store).isEmpty());
    }
}

void tst_vinyl_feedback_snapshot::
    failedSyncRetainsPreviousHomeSelectionSnapshot() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString directory = temporary.filePath(QStringLiteral("live"));
    const QString backup = directory + QStringLiteral("-backup");
    QVERIFY(QDir().mkdir(directory));
    const QString path = directory + QStringLiteral("/preferences.ini");
    const QString retained = QStringLiteral(
        "{\"version\":1,\"profileId\":\"account-a\",\"day\":\"2026-10-30\","
        "\"seedHash\":\"seed-a\",\"refreshSerial\":3,\"keys\":[\"a\"]}");
    const QString replacement = QStringLiteral(
        "{\"version\":1,\"profileId\":\"account-a\",\"day\":\"2026-10-30\","
        "\"seedHash\":\"seed-b\",\"refreshSerial\":4,\"keys\":[\"b\"]}");

    {
        ProfilePreferencesStore store(path);
        bool committed = false;
        QVERIFY(invokeHomeSelectionSnapshotSetter(
            &store, retained, &committed));
        QVERIFY(committed);
        QVERIFY(QDir().rename(directory, backup));

        {
            QFile blocker(directory);
            QVERIFY(blocker.open(QIODevice::WriteOnly));
            QVERIFY(invokeHomeSelectionSnapshotSetter(
                &store, replacement, &committed));
            QVERIFY(!committed);
            QCOMPARE(homeSelectionSnapshotValue(&store), retained);
            QVERIFY(!persistenceError(&store).isEmpty());
        }

        QVERIFY(QFile::remove(directory));
        QVERIFY(QDir().rename(backup, directory));
    }

    ProfilePreferencesStore reloaded(path);
    QCOMPARE(homeSelectionSnapshotValue(&reloaded), retained);
}

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
