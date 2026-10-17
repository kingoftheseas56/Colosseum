#include "FeriaAccountStore.h"
#include "app-state/PorticoAppStateBridge.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QSet>
#include <QRegularExpression>
#include <cmath>
#include <algorithm>

namespace {
bool sensitiveBrowserKey(const QString &key) {
    const auto name = key.toLower();
    return QStringList{"code", "state", "token", "access_token", "refresh_token", "id_token",
        "client_secret", "password", "passwd", "authorization", "auth", "credential",
        "session", "sessionid", "session_id", "ticket", "code_verifier", "code_challenge",
        "signature", "sig", "api_key"}.contains(name) || name.endsWith("_token");
}

QString browserUrl(const QString &value, bool automatic) {
    QUrl url(value.trimmed());
    if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http") || url.host().isEmpty()) return {};
    static const QRegularExpression authRoute(
        "(^|/)(oauth[^/]*|authorize|authorization|callback|sign[-_]?in|sign[-_]?out|log[-_]?in|log[-_]?out|authenticate)(/|$)",
        QRegularExpression::CaseInsensitiveOption);
    if (automatic && authRoute.match(url.path()).hasMatch()) return {};
    url.setUserInfo({});
    const auto sanitize = [](const QString &query, bool *sensitive) {
        QUrlQuery kept;
        for (const auto &pair : QUrlQuery(query).queryItems()) {
            if (sensitiveBrowserKey(pair.first)) { *sensitive = true; continue; }
            kept.addQueryItem(pair.first, pair.second);
        }
        return kept.query(QUrl::FullyEncoded);
    };
    bool sensitive = false;
    const QString query = sanitize(url.query(QUrl::FullyEncoded), &sensitive);
    // OAuth may deliver credentials in a fragment rather than the query.
    const QString fragment = url.fragment(QUrl::FullyEncoded);
    bool fragmentSensitive = false;
    const QString cleanFragment = sanitize(fragment, &fragmentSensitive);
    if (automatic && (sensitive || fragmentSensitive)) return {};
    if (sensitive) url.setQuery(query, QUrl::StrictMode);
    if (fragmentSensitive) url.setFragment(cleanFragment, QUrl::StrictMode);
    return url.toString(QUrl::FullyEncoded);
}

bool knownBrowserProvider(const QString &provider) {
    return PorticoAppStateBridge::currentPorticoProviderIds().contains(provider);
}

QVariantMap validatedBrowserPreferences(const QVariantMap &stored) {
    QVariantMap result{{"restoreTabs", false}, {"freezeBackground", true}, {"searchEngine", "duckduckgo"}};
    for (const auto &key : {"restoreTabs", "freezeBackground"}) {
        const auto value = stored.value(key);
        if (value.metaType().id() == QMetaType::Bool) result.insert(key, value);
    }
    const auto search = stored.value("searchEngine");
    if (search.metaType().id() == QMetaType::QString
        && QStringList{"duckduckgo", "google", "bing"}.contains(search.toString()))
        result.insert("searchEngine", search);
    return result;
}

QVariantList validatedBrowserDescriptors(const QVariantList &descriptors) {
    QVariantList rows;
    QSet<QString> ids;
    for (const auto &value : descriptors) {
        if (value.metaType().id() != QMetaType::QVariantMap) continue;
        const auto descriptor = value.toMap();
        const auto inputUrl = descriptor.value("url"), provider = descriptor.value("providerId");
        if (inputUrl.metaType().id() != QMetaType::QString || inputUrl.toString().size() > 8192
            || provider.metaType().id() != QMetaType::QString || !knownBrowserProvider(provider.toString())) continue;
        if (descriptor.contains("privateBrowsing")
            && (descriptor.value("privateBrowsing").metaType().id() != QMetaType::Bool
                || descriptor.value("privateBrowsing").toBool())) continue;
        const auto url = browserUrl(inputUrl.toString(), true);
        if (url.isEmpty() || url.size() > 8192) continue;
        QVariantMap row{{"url", url}, {"providerId", provider}};
        bool valid = true;
        for (const auto &key : {"providerName", "title", "mode", "launchMode", "titleId", "logo", "restoreId"}) {
            if (!descriptor.contains(key)) continue;
            const auto text = descriptor.value(key);
            if (text.metaType().id() != QMetaType::QString || text.toString().size() > (QString(key) == "restoreId" ? 100 : 2048)) {
                valid = false; break;
            }
            row.insert(key, text);
        }
        if (!valid) continue;
        QString id = row.value("restoreId").toString();
        if (id.isEmpty()) id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (ids.contains(id)) continue;
        ids.insert(id); row.insert("restoreId", id);
        rows.append(row);
        if (rows.size() == 50) break;
    }
    return rows;
}

// Only a provider-specific rule can qualify an external media ID. Context comes
// from beginVisit(), never from the page payload. Other providers remain legacy.
QString youtubeMusicId(const QString &href) {
    const QUrl url(href);
    const auto ids = QUrlQuery(url).allQueryItemValues("v");
    if (url.scheme() != "https" || url.host() != "music.youtube.com"
        || (url.port(-1) != -1 && url.port() != 443) || url.path() != "/watch" || !url.userInfo().isEmpty() || ids.size() != 1) return {};
    static const QRegularExpression valid("^[A-Za-z0-9_-]{11}$");
    return valid.match(ids.first()).hasMatch() ? ids.first() : QString();
}
QString qualifiedIdentity(const QString &pk, const QString &id) {
    return "provider-media-v1\n" + pk + "\nyoutube-video\n" + id;
}
QString continueIdentity(const QVariantMap &row) {
    if (row.value("kind") == "book") {
        const auto url = FeriaAccountStore::safeUrl(row.value("url").toString());
        if (!url.isEmpty()) return "book\n" + row.value("pk").toString() + "\n" + url;
    }
    if (row.value("pk") == "ytmusic") {
        const auto provenId = youtubeMusicId(row.value("url").toString());
        const auto declaredId = row.value("providerMediaId").toString();
        if (!provenId.isEmpty() && (declaredId.isEmpty() || declaredId == provenId))
            return qualifiedIdentity("ytmusic", provenId);
    }
    return "media\n" + row.value("id").toString();
}
}

