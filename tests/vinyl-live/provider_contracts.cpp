#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QDir>
#include <QSet>
#include "vinyl/VinylProvider.h"

namespace {
QString fixture() {
    return QDir(QStringLiteral(VINYL_SOURCE_ROOT)).filePath(
        QStringLiteral("tests/vinyl-live/provider_resilience_fixture.py"));
}
struct PythonOverride {
    QByteArray original = qgetenv("COLOSSEUM_VINYL_PYTHON");
    bool wasSet = qEnvironmentVariableIsSet("COLOSSEUM_VINYL_PYTHON");
    explicit PythonOverride(const QByteArray &value) { qputenv("COLOSSEUM_VINYL_PYTHON", value); }
    ~PythonOverride() {
        if (wasSet) qputenv("COLOSSEUM_VINYL_PYTHON", original);
        else qunsetenv("COLOSSEUM_VINYL_PYTHON");
    }
};
}

class ProviderContracts : public QObject {
    Q_OBJECT
private slots:
    void successfulPayloadsAreAsyncAndCorrelated() {
        PythonOverride python("python");
        VinylProvider provider(fixture());
        QSignalSpy done(&provider, &VinylProvider::completed);
        QSignalSpy categories(&provider, &VinylProvider::errorCategorized);
        const int search = provider.request("browse", "search", "query");
        QVERIFY(search > 0);
        QVERIFY(done.isEmpty()); // No completion before caller stores request ID.
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000);
        QCOMPARE(done.at(0).at(0).toString(), QStringLiteral("browse"));
        QCOMPARE(done.at(0).at(1).toInt(), search);
        QCOMPARE(done.at(0).at(2).toList().size(), 1);
        QCOMPARE(done.at(0).at(2).toList().first().toMap().value("browseId").toString(),
                 QStringLiteral("query"));
        QVERIFY(done.at(0).at(3).toString().isEmpty());
        QCOMPARE(categories.size(), 0);

