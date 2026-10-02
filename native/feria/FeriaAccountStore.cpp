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
#include <cmath>

FeriaAccountStore::FeriaAccountStore(QObject *parent) : QObject(parent) {}

void FeriaAccountStore::setStoragePath(const QString &path) {
    endVisit();
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
            else m_state = doc.object().toVariantMap();
        }
    }
    emit changed();
    emit profileChanged();
}

QVariantList FeriaAccountStore::sessions() const { return m_state.value("sessions").toList(); }
bool FeriaAccountStore::recording() const { return m_state.value("recording", true).toBool() && !m_path.isEmpty() && m_error.isEmpty(); }

QVariantList FeriaAccountStore::continueItems() const {
    QVariantList result;
    QSet<QString> seen;
    for (const auto &value : sessions()) {
        const auto row = value.toMap();
        const auto id = row.value("id").toString();
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
    if (m_path.isEmpty() || !m_error.isEmpty()) return false;
    auto next = state;
    next.insert("version", 1);
    QSaveFile file(m_path);
    const auto data = QJsonDocument(QJsonObject::fromVariantMap(next)).toJson(QJsonDocument::Compact);
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath()) || !file.open(QIODevice::WriteOnly)
        || file.write(data) != data.size() || !file.commit()) {
        m_error = "Could not save Feria account data. Check available disk space and restart Colosseum.";
        emit changed();
        return false;
    }
    m_state = next;
    emit changed();
    return true;
}

void FeriaAccountStore::beginVisit(const QVariantMap &context) {
    endVisit();
    if (!PorticoAppStateBridge::currentPorticoProviderIds().contains(context.value("pk").toString())) return;
    m_context = context;
    m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_clock.start();
}
void FeriaAccountStore::endVisit() {
    m_context.clear(); m_sessionId.clear(); m_lastItem.clear(); m_lastPlaying = false; m_clock.invalidate();
}
bool FeriaAccountStore::observe(const QVariantMap &sample) {
    const bool reading = sample.value("kind").toString() == "book";
    if (reading && !QStringList{"kindle", "playbooks", "mangaplus", "viz", "webtoon", "dcui", "marvel"}.contains(m_context.value("pk").toString())) return false;
    return record(sample, reading);
}
bool FeriaAccountStore::saveReadingPlace(const QString &url, const QString &title) {
    QVariantMap sample{{"href", url}, {"title", title}};
    for (const auto &value : sessions()) {
        const auto row = value.toMap();
        if (row.value("pk") == m_context.value("pk") && row.value("url") == safeUrl(url)
            && row.value("kind") == "book") {
            sample.insert("locator", row.value("locator"));
            break;
        }
    }
    return record(sample, true);
}
bool FeriaAccountStore::record(const QVariantMap &sample, bool reading) {
    const double elapsed = m_clock.isValid() ? m_clock.restart() / 1000.0 : 0;
    if (!recording() || m_context.isEmpty()) { m_lastPlaying = false; return false; }
    const auto url = safeUrl(sample.value("href").toString());
    auto locator = sample.value("locator").toMap();
    if (reading && locator.isEmpty()) locator.insert("type", "url");
    const bool scroll = reading && locator.value("type") == "scroll";
    const double fraction = locator.value("fraction").toDouble();
    if (reading && (locator.value("type") != "url" && !scroll)) return false;
    if (scroll && (!std::isfinite(fraction) || fraction < 0 || fraction > 1
        || locator.value("selector").toString().size() > 512)) return false;
    if (reading) locator = scroll ? QVariantMap{{"type", "scroll"}, {"fraction", fraction}, {"selector", locator.value("selector").toString()}} : QVariantMap{{"type", "url"}};
    const double position = reading ? (scroll ? fraction * 100 : 0) : sample.value("position").toDouble();
    const double duration = reading ? (scroll ? 100 : 0) : sample.value("duration").toDouble();
    const bool playing = !sample.value("paused", true).toBool() && !sample.value("ended").toBool();
    if (url.isEmpty() || (!reading && (sample.value("ad").toBool() || !std::isfinite(position)
        || !std::isfinite(duration) || duration < 30 || position <= 0))) { m_lastPlaying = false; return false; }
    QString title = sample.value("title").toString().trimmed().left(300);
    if (title.isEmpty()) title = m_context.value("title").toString();
    if (title.isEmpty()) title = QUrl(url).host();
    const auto pk = m_context.value("pk").toString();
    const auto id = QString::fromLatin1(QCryptographicHash::hash((pk + "\n" + url + "\n" + title).toUtf8(), QCryptographicHash::Sha256).toHex());
    double active = 0;
    const double delta = position - m_lastPosition;
    const double rate = sample.value("rate", 1).toDouble();
    if (!reading && m_lastPlaying && m_lastItem == id && elapsed > 0 && elapsed <= 6
        && delta > 0 && std::isfinite(rate) && rate > 0 && rate <= 16 && delta <= elapsed * rate + 2)
        active = std::min(elapsed, delta / rate);
    if (reading && playing && m_lastPlaying && m_lastItem == id && elapsed > 0 && elapsed <= 6) active = elapsed;
    m_lastItem = id; m_lastPosition = position; m_lastPlaying = playing;
    auto rows = sessions();
    QVariantMap row;
    for (qsizetype i = 0; i < rows.size(); ++i) {
        auto candidate = rows[i].toMap();
        if (candidate.value("sessionId") == m_sessionId && candidate.value("id") == id
            && QDateTime::fromMSecsSinceEpoch(candidate.value("at").toLongLong()).date() == QDate::currentDate()) {
            row = candidate; rows.removeAt(i); break;
        }
    }
    if (row.isEmpty() && !reading && !playing) return false;
    row.insert("id", id); row.insert("sessionId", m_sessionId); row.insert("pk", pk);
    row.insert("title", title); row.insert("url", url);
    row.insert("kind", reading ? "book" : sample.value("kind", "video").toString());
    row.insert("cover", m_context.value("cover"));
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
    m_lastPlaying = false;
    auto state = m_state; state.insert("recording", enabled); return commit(state);
}
bool FeriaAccountStore::clearHistory() {
    endVisit();
    auto state = m_state; state.insert("sessions", QVariantList{}); return commit(state);
}
bool FeriaAccountStore::dismissContinue(const QString &id) {
    auto rows = sessions();
    for (auto &value : rows) { auto row = value.toMap(); if (row.value("id") == id) { row.insert("dismissed", true); value = row; } }
    auto state = m_state; state.insert("sessions", rows); return commit(state);
}
