// Reader2Bridge.h
//
// The fresh reader's native seam (TASK 4). Registered on the paper's
// QWebChannel as "bridge" AND exposed to QML as context property
// "Reader2Bridge" (both point at the same instance): the paper pulls book
// bytes (base64) through it and pushes events up through it; QML reads/writes
// the shared stores through it and receives the same paperEventReceived
// signal the paper's events raise. Networking (dictionary lookups) lives
// here, never in the paper's JS — house rule "QML paints, C++ decides": no
// raw XHR on the paper's web-content thread.
//
// Store methods delegate to BookStores (native/reader/BookStores.h) — the
// SAME files the OLD reader's BookBridge uses under
// <AppDataLocation>/book_reader/ (progress.json, settings.json,
// bookmarks.json, annotations.json) — so both readers share state
// byte-identically with zero migration.
//
// Slim on purpose: no audiobook/window-chrome methods here. Those stay on the
// old BookBridge until swap day (Task 16).
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
class QNetworkAccessManager;
// Reader2Bridge — the fresh reader's native seam. Registered on the paper's
// QWebChannel as "bridge" AND exposed to QML as context property "Reader2Bridge".
// Paper pulls book bytes (base64) and pushes events; QML reads/writes the stores
// and receives the same events. Networking (dictionary) lives here, never in JS.
class Reader2Bridge : public QObject {
    Q_OBJECT
public:
    explicit Reader2Bridge(QObject* parent = nullptr);
    // paper-facing
    Q_INVOKABLE QString filesRead(const QString& filePath);     // base64, "" on error
    Q_INVOKABLE void paperEvent(const QString& name, const QString& json);
    // Canonical store key — SHA1[:20] of the path-normalized absolute path. MUST match
    // the old reader's BookBridge::progressKey byte-for-byte: progress.json AND
    // bookmarks.json AND annotations.json are all keyed by this fingerprint (the old
    // reader sets state.book.id = keyFor(path) before every save/read). QML derives
    // bookId through this so the fresh reader finds the old reader's records — the
    // zero-migration promise. Never key the stores by the raw path.
    Q_INVOKABLE QString bookKey(const QString& absPath) const;
    // QML-facing stores (delegate to BookStores — same files as old reader)
    Q_INVOKABLE QJsonObject progressGet(const QString& bookId);
    Q_INVOKABLE void progressSave(const QString& bookId, const QJsonObject& data);
    Q_INVOKABLE QJsonObject settingsGet();
    Q_INVOKABLE void settingsSave(const QJsonObject& data);
    Q_INVOKABLE QJsonArray bookmarksGet(const QString& bookId);
    Q_INVOKABLE QJsonObject bookmarksSave(const QString& bookId, const QJsonObject& bm);
    Q_INVOKABLE void bookmarksDelete(const QString& bookId, const QString& id);
    Q_INVOKABLE QJsonArray annotationsGet(const QString& bookId);
    Q_INVOKABLE QJsonObject annotationsSave(const QString& bookId, const QJsonObject& an);
    Q_INVOKABLE void annotationsDelete(const QString& bookId, const QString& id);
    // dictionary — Wiktionary REST, C++ side
    Q_INVOKABLE void dictLookup(const QString& word);
signals:
    void paperEventReceived(const QString& name, const QString& json);
    void dictResult(const QString& word, const QString& json, bool ok);
private:
    QNetworkAccessManager* m_nam;
    // IPv4 pin for the Wiktionary host (house scar: Wikimedia publishes AAAA records and
    // Qt-on-Windows stalls ~21s on the dead IPv6 route). Resolved once, then reused; empty
    // string = resolution failed, fall back to the plain hostname. See dictLookup().
    QString m_wiktIpv4;
    bool m_wiktResolved = false;
};