FeriaAccountStore::FeriaAccountStore(QObject *parent) : QObject(parent) {}

void FeriaAccountStore::setStoragePath(const QString &path) {
    m_restoreCaptured = false;
    m_writeFailed = false;
    m_visits.clear();
    m_downloadAuthorizations.clear();
    m_legacyToken.clear();
    m_path = path;
    m_state = {};
    m_error.clear();
    QFile file(path);
    if (!path.isEmpty() && file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) m_error = "Could not read Feria account data.";
        else {
            const auto doc = QJsonDocument::fromJson(file.readAll());
            if (!doc.isObject() || doc.object().value("version").toInt() != 1)
                m_error = "Feria account data could not be loaded. The saved file has been preserved.";
            else {
                m_state = doc.object().toVariantMap();
                m_state.insert("browserTabs", validatedBrowserDescriptors(m_state.value("browserTabs").toList()));
                m_state.insert("browserRestoreOffers", validatedBrowserDescriptors(m_state.value("browserRestoreOffers").toList()));
                m_state.insert("browserPreferences", validatedBrowserPreferences(m_state.value("browserPreferences").toMap()));
            }
        }
    }
    // Profile observers may immediately save restore offers. Release our read
    // handle before their atomic replacement of this file, especially on Windows.
    file.close();
    emit changed();
    emit profileChanged();
}

QVariantList FeriaAccountStore::sessions() const { return m_state.value("sessions").toList(); }
bool FeriaAccountStore::recording() const { return m_state.value("recording", true).toBool() && !m_path.isEmpty() && m_error.isEmpty(); }

QVariantList FeriaAccountStore::browserBookmarks() const { return m_state.value("browserBookmarks").toList(); }
QVariantList FeriaAccountStore::browserHistory() const { return m_state.value("browserHistory").toList(); }
QVariantList FeriaAccountStore::browserTabs() const { return m_state.value("browserTabs").toList(); }
QVariantList FeriaAccountStore::browserRestoreOffers() const { return m_state.value("browserRestoreOffers").toList(); }
QVariantList FeriaAccountStore::browserDownloadHistory() const { return m_state.value("browserDownloadHistory").toList(); }
QVariantMap FeriaAccountStore::browserPreferences() const {
    return validatedBrowserPreferences(m_state.value("browserPreferences").toMap());
}

