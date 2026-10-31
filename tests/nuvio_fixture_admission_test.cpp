#include "player/NuvioFixtureAdmission.h"

#include <QtTest>

using Colosseum::Nuvio::FixtureAdmission;

class FixtureAdmissionTest : public QObject {
    Q_OBJECT
private slots:
    void pinnedSwitchRequiresFreshProofAndExactBytes() {
        const QByteArray a("fixture A"), b("fixture B");
        FixtureAdmission gate({{"hash-a", sha(a)}, {"hash-b", sha(b)}});
        QVERIFY(!gate.admit(state("session", 0), "hash-a", 0, a));
        QVERIFY(gate.verify(state("session", 0)));
        QVERIFY(!gate.admit(state("session", 0), "hash-a", 0, b));
        QVERIFY(!gate.admit(state("session", 0), "foreign", 0, a));
        QVERIFY(!gate.admit(state("session", 0), "hash-a", 1, a));
        QVERIFY(gate.admit(state("session", 0), "hash-a", 0, a));
        QVERIFY(gate.verify(state("session", 1), true));
        QVERIFY(gate.admit(state("session", 1), "hash-b", 0, b));
        QVERIFY(gate.verify(state("session", 2), true));
        QVERIFY(gate.admit(state("session", 2), "hash-a", 0, a));
        QVERIFY(!gate.admit(state("session", 3), "hash-a", 0, a));
        QVERIFY(!gate.admit(state("different-session", 0), "hash-a", 0, a));
        QVERIFY(!gate.admit(state("session", 0, false), "hash-a", 0, a));
    }
    void explicitVerificationRemainsAnEmptySessionCheck() {
        const QByteArray a("fixture A");
        FixtureAdmission gate({{"hash-a", sha(a)}});
        QVERIFY(!gate.verify(state("session", 1), true));
        QVERIFY(gate.verify(state("session", 0)));
        QVERIFY(gate.admit(state("session", 0), "hash-a", 0, a));
        QVERIFY(!gate.verify(state("session", 1)));
        QVERIFY(!gate.admit(state("session", 0), "hash-a", 0, a));
        QVERIFY(!gate.verify("isolation_pass=true; session_instance=session; torrent_count=garbage"));
        QVERIFY(!gate.verify("isolation_pass=true; torrent_count=0"));
        QVERIFY(!gate.verify(state("session", -1)));
    }
    void legacyAndResetStayClosed() {
        FixtureAdmission gate;
        QVERIFY(gate.verify(state("session", 0)));
        QVERIFY(gate.admit(state("session", 0), "hash-a", 0, "metadata"));
        QVERIFY(!gate.admit(state("session", 1), "hash-b", 0, "metadata"));
        QVERIFY(!gate.verify(state("session", 1), true));
        QVERIFY(gate.verify(state("session", 0)));
        gate.reset();
        QVERIFY(!gate.admit(state("session", 0), "hash-a", 0, "metadata"));
    }
private:
    static QByteArray sha(const QByteArray &data) {
        return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
    }
    static QString state(const QString &session, int count, bool safe = true) {
        return QStringLiteral("isolation_pass=%1; session_instance=%2; torrent_count=%3; enable_dht=false")
            .arg(safe ? QStringLiteral("true") : QStringLiteral("false"), session).arg(count);
    }
};

QTEST_GUILESS_MAIN(FixtureAdmissionTest)
#include "nuvio_fixture_admission_test.moc"
