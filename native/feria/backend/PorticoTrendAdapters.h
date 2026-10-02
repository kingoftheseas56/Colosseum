#pragma once

#include "PorticoTrendAdapter.h"

class StremioCatalogAdapter final : public PorticoTrendAdapter
{
    Q_OBJECT
public:
    using PorticoTrendAdapter::PorticoTrendAdapter;
    QString id() const override { return QStringLiteral("stremio"); }
    QString sourceLabel() const override { return QStringLiteral("Stremio Catalog"); }
    QString medium() const override { return QStringLiteral("watch"); }
    QString stability() const override { return QStringLiteral("public-protocol"); }
    QString defaultTitle() const override { return QStringLiteral("Trending to watch"); }
    bool isConfigured(const QVariantMap &config) const override;
    void fetch(const PorticoTrend::Query &query, const QVariantMap &config) override;
    static PorticoTrend::Shelf parsePayload(const QByteArray &body,
        const PorticoTrend::Query &query, const QVariantMap &config, QString *error);
};

class OpenLibraryTrendAdapter final : public PorticoTrendAdapter
{
    Q_OBJECT
public:
    using PorticoTrendAdapter::PorticoTrendAdapter;
    QString id() const override { return QStringLiteral("openlibrary"); }
    QString sourceLabel() const override { return QStringLiteral("Open Library"); }
    QString medium() const override { return QStringLiteral("read"); }
    QString stability() const override { return QStringLiteral("public-api"); }
    QString defaultTitle() const override { return QStringLiteral("Trending books"); }
    void fetch(const PorticoTrend::Query &query, const QVariantMap &config) override;
    static PorticoTrend::Shelf parsePayload(const QByteArray &body,
        const PorticoTrend::Query &query, const QVariantMap &config, QString *error);
};

class AniListTrendAdapter final : public PorticoTrendAdapter
{
    Q_OBJECT
public:
    using PorticoTrendAdapter::PorticoTrendAdapter;
    QString id() const override { return QStringLiteral("anilist"); }
    QString sourceLabel() const override { return QStringLiteral("AniList"); }
    QString medium() const override { return QStringLiteral("read"); }
    QString stability() const override { return QStringLiteral("public-api"); }
    QString defaultTitle() const override { return QStringLiteral("Trending manga"); }
    void fetch(const PorticoTrend::Query &query, const QVariantMap &config) override;
    static PorticoTrend::Shelf parsePayload(const QByteArray &body,
        const PorticoTrend::Query &query, const QVariantMap &config, QString *error);
};

class AppleMusicChartsAdapter final : public PorticoTrendAdapter
{
    Q_OBJECT
public:
    using PorticoTrendAdapter::PorticoTrendAdapter;
    QString id() const override { return QStringLiteral("applemusic"); }
    QString sourceLabel() const override { return QStringLiteral("Apple Music"); }
    QString medium() const override { return QStringLiteral("listen"); }
    QString stability() const override { return QStringLiteral("public-api-authenticated"); }
    QString defaultTitle() const override { return QStringLiteral("Top albums"); }
    bool isConfigured(const QVariantMap &config) const override;
    void fetch(const PorticoTrend::Query &query, const QVariantMap &config) override;
    static PorticoTrend::Shelf parsePayload(const QByteArray &body,
        const PorticoTrend::Query &query, const QVariantMap &config, QString *error);
};

class YouTubeChartsAdapter final : public PorticoTrendAdapter
{
    Q_OBJECT
public:
    using PorticoTrendAdapter::PorticoTrendAdapter;
    QString id() const override { return QStringLiteral("youtube"); }
    QString sourceLabel() const override { return QStringLiteral("YouTube Charts"); }
    QString medium() const override { return QStringLiteral("listen"); }
    QString stability() const override { return QStringLiteral("private-web"); }
    QString defaultTitle() const override { return QStringLiteral("Top songs this week"); }
    void fetch(const PorticoTrend::Query &query, const QVariantMap &config) override;
    static PorticoTrend::Shelf parsePayload(const QByteArray &body,
        const PorticoTrend::Query &query, const QVariantMap &config, QString *error);
};

class WebtoonTrendAdapter final : public PorticoTrendAdapter
{
    Q_OBJECT
public:
    using PorticoTrendAdapter::PorticoTrendAdapter;
    QString id() const override { return QStringLiteral("webtoon"); }
    QString sourceLabel() const override { return QStringLiteral("WEBTOON"); }
    QString medium() const override { return QStringLiteral("read"); }
    QString stability() const override { return QStringLiteral("public-page"); }
    QString defaultTitle() const override { return QStringLiteral("Trending on WEBTOON"); }
    void fetch(const PorticoTrend::Query &query, const QVariantMap &config) override;
    static PorticoTrend::Shelf parsePayload(const QByteArray &body,
        const PorticoTrend::Query &query, const QVariantMap &config, QString *error);
};

class GlobalComixTrendAdapter final : public PorticoTrendAdapter
{
    Q_OBJECT
public:
    using PorticoTrendAdapter::PorticoTrendAdapter;
    QString id() const override { return QStringLiteral("globalcomix"); }
    QString sourceLabel() const override { return QStringLiteral("GlobalComix"); }
    QString medium() const override { return QStringLiteral("read"); }
    QString stability() const override { return QStringLiteral("public-page"); }
    QString defaultTitle() const override { return QStringLiteral("Popular comics"); }
    void fetch(const PorticoTrend::Query &query, const QVariantMap &config) override;
    static PorticoTrend::Shelf parsePayload(const QByteArray &body,
        const PorticoTrend::Query &query, const QVariantMap &config, QString *error);
};

class SpotifyChartsAdapter final : public PorticoTrendAdapter
{
    Q_OBJECT
public:
    using PorticoTrendAdapter::PorticoTrendAdapter;
    QString id() const override { return QStringLiteral("spotify"); }
    QString sourceLabel() const override { return QStringLiteral("Spotify Charts"); }
    QString medium() const override { return QStringLiteral("listen"); }
    QString stability() const override { return QStringLiteral("authorized-export"); }
    QString defaultTitle() const override { return QStringLiteral("Top songs on Spotify"); }
    bool isConfigured(const QVariantMap &config) const override;
    void fetch(const PorticoTrend::Query &query, const QVariantMap &config) override;
    static PorticoTrend::Shelf parsePayload(const QByteArray &body,
        const PorticoTrend::Query &query, const QVariantMap &config, QString *error);
};
