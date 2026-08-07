// tst_http_header_fields — Theatre House HTTP Source, slice 1. Data-row unit test for the pure
// httpHeaderFieldsList() formatter (native/player/http_header_fields.h): the map→mpv
// http-header-fields conversion, WITHOUT an mpv instance. The wire behaviour (that the formatted
// list actually reaches ffmpeg's request, and that loadFile clears it) is proven separately by
// tests/http_header_channel_harness.cpp.
//
// Non-vacuous by construction: if the formatter comma-joined instead of listing, the "comma in
// value" row fails; if the CRLF/colon guards were absent, the injection rows fail. Negative control
// performed at build time — flip any one expected value and exactly that row goes red (per-row Qt
// Test reporting), then restore.

#include "player/http_header_fields.h"

#include <QStringList>
#include <QVariantMap>
#include <QtTest>

class tst_http_header_fields : public QObject
{
    Q_OBJECT

private slots:
    void formats_data();
    void formats();
    void drops_injection_attempts_data();
    void drops_injection_attempts();
};

void tst_http_header_fields::formats_data()
{
    QTest::addColumn<QVariantMap>("headers");
    QTest::addColumn<QStringList>("expected");

    QTest::newRow("empty map -> no entries")
        << QVariantMap() << QStringList();
    QTest::newRow("one pair")
        << QVariantMap{{"Referer", "https://prov.example/"}}
        << QStringList{"Referer: https://prov.example/"};
    // QVariantMap iterates keys sorted, so multi-header output is deterministic: Origin < Referer.
    QTest::newRow("multiple pairs, sorted by key")
        << QVariantMap{{"Referer", "https://prov.example/p"}, {"Origin", "https://prov.example"}}
        << QStringList{"Origin: https://prov.example", "Referer: https://prov.example/p"};
    // The comma bug the node-array format exists to prevent: a comma in a value must NOT split.
    QTest::newRow("comma in value stays one entry")
        << QVariantMap{{"X-Thing", "a,b,c"}}
        << QStringList{"X-Thing: a,b,c"};
    // A colon inside the value (every https URL has one) must survive — we join key + ": " + value.
    QTest::newRow("colon in value preserved")
        << QVariantMap{{"Referer", "https://host:8080/path"}}
        << QStringList{"Referer: https://host:8080/path"};
    QTest::newRow("surrounding whitespace trimmed")
        << QVariantMap{{" Referer ", " https://prov.example/ "}}
        << QStringList{"Referer: https://prov.example/"};
}

void tst_http_header_fields::formats()
{
    QFETCH(QVariantMap, headers);
    QFETCH(QStringList, expected);
    QCOMPARE(httpHeaderFieldsList(headers), expected);
}

// The headers map is third-party addon JSON. ffmpeg joins entries with CRLF into the raw request,
// so a value with "\r\n" is a header-injection vector and a key with ':' or whitespace corrupts the
// field. Every such entry must be DROPPED, not sanitized-and-sent.
void tst_http_header_fields::drops_injection_attempts_data()
{
    QTest::addColumn<QVariantMap>("headers");
    QTest::addColumn<QStringList>("expected");

    QTest::newRow("CRLF in value is dropped")
        << QVariantMap{{"Referer", "https://x/\r\nEvil: 1"}} << QStringList();
    QTest::newRow("CRLF in key is dropped")
        << QVariantMap{{"Ev\r\nil", "1"}} << QStringList();
    QTest::newRow("colon in key is dropped")
        << QVariantMap{{"Re:fer", "x"}} << QStringList();
    QTest::newRow("space in key is dropped")
        << QVariantMap{{"Re fer", "x"}} << QStringList();
    QTest::newRow("empty key is dropped")
        << QVariantMap{{"", "x"}} << QStringList();
    // A poisoned entry must not take the legitimate one down with it. Sorted: "Ev\r\nil" < "Referer".
    QTest::newRow("bad entry dropped, good entry kept")
        << QVariantMap{{"Referer", "https://ok/"}, {"Ev\r\nil", "1"}}
        << QStringList{"Referer: https://ok/"};
}

void tst_http_header_fields::drops_injection_attempts()
{
    QFETCH(QVariantMap, headers);
    QFETCH(QStringList, expected);
    QCOMPARE(httpHeaderFieldsList(headers), expected);
}

QTEST_APPLESS_MAIN(tst_http_header_fields)
#include "tst_http_header_fields.moc"
