// Reader2Bridge.h
//
// The fresh reader's native seam (TASK 4). Exposed to QML as context property
// "Reader2Bridge"; the paper's QWebChannel gets ONLY the nested Reader2PaperGate
// (registered as "bridge" by Paper.qml — least privilege, see the gate class
// below): the paper pulls book bytes (base64) and pushes events up through the
// gate; QML reads/writes the shared stores through the full bridge and receives
// the same paperEventReceived signal the paper's events raise. Networking
// (dictionary lookups) lives here, never in the paper's JS — house rule "QML
// paints, C++ decides": no raw XHR on the paper's web-content thread.
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
// Reader2Bridge — the fresh reader's native seam. QML context property
// "Reader2Bridge"; the paper's QWebChannel sees only the paperGate (below).
// Paper pulls book bytes (base64) and pushes events through the gate; QML
// reads/writes the stores and receives the same events. Networking
// (dictionary) lives here, never in JS.
class Reader2PaperGate;

class Reader2Bridge : public QObject {
    Q_OBJECT
    // The paper-facing gate (LEAST PRIVILEGE — Codex re-review fix): QWebChannel exposes EVERY
    // invokable/slot/property of a registered object to the page, so registering this bridge
    // itself handed the untrusted paper setAuthorizedBook (self-authorize any file, then
    // filesRead it), every store write, and dictLookup. Paper.qml now registers ONLY this gate
    // (Reader2Bridge.paperGate) on the channel; the gate exposes exactly filesRead + paperEvent
    // and nothing else. The full bridge stays QML-side only (context property).
    Q_PROPERTY(QObject* paperGate READ paperGate CONSTANT)
public:
    explicit Reader2Bridge(QObject* parent = nullptr);
    QObject* paperGate() const;
    // paper-facing (reached through Reader2PaperGate; also directly callable by QML/tests)
    Q_INVOKABLE QString filesRead(const QString& filePath);     // base64, "" on error/unauthorized
    // Authorize which book filesRead may serve (hardening). The paper is UNTRUSTED web content;
    // without this it could pull ANY file off disk through the bridge. ReaderShell calls this
    // with the book path BEFORE every paper.open, so filesRead serves ONLY the currently-open
    // book and refuses ("") any other path. Stores a normalized (canonical) copy.
    // QML-ONLY: never exposed on the web channel (the gate doesn't carry it) — the paper must
    // not be able to authorize itself.
    Q_INVOKABLE void setAuthorizedBook(const QString& absPath);
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
    // Fire the actual Wiktionary GET for `word` (URL term = `query`), with the IPv4 pin,
    // a `timeoutMs` timeout, and a ~512 KB response cap. Called either directly (host already
    // resolved — full 8s budget) or from the async DNS callback in dictLookup (the REMAINING
    // budget of the one overall 8s deadline, so DNS+HTTP never exceed 8s total). Emits
    // dictResult in every terminal path (ok / error / timeout / too-big). `word` is the emit
    // key (matches the QML dictWord); `query` is the possibly-trimmed lookup term in the URL.
    void sendDictRequest(const QString& word, const QString& query, int timeoutMs);

    QNetworkAccessManager* m_nam;
    // The one book filesRead is allowed to serve (normalized/canonical). Empty = nothing
    // authorized yet → filesRead refuses everything. Set by setAuthorizedBook() per open.
    QString m_authorizedBook;
    // IPv4 pin for the Wiktionary host (house scar: Wikimedia publishes AAAA records and
    // Qt-on-Windows stalls ~21s on the dead IPv6 route). Resolved once, then reused; empty
    // string = resolution failed, fall back to the plain hostname. See dictLookup().
    QString m_wiktIpv4;
    bool m_wiktResolved = false;
    Reader2PaperGate* m_paperGate;   // the web channel's ONLY registered object (see Q_PROPERTY)
};

// Reader2PaperGate — the paper's ENTIRE native surface. This is the only object registered on
// the paper's QWebChannel ("bridge"): the untrusted web content can pull the authorized book's
// bytes (filesRead) and push events up (paperEvent) — and can reach NOTHING else. Authorization
// (setAuthorizedBook), the shared stores, and the dictionary live on Reader2Bridge, which is a
// QML context property only and never touches the channel. Both methods delegate; neither adds
// behavior. Keep this class METHOD-MINIMAL — every public slot/invokable/property added here is
// handed straight to untrusted book content.
class Reader2PaperGate : public QObject {
    Q_OBJECT
public:
    explicit Reader2PaperGate(Reader2Bridge* bridge);
    Q_INVOKABLE QString filesRead(const QString& filePath);
    Q_INVOKABLE void paperEvent(const QString& name, const QString& json);
private:
    Reader2Bridge* m_bridge;
};
