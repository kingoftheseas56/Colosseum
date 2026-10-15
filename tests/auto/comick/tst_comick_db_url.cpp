// tst_comick_db_url — ComickCatalogClient Slice B: fetch a series record by MAL id. Data-row
// unit test for the pure dbUrlFor() key-selection + percent-encoding helper (declared in
// native/engine/ComickCatalogClient.h, defined in the .cpp under tankoban::manga::comick),
// WITHOUT any QNetworkAccessManager, network access, or app boot.
//
// Proves: an empty MAL id reproduces today's WeebCentral-ULID db URL byte-for-byte; a
// non-empty MAL id builds the new `db/mal-<id>.json` key instead — and wins even when a
// WeebCentral id is ALSO present, because a series fetched by MAL id must look itself up
// under its MAL record, not a WeebCentral one that happens to also be known; and reserved
// characters in either id are percent-encoded exactly as the pre-Slice-B inline code did
// (QUrl::toPercentEncoding with no exclude/include list).
//
// Non-vacuous by construction: each row is a distinct expected string, so a wrong
// key-selection or a dropped/altered encoding step fails exactly that named row while the
// others keep passing (per-row Qt Test reporting). Negative control performed at build
// time: temporarily flipped the selection rule in native/engine/ComickCatalogClient.cpp's
// dbUrlFor() to prefer weebCentralId over a present malId — the
// "mal id wins over a present weebCentralId" row (and the plain non-empty-MAL row) went
// RED with the ULID URL where the mal-21 URL was expected; every other row kept passing.
// Reverted, rebuilt, all rows green again — recorded in
// docs/colosseum-test-verification.md.

#include "engine/ComickCatalogClient.h"

#include <QString>
#include <QtTest>

using tankoban::manga::comick::dbUrlFor;

namespace {
constexpr const char* kDbBaseUrl =
    "https://raw.githubusercontent.com/kingoftheseas56/colosseum-volume-db/main/db/";
}

class tst_comick_db_url : public QObject
{
    Q_OBJECT

private slots:
    void builds_data();
    void builds();
};

void tst_comick_db_url::builds_data()
{
    QTest::addColumn<QString>("weebCentralId");
    QTest::addColumn<QString>("malId");
    QTest::addColumn<QString>("expected");

    QTest::newRow("empty MAL id -> existing ULID path, unchanged")
        << QStringLiteral("01J76XY7E4JCPK14V53BVQWD9Y") << QString()
        << QString::fromLatin1(kDbBaseUrl) + QStringLiteral("01J76XY7E4JCPK14V53BVQWD9Y.json");

    QTest::newRow("non-empty MAL id -> mal-<id> key")
        << QString() << QStringLiteral("21")
        << QString::fromLatin1(kDbBaseUrl) + QStringLiteral("mal-21.json");

    QTest::newRow("mal id wins over a present weebCentralId")
        << QStringLiteral("01J76XY7E4JCPK14V53BVQWD9Y") << QStringLiteral("21")
        << QString::fromLatin1(kDbBaseUrl) + QStringLiteral("mal-21.json");

    QTest::newRow("whitespace-padded MAL id is trimmed before the mal- key is built")
        << QString() << QStringLiteral("  21  ")
        << QString::fromLatin1(kDbBaseUrl) + QStringLiteral("mal-21.json");

    QTest::newRow("reserved characters in a ULID-shaped id are percent-encoded")
        << QStringLiteral("weird id/with?reserved&chars=1") << QString()
        << QString::fromLatin1(kDbBaseUrl)
               + QStringLiteral("weird%20id%2Fwith%3Freserved%26chars%3D1.json");

    QTest::newRow("reserved characters in a MAL id are percent-encoded")
        << QString() << QStringLiteral("21/beta?x=1")
        << QString::fromLatin1(kDbBaseUrl) + QStringLiteral("mal-21%2Fbeta%3Fx%3D1.json");

    QTest::newRow("both empty -> empty key, still well-formed")
        << QString() << QString()
        << QString::fromLatin1(kDbBaseUrl) + QStringLiteral(".json");
}

void tst_comick_db_url::builds()
{
    QFETCH(QString, weebCentralId);
    QFETCH(QString, malId);
    QFETCH(QString, expected);
    QCOMPARE(dbUrlFor(weebCentralId, malId), expected);
}

QTEST_APPLESS_MAIN(tst_comick_db_url)
#include "tst_comick_db_url.moc"
