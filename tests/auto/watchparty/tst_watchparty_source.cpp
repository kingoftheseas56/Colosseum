// Watch Party Slice 2 — authoritative source identity and redaction.
// No player/network/provider instance is constructed: this drives the pure classifier
// against the exact candidate shape PlayerPage receives.

#include "watchparty/WatchPartyProtocol.h"
#include "watchparty/WatchPartySource.h"
#include "watchparty/WatchPartyServiceEndpoint.h"

#include <QJsonObject>
#include <QtTest>

namespace WatchParty = Colosseum::WatchParty;

class tst_watchparty_source final : public QObject
{
    Q_OBJECT

private slots:
    void valid_torrent_candidate_is_eligible_and_normalized();
    void direct_candidate_is_unsupported_even_when_names_claim_debrid();
    void direct_route_wins_when_candidate_also_contains_a_hash();
    void url_resume_prefix_is_unsupported_without_provenance();
    void invalid_torrent_identity_is_unsupported();
    void verified_debrid_requires_explicit_safe_provider_descriptor();
    void protocol_round_trips_torrent_and_debrid_descriptors();
    void protocol_rejects_unknown_secret_bearing_fields();
    void serializer_rejects_url_shaped_debrid_reference();
    void service_endpoint_uses_hosted_default_when_unset();
    void service_endpoint_environment_override_wins();
};

void tst_watchparty_source::valid_torrent_candidate_is_eligible_and_normalized()
{
    const QVariantMap candidate{
        {QStringLiteral("infoHash"),
         QStringLiteral("ABCDEF0123456789ABCDEF0123456789ABCDEF01")},
        {QStringLiteral("fileIdx"), 4},
        {QStringLiteral("addonId"), QStringLiteral(" com.example.torrent ")}
    };

    const WatchParty::SourceInspection result =
        WatchParty::SourceInspector::inspectCandidate(candidate);

    QVERIFY(result.eligible());
    QCOMPARE(result.eligibility, WatchParty::SourceEligibility::Torrent);
    QCOMPARE(result.descriptor.kind, WatchParty::SourceKind::Torrent);
    QCOMPARE(result.descriptor.infoHash,
             QStringLiteral("abcdef0123456789abcdef0123456789abcdef01"));
    QCOMPARE(result.descriptor.fileIdx, 4);
    QCOMPARE(result.addonId, QStringLiteral("com.example.torrent"));

    const QVariantMap exposed = WatchParty::SourceInspector::toVariantMap(result);
    QCOMPARE(exposed.value(QStringLiteral("eligible")).toBool(), true);
    QCOMPARE(exposed.value(QStringLiteral("eligibility")).toString(),
             QStringLiteral("torrent"));
    const QVariantMap descriptor =
        exposed.value(QStringLiteral("descriptor")).toMap();
    QCOMPARE(descriptor.value(QStringLiteral("infoHash")).toString(),
             result.descriptor.infoHash);
    QCOMPARE(descriptor.value(QStringLiteral("fileIdx")).toInt(), 4);
    QVERIFY(!descriptor.contains(QStringLiteral("url")));
    QVERIFY(!descriptor.contains(QStringLiteral("headers")));
}

void tst_watchparty_source::direct_candidate_is_unsupported_even_when_names_claim_debrid()
{
    // Negative control: names/hosts/headers are deliberately tempting. If the
    // classifier ever starts guessing "debrid" from any of them, this goes red.
    const QVariantMap candidate{
        {QStringLiteral("infoHash"),
         QStringLiteral("url:https://cdn.example/video.mkv?token=secret")},
        {QStringLiteral("url"),
         QStringLiteral("https://cdn.example/video.mkv?token=secret")},
        {QStringLiteral("fileIdx"), 0},
        {QStringLiteral("addonId"), QStringLiteral("com.real-debrid-looking.addon")},
        {QStringLiteral("addonName"), QStringLiteral("REAL DEBRID PREMIUM")},
        {QStringLiteral("headers"),
         QVariantMap{{QStringLiteral("Authorization"),
                      QStringLiteral("Bearer secret")}}}
    };

    const WatchParty::SourceInspection result =
        WatchParty::SourceInspector::inspectCandidate(candidate);

    QVERIFY(!result.eligible());
    QCOMPARE(result.eligibility, WatchParty::SourceEligibility::Unsupported);
    QCOMPARE(result.reason, QStringLiteral("direct_source_not_verified_debrid"));
    QVERIFY(!result.descriptor.isValid());

    const QVariantMap exposed = WatchParty::SourceInspector::toVariantMap(result);
    QVERIFY(!exposed.contains(QStringLiteral("url")));
    QVERIFY(!exposed.contains(QStringLiteral("headers")));
    QVERIFY(exposed.value(QStringLiteral("descriptor")).toMap().isEmpty());
}