bool FeriaAccountStore::setBrowserBookmark(const QString &value, const QString &title, bool saved) {
    const QString url = browserUrl(value, false);
    if (url.isEmpty()) return false;
    auto rows = browserBookmarks();
    for (qsizetype i = rows.size(); i-- > 0;)
        if (rows.at(i).toMap().value("url").toString() == url) rows.removeAt(i);
    if (saved) rows.prepend(QVariantMap{{"url", url}, {"title", title.trimmed().left(300)},
                                      {"at", QDateTime::currentMSecsSinceEpoch()}});
    while (rows.size() > 500) rows.removeLast();
    auto state = m_state; state.insert("browserBookmarks", rows); return commit(state);
}

bool FeriaAccountStore::recordBrowserVisit(const QString &sessionId, const QString &token,
                                          const QString &value, const QString &title) {
    const Visit *visit = visitFor(sessionId, token);
    const QString url = browserUrl(value, true);
    if (!visit || visit->privateBrowsing || !recording() || url.isEmpty()
        || !knownBrowserProvider(visit->context.value("pk").toString())) return false;
    auto rows = browserHistory();
    if (!rows.isEmpty() && rows.first().toMap().value("sessionId").toString() == sessionId
        && rows.first().toMap().value("url").toString() == url) rows.removeFirst();
    rows.prepend(QVariantMap{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"sessionId", sessionId}, {"url", url}, {"title", title.trimmed().left(300)},
        {"pk", visit->context.value("pk")}, {"at", QDateTime::currentMSecsSinceEpoch()}});
    while (rows.size() > 1000) rows.removeLast();
    auto state = m_state; state.insert("browserHistory", rows); return commit(state);
}

bool FeriaAccountStore::clearBrowserHistory() {
    auto state = m_state; state.insert("browserHistory", QVariantList{}); return commit(state);
}

bool FeriaAccountStore::setBrowserPreference(const QString &key, const QVariant &value) {
    if (key == "restoreTabs" || key == "freezeBackground") {
        if (value.metaType().id() != QMetaType::Bool) return false;
    } else if (key == "searchEngine") {
        if (value.metaType().id() != QMetaType::QString
            || !QStringList{"duckduckgo", "google", "bing"}.contains(value.toString())) return false;
    } else return false;
    auto preferences = browserPreferences(); preferences.insert(key, value);
    auto state = m_state; state.insert("browserPreferences", preferences); return commit(state);
}

bool FeriaAccountStore::saveBrowserTabs(const QVariantList &descriptors) {
    auto state = m_state; state.insert("browserTabs", validatedBrowserDescriptors(descriptors)); return commit(state);
}

bool FeriaAccountStore::saveBrowserRestoreOffers(const QVariantList &descriptors) {
    auto state = m_state; state.insert("browserRestoreOffers", validatedBrowserDescriptors(descriptors)); return commit(state);
}

bool FeriaAccountStore::captureBrowserRestoreOffers() {
    if (m_path.isEmpty() || (!m_error.isEmpty() && !m_writeFailed)) return false;
    if (m_restoreCaptured) return true;
    const auto tabs = browserTabs();
    const auto digest = QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument::fromVariant(tabs).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    m_restoreCaptured = true; // changed() can synchronously re-enter this method.
    if (m_state.value("browserRestoreSnapshotDigest").toString() == digest) return true;
    auto offers = browserRestoreOffers();
    for (const auto &value : tabs) {
        const auto id = value.toMap().value("restoreId").toString();
        qsizetype index = 0;
        while (index < offers.size() && offers[index].toMap().value("restoreId").toString() != id) ++index;
        if (index < offers.size()) offers[index] = value;
        else if (offers.size() < 50) offers.append(value);
    }
    auto state = m_state;
    state.insert("browserTabs", tabs); // Persist the IDs assigned to legacy descriptors on read.
    state.insert("browserRestoreOffers", offers);
    state.insert("browserRestoreSnapshotDigest", digest);
    if (commit(state)) return true;
    m_restoreCaptured = false;
    return false;
}

QString FeriaAccountStore::authorizeBrowserDownload(const QString &sessionId, const QString &visitToken) {
    const Visit *visit = visitFor(sessionId, visitToken);
    if (!visit || visit->privateBrowsing || m_path.isEmpty() || (!m_error.isEmpty() && !m_writeFailed)) return {};
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_downloadAuthorizations.insert(token, {m_path, visit->context.value("pk").toString()});
    return token;
}