        const int album = provider.request("browse", "album", "release");
        QVERIFY(done.size() == 1);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 5000);
        QCOMPARE(done.at(1).at(1).toInt(), album);
        QCOMPARE(done.at(1).at(2).toMap().value("browseId").toString(), QStringLiteral("release"));
        const int play = provider.request("play", "resolve", "InEXiAJCOuA");
        QVERIFY(done.size() == 2);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 3, 5000);
        QCOMPARE(done.at(2).at(1).toInt(), play);
        QCOMPARE(done.at(2).at(2).toMap().value("videoId").toString(), QStringLiteral("InEXiAJCOuA"));
        QCOMPARE(categories.size(), 0);
    }

    void failedStartIsAsyncAndCategorizedOnce() {
        PythonOverride python("vinyl-python-missing-executable-001");
        VinylProvider provider(fixture());
        QSignalSpy done(&provider, &VinylProvider::completed);
        QSignalSpy categories(&provider, &VinylProvider::errorCategorized);
        const int id = provider.request("browse", "search", "query");
        QVERIFY(id > 0);
        QVERIFY(done.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000);
        QCOMPARE(done.first().at(1).toInt(), id);
        QCOMPARE(done.first().at(3).toString(), QStringLiteral("Vinyl's Python helper could not start."));
        QCOMPARE(categories.size(), 1);
        QCOMPARE(categories.first().at(2).toString(), QStringLiteral("start_failed"));
        QTest::qWait(100);
        QCOMPARE(done.size(), 1);
        QCOMPARE(categories.size(), 1);
    }

    void malformedOversizedAndExitedProcessAreBounded() {
        PythonOverride python("python");
        const QList<QPair<QString, QString>> cases{
            {"malformed", "malformed_response"},
            {"missing-ok", "malformed_response"},
            {"wrong-shape", "malformed_response"},
            {"broken-search-row", "malformed_response"},
            {"too-big", "oversized_response"},
            {"crash", "process_failed"},
        };
        for (const auto &item : cases) {
            VinylProvider provider(fixture(), nullptr, 5000, 5000);
            QSignalSpy done(&provider, &VinylProvider::completed);
            QSignalSpy categories(&provider, &VinylProvider::errorCategorized);
            const int id = provider.request("browse", "search", item.first);
            QVERIFY2(done.isEmpty(), qPrintable(item.first));
            QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 8000);
            QCOMPARE(done.first().at(1).toInt(), id);
            QCOMPARE(categories.size(), 1);
            QCOMPARE(categories.first().at(2).toString(), item.second);
            QVERIFY(!done.first().at(3).toString().contains("secret.example"));
            QVERIFY(!done.first().at(3).toString().contains("PRIVATE"));
            QTest::qWait(50);
            QCOMPARE(done.size(), 1);
        }
    }

    void helperErrorsAreSanitizedAndCategorized() {
        PythonOverride python("python");
        const QList<QPair<QString, QString>> cases{
            {"leak", "invalid_request"}, {"dependency", "dependency_missing"}
        };
        for (const auto &item : cases) {
            VinylProvider provider(fixture());
            QSignalSpy done(&provider, &VinylProvider::completed);
            QSignalSpy categories(&provider, &VinylProvider::errorCategorized);
            const int id = provider.request("browse", "search", item.first);
            QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000);
            QCOMPARE(done.first().at(1).toInt(), id);
            QCOMPARE(categories.size(), 1);
            QCOMPARE(categories.first().at(2).toString(), item.second);
            const auto message = done.first().at(3).toString();
            QVERIFY(!message.isEmpty());
            QVERIFY(!message.contains("secret.example"));
            QVERIFY(!message.contains("PRIVATE"));
        }
    }

    void incompleteSuccessfulPayloadsAreRejected() {
        PythonOverride python("python");
        VinylProvider provider(fixture());
        QSignalSpy done(&provider, &VinylProvider::completed);
        QSignalSpy categories(&provider, &VinylProvider::errorCategorized);
        const int album = provider.request("browse", "album", "broken-album");
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000);
        QCOMPARE(done.first().at(1).toInt(), album);
        QCOMPARE(done.first().at(3).toString(), QStringLiteral("Vinyl's provider reply was invalid."));
        QCOMPARE(categories.size(), 1);
        QCOMPARE(categories.first().at(2).toString(), QStringLiteral("malformed_response"));
        const int play = provider.request("play", "resolve", "CCCCCCCCCCC");
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 5000);
        QCOMPARE(done.at(1).at(1).toInt(), play);
        QCOMPARE(categories.size(), 2);
        QCOMPARE(categories.at(1).at(2).toString(), QStringLiteral("malformed_response"));
    }

    void resolvedTrackCannotChangeIdentity() {
        PythonOverride python("python");
        VinylProvider provider(fixture());
        QSignalSpy done(&provider, &VinylProvider::completed);
        QSignalSpy categories(&provider, &VinylProvider::errorCategorized);
        const int id = provider.request("play", "resolve", "AAAAAAAAAAA");
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000);
        QCOMPARE(done.first().at(1).toInt(), id);
        QVERIFY(done.first().at(2).isNull());
        QCOMPARE(categories.size(), 1);
        QCOMPARE(categories.first().at(2).toString(), QStringLiteral("identity_mismatch"));
    }

    void supersessionAndProfileLeaveSuppressLateResults() {
        PythonOverride python("python");
        VinylProvider provider(fixture());
        QSignalSpy done(&provider, &VinylProvider::completed);
        QSignalSpy categories(&provider, &VinylProvider::errorCategorized);
        const int oldId = provider.request("browse", "search", "slow");
        const int newId = provider.request("browse", "search", "new");
        QVERIFY(newId > oldId);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000);
        QCOMPARE(done.first().at(1).toInt(), newId);
        QTest::qWait(650);
        QCOMPARE(done.size(), 1);
        QCOMPARE(categories.size(), 0);
        provider.request("browse", "search", "slow");
        emit provider.profileLeaving();
        const int afterProfile = provider.request("browse", "search", "incoming");
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 5000);
        QCOMPARE(done.at(1).at(1).toInt(), afterProfile);
        QTest::qWait(650);
        QCOMPARE(done.size(), 2);
        QCOMPARE(categories.size(), 0);
    }

    void cancellationDropsResultsWithoutBlocking() {
        PythonOverride python("python");
        VinylProvider provider(fixture());
        QSignalSpy done(&provider, &VinylProvider::completed);
        const int canceled = provider.request("browse", "search", "slow");
        QVERIFY(canceled > 0);
        QElapsedTimer timer;
        timer.start();
        provider.cancel("browse");
        QVERIFY2(timer.elapsed() < 500, "Cancellation must not block on waitForFinished.");
        QTest::qWait(650);
        QCOMPARE(done.size(), 0);
    }

    void independentLanesAndTimeoutDoNotStrandRequests() {
        PythonOverride python("python");
        VinylProvider provider(fixture(), nullptr, 600, 5000);
        QSignalSpy done(&provider, &VinylProvider::completed);
        QSignalSpy categories(&provider, &VinylProvider::errorCategorized);
        const int browsing = provider.request("browse", "search", "hang");
        const int playing = provider.request("play", "resolve", "InEXiAJCOuA");
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 7000);
        QCOMPARE(categories.size(), 1);
        QCOMPARE(categories.first().at(1).toInt(), browsing);
        QCOMPARE(categories.first().at(2).toString(), QStringLiteral("timeout"));
        QSet<int> ids;
        for (const auto &record : done) {
            ids.insert(record.at(1).toInt());
            if (record.at(1).toInt() == browsing)
                QCOMPARE(record.at(3).toString(), QStringLiteral("YouTube took too long. Try again."));
            else if (record.at(1).toInt() == playing)
                QCOMPARE(record.at(2).toMap().value("videoId").toString(), QStringLiteral("InEXiAJCOuA"));
        }
        QCOMPARE(ids, QSet<int>({browsing, playing}));
        QTest::qWait(150);
        QCOMPARE(done.size(), 2);
    }
};

QTEST_GUILESS_MAIN(ProviderContracts)
#include "provider_contracts.moc"