void tst_watchparty_source::direct_route_wins_when_candidate_also_contains_a_hash()
{
    // PlayerPage.playStreamAt() chooses c.url before Stream.play(infoHash,...).
    // Eligibility must mirror that actual route instead of blessing the dormant hash.
    const QVariantMap candidate{
        {QStringLiteral("infoHash"),
         QStringLiteral("0123456789abcdef0123456789abcdef01234567")},
        {QStringLiteral("fileIdx"), 2},
        {QStringLiteral("url"), QStringLiteral("https://host.example/file")}
    };

    const WatchParty::SourceInspection result =
        WatchParty::SourceInspector::inspectCandidate(candidate);
    QVERIFY(!result.eligible());
    QCOMPARE(result.reason, QStringLiteral("direct_source_not_verified_debrid"));
}

void tst_watchparty_source::url_resume_prefix_is_unsupported_without_provenance()
{
    const QVariantMap candidate{
        {QStringLiteral("infoHash"),
         QStringLiteral("url:https://host.example/resumed")},
        {QStringLiteral("fileIdx"), 0}
    };

    const WatchParty::SourceInspection result =
        WatchParty::SourceInspector::inspectCandidate(candidate);
    QVERIFY(!result.eligible());
    QCOMPARE(result.reason, QStringLiteral("direct_source_not_verified_debrid"));
}

void tst_watchparty_source::invalid_torrent_identity_is_unsupported()
{
    const QVariantMap candidate{
        {QStringLiteral("infoHash"), QStringLiteral("not-a-btih")},
        {QStringLiteral("fileIdx"), -1}
    };

    const WatchParty::SourceInspection result =
        WatchParty::SourceInspector::inspectCandidate(candidate);
    QVERIFY(!result.eligible());
    QCOMPARE(result.reason, QStringLiteral("invalid_torrent_identity"));

    QVariantMap fractional = candidate;
    fractional.insert(QStringLiteral("infoHash"),
                      QStringLiteral("0123456789abcdef0123456789abcdef01234567"));
    fractional.insert(QStringLiteral("fileIdx"), 1.5);
    const WatchParty::SourceInspection fractionalResult =
        WatchParty::SourceInspector::inspectCandidate(fractional);
    QVERIFY(!fractionalResult.eligible());
    QCOMPARE(fractionalResult.reason, QStringLiteral("invalid_torrent_identity"));
}

void tst_watchparty_source::verified_debrid_requires_explicit_safe_provider_descriptor()
{
    // This helper is intentionally NOT Q_INVOKABLE and no current extension row
    // calls it. It is the future source-owner seam after provider semantics are verified.
    const WatchParty::SourceInspection valid =
        WatchParty::SourceInspector::verifiedDebrid(
            QStringLiteral("verified-provider"),
            QStringLiteral("source:abc123"),
            QStringLiteral("com.example.verified"));

    QVERIFY(valid.eligible());
    QCOMPARE(valid.eligibility, WatchParty::SourceEligibility::Debrid);
    QCOMPARE(valid.descriptor.kind, WatchParty::SourceKind::Debrid);
    QCOMPARE(valid.descriptor.providerId, QStringLiteral("verified-provider"));
    QCOMPARE(valid.descriptor.providerSourceId, QStringLiteral("source:abc123"));

    const WatchParty::SourceInspection urlReference =
        WatchParty::SourceInspector::verifiedDebrid(
            QStringLiteral("verified-provider"),
            QStringLiteral("https://provider.example/file?token=secret"));
    QVERIFY(!urlReference.eligible());
    QCOMPARE(urlReference.reason, QStringLiteral("invalid_debrid_descriptor"));
}