bool FeriaAccountStore::cancelBrowserDownload(const QString &downloadToken) {
    return m_downloadAuthorizations.remove(downloadToken);
}

bool FeriaAccountStore::recordBrowserDownload(const QVariantMap &descriptor) {
    if (descriptor.value("privateBrowsing").toBool()) return false;
    const bool authorized = descriptor.contains("downloadToken");
    const QString downloadToken = descriptor.value("downloadToken").toString();
    QString authorizedProvider;
    if (authorized) {
        const auto grant = m_downloadAuthorizations.constFind(downloadToken);
        if (grant == m_downloadAuthorizations.cend() || grant->profilePath != m_path || grant->committing) return false;
        authorizedProvider = grant->providerId;
        if (descriptor.contains("providerId") && descriptor.value("providerId").toString() != authorizedProvider) return false;
    } else if (descriptor.contains("sessionId") || descriptor.contains("visitToken")) {
        const Visit *visit = visitFor(descriptor.value("sessionId").toString(), descriptor.value("visitToken").toString());
        if (!visit || visit->privateBrowsing) return false;
    } else return false;
    const QString url = browserUrl(descriptor.value("url").toString(), true);
    if (url.isEmpty()) return false;
    QString id = descriptor.value("id").toString().left(100);
    if (id.isEmpty()) id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVariantMap row{{"id", id}, {"url", url}, {"at", QDateTime::currentMSecsSinceEpoch()}};
    for (const auto &key : {"title", "fileName", "path", "state", "providerId", "mimeType"})
        if (descriptor.contains(key)) row.insert(key, descriptor.value(key).toString().left(2048));
    if (authorized) row.insert("providerId", authorizedProvider);
    for (const auto &key : {"receivedBytes", "totalBytes"})
        if (descriptor.contains(key)) {
            bool ok = false;
            const qint64 bytes = descriptor.value(key).toLongLong(&ok);
            if (!ok || bytes < 0) return false;
            row.insert(key, bytes);
        }
    auto rows = browserDownloadHistory();
    for (qsizetype i = rows.size(); i-- > 0;)
        if (rows.at(i).toMap().value("id").toString() == id) rows.removeAt(i);
    rows.prepend(row);
    while (rows.size() > 100) rows.removeLast();
    auto state = m_state; state.insert("browserDownloadHistory", rows);
    // changed() is synchronous; reserve the grant before commit so callbacks cannot replay it.
    if (authorized) m_downloadAuthorizations[downloadToken].committing = true;
    const bool saved = commit(state);
    if (authorized) {
        const auto grant = m_downloadAuthorizations.find(downloadToken);
        if (grant != m_downloadAuthorizations.end()) {
            if (saved) m_downloadAuthorizations.erase(grant);
            else grant->committing = false;
        }
    }
    return saved;
}

bool FeriaAccountStore::clearBrowserDownloads() {
    auto state = m_state; state.insert("browserDownloadHistory", QVariantList{}); return commit(state);
}

QVariantList FeriaAccountStore::continueItems() const {
    QVariantList result;
    QSet<QString> seen;
    auto rows = sessions();
    rows.append(m_state.value("retainedContinue").toList());
    // Retained rows can be newer than history aliases. One O(n log n) ordering
    // and a set lookup per row; never scan all history separately for each alias.
    std::stable_sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("at").toLongLong() > b.toMap().value("at").toLongLong();
    });
    for (const auto &value : rows) {
        const auto row = value.toMap();
        // Title-derived IDs in existing files refer to the same chapter. The
        // newest row wins, including completion/dismissal, without deleting history.
        const auto id = continueIdentity(row);
        if (seen.contains(id)) continue;
        seen.insert(id);
        if (!row.value("completed").toBool() && !row.value("dismissed").toBool()) result.append(row);
    }
    return result;
}

QString FeriaAccountStore::safeUrl(const QString &value) {
    QUrl url(value);
    if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http") || url.host().isEmpty()) return {};
    const auto path = url.path().toLower();
    for (const auto &part : {"oauth", "authorize", "callback", "signin", "login", "checkout", "account"})
        if (path.contains(QLatin1String(part))) return {};
    url.setUserInfo({});
    const auto fragment = url.fragment();
    url.setFragment(fragment.startsWith('/') && !fragment.contains('?') && !fragment.contains('=') ? fragment : QString());
    QUrlQuery kept;
    for (const auto &pair : QUrlQuery(url).queryItems())
        if (QStringList{"v", "id", "chapter", "episode", "page", "book", "volume", "asin", "title_no", "episode_no", "pg", "loc", "cfi"}.contains(pair.first)) kept.addQueryItem(pair.first, pair.second);
    url.setQuery(kept);
    return url.toString();
}

