// ExtensionsStore.h
//
// The extension registry behind the Extensions page: which Stremio-protocol
// addons the house carries, in what order, on or off. Spec:
// docs/superpowers/specs/2026-07-05-colosseum-extensions-store-design.md
// (Brotherhood repo). Ratified mock: agents/colosseum-extensions-mock.html.
//
// What it is (plain): a list of {id, transportUrl, manifest} entries persisted
// to <appdata>/extensions/installed.json (QSaveFile atomic, the MangaDownloader
// index pattern). Install = fetch the manifest over HTTP, validate id+name,
// slim it (strip data-URIs, cap description), persist. Order = the array order;
// when Theatre asks its stream extensions for play sources it asks top-first.
//
// Law folded in at THIS layer (not a setting):
//   - adult extensions (behaviorHints.adult) are refused at preview AND install;
//   - no Stremio-account sync — the file is the only store;
//   - first run seeds the four house extensions Theatre already runs on
//     (Cinemeta core/locked, Torrentio, Anime Kitsu, OpenSubtitles v3), so the
//     store tells the truth about the present from day one.
//
// Threading: pure QNetworkAccessManager + lambdas on the main thread.

#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class QJsonObject;

class ExtensionsStore : public QObject
{
    Q_OBJECT
    // bump-on-change counter so QML rebinds: (Extensions.revision, Extensions.installed())
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit ExtensionsStore(QNetworkAccessManager* nam, QObject* parent = nullptr);

    int revision() const { return m_revision; }

    // Ordered entries: { id, transportUrl, installedAt, enabled, core, manifest }.
    Q_INVOKABLE QVariantList installed() const;

    // True if a transportUrl (any form) or manifest id is already carried.
    Q_INVOKABLE bool isInstalled(const QString& urlOrId) const;

    // The bundled universe payload for an installed universe extension. C++ owns this read
    // because Qt blocks XMLHttpRequest on file:// by default (QML_XHR_ALLOW_FILE_READ) —
    // and because house doctrine keeps transport out of the GUI thread's JS. Returns the
    // raw JSON text; validation stays in UniverseExtApi.js. Empty string on any failure.
    Q_INVOKABLE QString universePayload(const QString& file) const;

    // Paste-a-link step 1: fetch + validate the manifest, emit previewReady with
    // the slimmed manifest so the sheet can show what it offers BEFORE anything
    // is added. Adult manifests are refused here (previewFailed).
    Q_INVOKABLE void preview(const QString& rawUrl);

    // Install (curated one-click or sheet confirm). Uses the preview cache when
    // present, otherwise fetches. Emits installFinished / installFailed.
    Q_INVOKABLE void install(const QString& rawUrl);

    Q_INVOKABLE void remove(const QString& id);          // core rows refuse
    Q_INVOKABLE void setEnabled(const QString& id, bool on);  // core rows refuse
    // Absolute reorder. NOT ±steps: a world-relative arrow press is not a global
    // neighbour swap, so QML resolves the destination (it owns world derivation) and
    // this just performs it. Core rows refuse — catalogues are never ranked.
    Q_INVOKABLE void moveTo(const QString& id, int index);

    // "stremio://host/manifest.json" → "https://host/manifest.json";
    // bare host/path gets "/manifest.json" appended. Exposed for the sheet's echo.
    Q_INVOKABLE QString normalizeUrl(const QString& raw) const;

signals:
    void changed();
    void previewReady(const QString& transportUrl, const QVariantMap& manifest);
    void previewFailed(const QString& transportUrl, const QString& reason);
    void installFinished(const QString& id, const QString& name);
    void installFailed(const QString& transportUrl, const QString& reason);

private:
    void loadIndex();
    void saveIndex() const;
    void seed();                       // first run: every house catalogue + well
    void migrateDefaults();            // existing install: add house rows a newer
                                       // defaults version introduced, once only
    bool appendHouseDefaults(bool onlyMissing);   // true if anything added or refreshed
    void bump();
    int  indexOfId(const QString& id) const;
    QString indexPath() const;         // <appdata>/extensions/installed.json

    void fetchManifest(const QString& transportUrl, bool thenInstall);
    void finishInstall(const QString& transportUrl, const QVariantMap& slim);
    static QVariantMap slimManifest(const QJsonObject& m);
    static bool manifestIsAdult(const QVariantMap& slim);

    QNetworkAccessManager* m_nam = nullptr;
    QList<QVariantMap> m_items;                 // ordered — array order IS ask-order
    QHash<QString, QVariantMap> m_previewCache; // transportUrl → slim manifest
    int m_revision = 0;
    // Which generation of house defaults this profile has already been given.
    // Absent from an older installed.json → treated as 1 (the original four).
    // Bumping kHouseDefaultsVersion adds the new rows once, and never again — so a
    // row the user deliberately removed does not come back.
    int m_defaultsVersion = 0;
};