void tst_watchparty_source::protocol_round_trips_torrent_and_debrid_descriptors()
{
    const WatchParty::SourceDescriptor torrent =
        WatchParty::SourceDescriptor::torrent(
            QStringLiteral("0123456789abcdef0123456789abcdef01234567"), 9);
    const QJsonObject torrentJson = WatchParty::sourceDescriptorToJson(torrent);
    QCOMPARE(torrentJson.value(QStringLiteral("kind")).toString(),
             QStringLiteral("torrent"));

    WatchParty::SourceDescriptor parsedTorrent;
    QString error;
    QVERIFY2(WatchParty::sourceDescriptorFromJson(torrentJson,
                                                  &parsedTorrent, &error),
             qPrintable(error));
    QCOMPARE(parsedTorrent.kind, WatchParty::SourceKind::Torrent);
    QCOMPARE(parsedTorrent.infoHash, torrent.infoHash);
    QCOMPARE(parsedTorrent.fileIdx, 9);

    const WatchParty::SourceDescriptor debrid =
        WatchParty::SourceDescriptor::debrid(
            QStringLiteral("verified-provider"),
            QStringLiteral("source:abc123"));
    const QJsonObject debridJson = WatchParty::sourceDescriptorToJson(debrid);
    QCOMPARE(debridJson.value(QStringLiteral("kind")).toString(),
             QStringLiteral("debrid"));

    WatchParty::SourceDescriptor parsedDebrid;
    error.clear();
    QVERIFY2(WatchParty::sourceDescriptorFromJson(debridJson,
                                                  &parsedDebrid, &error),
             qPrintable(error));
    QCOMPARE(parsedDebrid.kind, WatchParty::SourceKind::Debrid);
    QCOMPARE(parsedDebrid.providerId, debrid.providerId);
    QCOMPARE(parsedDebrid.providerSourceId, debrid.providerSourceId);
}

void tst_watchparty_source::protocol_rejects_unknown_secret_bearing_fields()
{
    QJsonObject torrent{
        {QStringLiteral("kind"), QStringLiteral("torrent")},
        {QStringLiteral("infoHash"),
         QStringLiteral("0123456789abcdef0123456789abcdef01234567")},
        {QStringLiteral("fileIdx"), 0},
        {QStringLiteral("headers"),
         QJsonObject{{QStringLiteral("Authorization"),
                      QStringLiteral("Bearer secret")}}}
    };

    WatchParty::SourceDescriptor parsed;
    QString error;
    QVERIFY(!WatchParty::sourceDescriptorFromJson(torrent, &parsed, &error));
    QVERIFY(error.contains(QStringLiteral("source key")));

    QJsonObject debrid{
        {QStringLiteral("kind"), QStringLiteral("debrid")},
        {QStringLiteral("providerId"), QStringLiteral("verified-provider")},
        {QStringLiteral("providerSourceId"), QStringLiteral("source:abc123")},
        {QStringLiteral("url"),
         QStringLiteral("https://provider.example/file?token=secret")}
    };

    error.clear();
    QVERIFY(!WatchParty::sourceDescriptorFromJson(debrid, &parsed, &error));
    QVERIFY(error.contains(QStringLiteral("source key")));
}

void tst_watchparty_source::serializer_rejects_url_shaped_debrid_reference()
{
    // Negative control: even a caller that labels the row "debrid" cannot push a
    // resolved URL/query token into room state through SourceDescriptor.
    WatchParty::SourceDescriptor poisoned;
    poisoned.kind = WatchParty::SourceKind::Debrid;
    poisoned.providerId = QStringLiteral("verified-provider");
    poisoned.providerSourceId =
        QStringLiteral("https://provider.example/file?token=secret");

    QVERIFY(!poisoned.isValid());
    QVERIFY(WatchParty::sourceDescriptorToJson(poisoned).isEmpty());
}

void tst_watchparty_source::service_endpoint_uses_hosted_default_when_unset()
{
    const QByteArray previous = qgetenv("COLOSSEUM_WATCH_PARTY_URL");
    qunsetenv("COLOSSEUM_WATCH_PARTY_URL");

    QCOMPARE(WatchParty::ServiceEndpoint::configuredUrl(),
             QUrl(QStringLiteral("wss://colosseum-watchparty-relay.colosseum-watchparty-relay.workers.dev")));

    if (previous.isNull())
        qunsetenv("COLOSSEUM_WATCH_PARTY_URL");
    else
        qputenv("COLOSSEUM_WATCH_PARTY_URL", previous);
}

void tst_watchparty_source::service_endpoint_environment_override_wins()
{
    const QByteArray previous = qgetenv("COLOSSEUM_WATCH_PARTY_URL");
    qputenv("COLOSSEUM_WATCH_PARTY_URL", QByteArrayLiteral("wss://party.example.test/v3"));

    QCOMPARE(WatchParty::ServiceEndpoint::configuredUrl(),
             QUrl(QStringLiteral("wss://party.example.test/v3")));

    if (previous.isNull())
        qunsetenv("COLOSSEUM_WATCH_PARTY_URL");
    else
        qputenv("COLOSSEUM_WATCH_PARTY_URL", previous);
}

QTEST_APPLESS_MAIN(tst_watchparty_source)
#include "tst_watchparty_source.moc"