bool FeriaAccountStore::commit(const QVariantMap &state) {
    if (m_path.isEmpty() || (!m_error.isEmpty() && !m_writeFailed)) return false;
    auto next = state;
    next.insert("version", 1);
    QSaveFile file(m_path);
    const auto data = QJsonDocument(QJsonObject::fromVariantMap(next)).toJson(QJsonDocument::Compact);
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath()) || !file.open(QIODevice::WriteOnly)
        || file.write(data) != data.size() || !file.commit()) {
        m_writeFailed = true;
        m_error = "Could not save Feria account data. Check available disk space and retry.";
        emit changed();
        return false;
    }
    m_state = next;
    m_writeFailed = false;
    m_error.clear();
    emit changed();
    return true;
}

QString FeriaAccountStore::beginSessionVisit(const QString &sessionId, const QVariantMap &context) {
    endSessionVisit(sessionId);
    if (sessionId.isEmpty() || m_path.isEmpty() || (!m_error.isEmpty() && !m_writeFailed)
        || !PorticoAppStateBridge::currentPorticoProviderIds().contains(context.value("pk").toString())) return {};
    Visit visit;
    visit.context = context;
    visit.privateBrowsing = context.value("privateBrowsing").toBool();
    visit.mediaCaptureBlocked = context.value("resumePending").toBool();
    visit.sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    visit.token = visit.sessionId;
    visit.clock.start();
    m_visits.insert(sessionId, visit);
    return visit.token;
}
void FeriaAccountStore::endSessionVisit(const QString &sessionId) {
    m_visits.remove(sessionId);
    if (sessionId == QLatin1String("legacy")) m_legacyToken.clear();
}
void FeriaAccountStore::beginVisit(const QVariantMap &context) {
    m_legacyToken = beginSessionVisit(QStringLiteral("legacy"), context);
}
void FeriaAccountStore::endVisit() { endSessionVisit(QStringLiteral("legacy")); }
FeriaAccountStore::Visit *FeriaAccountStore::visitFor(const QString &id, const QString &token) {
    auto it = m_visits.find(id);
    return !token.isEmpty() && it != m_visits.end() && it->token == token ? &it.value() : nullptr;
}
void FeriaAccountStore::setMediaCaptureBlocked(bool blocked, const QString &token) {
    const QString expected = token.isEmpty() ? m_legacyToken : token;
    if (expected.isEmpty()) return;
    for (Visit &visit : m_visits) {
        if (visit.token != expected) continue;
        visit.mediaCaptureBlocked = blocked;
        if (blocked) visit.lastPlaying = false;
        return;
    }
}
bool FeriaAccountStore::observeSession(const QString &id, const QString &token, const QVariantMap &sample) {
    Visit *visit = visitFor(id, token);
    return visit && observeVisit(*visit, sample);
}
bool FeriaAccountStore::observe(const QVariantMap &sample) {
    Visit *visit = visitFor(QStringLiteral("legacy"), m_legacyToken);
    return visit && observeVisit(*visit, sample);
}
bool FeriaAccountStore::observeVisit(Visit &visit, const QVariantMap &sample) {
    if (visit.privateBrowsing) return false;
    if (sample.contains("visitToken") && sample.value("visitToken").toString() != visit.token) return false;
    const bool reading = sample.value("kind").toString() == "book";
    if (reading && !QStringList{"kindle", "playbooks", "mangaplus", "viz", "webtoon", "dcui", "marvel"}.contains(visit.context.value("pk").toString())) return false;
    if (!reading && (visit.mediaCaptureBlocked
        || (sample.contains("connectorOutcome") && sample.value("connectorOutcome") != "observation")
        || (sample.contains("captureState")
        && sample.value("captureState").toString() != "observation"))) return false;
    return record(visit, sample, reading);
}
bool FeriaAccountStore::saveReadingPlace(const QString &url, const QString &title) {
    Visit *visit = visitFor(QStringLiteral("legacy"), m_legacyToken);
    return visit && saveReadingPlace(*visit, url, title);
}
bool FeriaAccountStore::saveSessionReadingPlace(const QString &id, const QString &token,
                                                const QString &url, const QString &title) {
    Visit *visit = visitFor(id, token);
    return visit && saveReadingPlace(*visit, url, title);
}
bool FeriaAccountStore::saveReadingPlace(Visit &visit, const QString &url, const QString &title) {
    QVariantMap sample{{"href", url}, {"title", title}};
    auto rows = sessions();
    rows.append(m_state.value("retainedContinue").toList());
    for (const auto &value : rows) {
        const auto row = value.toMap();
        if (row.value("pk") == visit.context.value("pk") && row.value("url") == safeUrl(url)
            && row.value("kind") == "book") {
            sample.insert("locator", row.value("locator"));
            break;
        }
    }
    return record(visit, sample, true);
}
bool FeriaAccountStore::record(Visit &visit, const QVariantMap &sample, bool reading) {
    const double elapsed = visit.clock.isValid() ? visit.clock.restart() / 1000.0 : 0;
    if (visit.privateBrowsing || !recording() || visit.context.isEmpty()) { visit.lastPlaying = false; return false; }
    auto url = safeUrl(sample.value("href").toString());
    const auto pk = visit.context.value("pk").toString();
    const auto mediaId = reading ? QString() : sample.value("providerMediaId").toString();
    if (!mediaId.isEmpty()) {
        if (pk != "ytmusic" || mediaId != youtubeMusicId(sample.value("href").toString())
            || (!sample.value("identityProvider").toString().isEmpty()
                && sample.value("identityProvider").toString() != pk)) return false;
        url = "https://music.youtube.com/watch?v=" + mediaId;
    }
    auto locator = sample.value("locator").toMap();
    if (reading && locator.isEmpty()) locator.insert("type", "url");
    const bool scroll = reading && locator.value("type") == "scroll";
    const bool mangaPage = reading && locator.value("type") == "mangaplus-page";
    const double fraction = locator.value("fraction").toDouble();
    if (reading && (locator.value("type") != "url" && !scroll && !mangaPage)) return false;
    if (scroll && (!std::isfinite(fraction) || fraction < 0 || fraction > 1
        || locator.value("selector").toString().size() > 512)) return false;
    double readingFraction = scroll ? fraction : 0;
    if (mangaPage) {
        bool pageOk = false, offsetOk = false, totalOk = false;
        const double page = locator.value("page").toDouble(&pageOk);
        const double offset = locator.value("offset").toDouble(&offsetOk);
        const double total = locator.value("total").toDouble(&totalOk);
        const QUrl readerUrl(url);
        if (visit.context.value("pk") != "mangaplus" || readerUrl.scheme() != "https"
            || readerUrl.host() != "mangaplus.shueisha.co.jp"
            || !QRegularExpression("^/viewer/[0-9]+$").match(readerUrl.path()).hasMatch()
            || !pageOk || !offsetOk || !totalOk || !std::isfinite(page) || !std::isfinite(offset)
            || !std::isfinite(total) || page < 0 || page != std::floor(page) || page >= total
            || total < 1 || total > 10000 || total != std::floor(total) || offset < 0 || offset > 1) return false;
        readingFraction = (page + offset) / total;
        locator = {{"type", "mangaplus-page"}, {"page", int(page)}, {"offset", offset}, {"total", int(total)}};
    } else if (reading) {
        locator = scroll ? QVariantMap{{"type", "scroll"}, {"fraction", fraction}, {"selector", locator.value("selector").toString()}} : QVariantMap{{"type", "url"}};
    }
    const double position = reading ? readingFraction * 100 : sample.value("position").toDouble();
    const double duration = reading ? (scroll || mangaPage ? 100 : 0) : sample.value("duration").toDouble();
    const bool playing = !sample.value("paused", true).toBool() && !sample.value("ended").toBool();
    if (url.isEmpty() || (!reading && (sample.value("ad").toBool() || !std::isfinite(position)
        || !std::isfinite(duration) || duration < 30 || position <= 0 || position > duration))) { visit.lastPlaying = false; return false; }
    QString title = sample.value("title").toString().trimmed().left(300);
    if (title.isEmpty()) title = visit.context.value("title").toString();
    if (title.isEmpty()) title = QUrl(url).host();
    const auto identity = reading ? continueIdentity({{"kind", "book"}, {"pk", pk}, {"url", url}})
                                  : !mediaId.isEmpty() ? qualifiedIdentity(pk, mediaId) : pk + "\n" + url + "\n" + title;
    const auto id = QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
    double active = 0;
    const double delta = position - visit.lastPosition;
    const double rate = sample.value("rate", 1).toDouble();
    if (!reading && visit.lastPlaying && visit.lastItem == id && elapsed > 0 && elapsed <= 6
        && delta > 0 && std::isfinite(rate) && rate > 0 && rate <= 16 && delta <= elapsed * rate + 2)
        active = std::min(elapsed, delta / rate);
    if (reading && playing && visit.lastPlaying && visit.lastItem == id && elapsed > 0 && elapsed <= 6) active = elapsed;
    visit.lastItem = id; visit.lastPosition = position; visit.lastPlaying = playing;
    auto rows = sessions();
    QVariantMap row;
    for (qsizetype i = 0; i < rows.size(); ++i) {
        auto candidate = rows[i].toMap();
        if (candidate.value("sessionId") == visit.sessionId && candidate.value("id") == id
            && QDateTime::fromMSecsSinceEpoch(candidate.value("at").toLongLong()).date() == QDate::currentDate()) {
            row = candidate; rows.removeAt(i); break;
        }
    }
    if (row.isEmpty() && !reading && !playing) return false;
    row.insert("id", id); row.insert("sessionId", visit.sessionId); row.insert("pk", pk);
    row.insert("title", title); row.insert("url", url);
    if (!mediaId.isEmpty()) {
        row.insert("identityVersion", 1);
        row.insert("identityNamespace", "youtube-video");
        row.insert("providerMediaId", mediaId);
        row.insert("artist", sample.value("artist").toString().left(300));
        row.insert("album", sample.value("album").toString().left(300));
        row.insert("artwork", safeUrl(sample.value("artwork").toString()));
    }
    row.insert("kind", reading ? "book" : sample.value("kind", "video").toString());
    row.insert("cover", visit.context.value("cover"));
    row.insert("at", QDateTime::currentMSecsSinceEpoch());
    row.insert("mins", row.value("mins").toDouble() + active / 60.0);
    row.insert("position", std::min(position, duration));
    if (reading) row.insert("locator", locator);
    row.insert("duration", duration);
    row.insert("completed", duration > 0 && (sample.value("ended").toBool() || position >= duration * 0.98));
    row.insert("sample", false); row.insert("dismissed", false);
    rows.prepend(row);
    while (rows.size() > 5000) rows.removeLast();
    auto state = m_state; state.insert("sessions", rows);
    return commit(state);
}
bool FeriaAccountStore::setRecording(bool enabled) {
    for (Visit &visit : m_visits) visit.lastPlaying = false;
    auto state = m_state; state.insert("recording", enabled); return commit(state);
}
bool FeriaAccountStore::clearHistory() {
    m_visits.clear();
    m_legacyToken.clear();
    auto state = m_state;
    state.insert("sessions", QVariantList{});
    state.remove("retainedContinue");
    return commit(state);
}
bool FeriaAccountStore::clearActivity() {
    auto state = m_state;
    auto retained = continueItems();
    for (auto &value : retained) {
        auto row = value.toMap();
        row.remove("mins");
        row.remove("sessionId");
        value = row;
    }
    state.insert("retainedContinue", retained);
    state.insert("sessions", QVariantList{});
    if (!commit(state)) return false;
    for (Visit &visit : m_visits) {
        visit.lastPlaying = false;
        visit.lastItem.clear();
        visit.sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (visit.clock.isValid()) visit.clock.restart();
    }
    return true;
}
bool FeriaAccountStore::dismissContinue(const QString &id) {
    auto rows = sessions();
    auto retained = m_state.value("retainedContinue").toList();
    auto all = rows; all.append(retained);
    QString identity;
    for (const auto &value : all) {
        const auto row = value.toMap();
        if (row.value("id") == id) { identity = continueIdentity(row); break; }
    }
    auto dismiss = [&](QVariantList &items) {
        for (auto &value : items) {
            auto row = value.toMap();
            if (!identity.isEmpty() && continueIdentity(row) == identity) { row.insert("dismissed", true); value = row; }
        }
    };
    dismiss(rows); dismiss(retained);
    auto state = m_state; state.insert("sessions", rows); state.insert("retainedContinue", retained); return commit(state);
}
