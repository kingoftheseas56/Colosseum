#include "FeriaBrowserPolicy.h"
#include <QRegularExpression>
#include <QUrlQuery>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <algorithm>
#ifdef FERIA_HAS_WEBVIEW2
#include "provider-host/FeriaHostItem.h"
#include <qqml.h>
#endif

namespace {
bool invalidAddressInput(const QString &text) {
    if (text.size() > 8192) return true;
    for (const QChar c : text)
        if (c.category() == QChar::Other_Control) return true;
    return false;
}
bool hostWithPort(const QString &text) {
    static const QRegularExpression hostPort(
        QStringLiteral(R"(^(?:localhost|[^\s/:]+\.[^\s/:]+|\[[0-9a-fA-F:]+\]):[0-9]+(?:[/?#]|$))"),
        QRegularExpression::CaseInsensitiveOption);
    return hostPort.match(text).hasMatch();
}
}

QRect FeriaBrowserPolicy::windowWorkArea(QObject *object) const
{
    const auto *window = qobject_cast<QWindow *>(object);
    const QScreen *screen = window ? window->screen() : QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry() : QRect{};
}

QRect FeriaBrowserPolicy::boundedWindowGeometry(const QRect &requested, QObject *object) const
{
    const auto *window = qobject_cast<QWindow *>(object);
    return clampWindowGeometry(requested, windowWorkArea(object),
                               window ? window->frameMargins() : QMargins{});
}

QRect FeriaBrowserPolicy::clampWindowGeometry(const QRect &requested, const QRect &area,
                                             const QMargins &frame) const
{
    if (area.isEmpty()) return {};
    // Wide arithmetic prevents overflow for hostile window.open feature values.
    const qint64 left = std::max(0, frame.left()), top = std::max(0, frame.top());
    const qint64 right = std::max(0, frame.right()), bottom = std::max(0, frame.bottom());
    const qint64 availableWidth = qint64(area.width()) - left - right;
    const qint64 availableHeight = qint64(area.height()) - top - bottom;
    if (availableWidth < 1 || availableHeight < 1) return {};
    const qint64 width = std::clamp<qint64>(requested.width() > 0 ? requested.width() : 960,
                                          std::min<qint64>(320, availableWidth), availableWidth);
    const qint64 height = std::clamp<qint64>(requested.height() > 0 ? requested.height() : 720,
                                           std::min<qint64>(240, availableHeight), availableHeight);
    const qint64 x = std::clamp<qint64>(requested.x(), qint64(area.x()) + left,
                                       qint64(area.x()) + area.width() - right - width);
    const qint64 y = std::clamp<qint64>(requested.y(), qint64(area.y()) + top,
                                       qint64(area.y()) + area.height() - bottom - height);
    return QRect(int(x), int(y), int(width), int(height));
}

QUrl FeriaBrowserPolicy::navigationUrl(const QString &text) const
{
    if (invalidAddressInput(text)) return {};
    const QString input = text.trimmed();
    if (input.isEmpty() || input.startsWith('/') || input.startsWith('\\')) return {};
    // Qt parsing, with no QFileInfo/fromLocalFile shortcut from the examples.
    // Host:port must be classified before a generic scheme (localhost:8123).
    const QUrl url = QUrl::fromUserInput(hostWithPort(input)
        ? QStringLiteral("http://") + input : input);
    const QString scheme = url.scheme().toLower();
    return (scheme == "http" || scheme == "https") && allows(url)
        && url.userInfo().isEmpty() ? url : QUrl{};
}

QUrl FeriaBrowserPolicy::omniboxUrl(const QString &text, const QString &searchEngine) const
{
    if (invalidAddressInput(text)) return {};
    const QString input = text.trimmed();
    if (input.isEmpty() || input.startsWith('/') || input.startsWith('\\')) return {};
    static const QRegularExpression scheme(QStringLiteral("^[a-zA-Z][a-zA-Z0-9+.-]*:"));
    if (hostWithPort(input) || scheme.match(input).hasMatch()) return navigationUrl(input);
    static const QRegularExpression localhost(QStringLiteral("^localhost(?=$|[/?#])"),
        QRegularExpression::CaseInsensitiveOption);
    if (!input.contains(QRegularExpression(QStringLiteral("\\s")))
        && (input.contains('.') || localhost.match(input).hasMatch()
            || input.startsWith('['))) {
        const QUrl address = navigationUrl(input);
        if (address.isValid() && !address.isEmpty()) return address;
    }
    const QString endpoint = searchEngine == QStringLiteral("google") ? QStringLiteral("https://www.google.com/search")
        : searchEngine == QStringLiteral("bing") ? QStringLiteral("https://www.bing.com/search")
        : QStringLiteral("https://duckduckgo.com/");
    QUrl url(endpoint);
    // Encode the complete value, including literal + (form-query servers
    // otherwise interpret it as space). QUrlQuery alone preserves +.
    url.setQuery(QStringLiteral("q=") + QString::fromLatin1(QUrl::toPercentEncoding(input)));
    return url;
}

bool FeriaBrowserPolicy::webView2Available() const
{
#ifdef FERIA_HAS_WEBVIEW2
    return true;
#else
    return false;
#endif
}

void FeriaBrowserPolicy::registerTypes()
{
#ifdef FERIA_HAS_WEBVIEW2
    qmlRegisterType<FeriaHostItem>("Colosseum.FeriaHost", 1, 0, "FeriaHostItem");
#endif
}

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
#include <memory>

namespace {
const QMap<QString, QStringList> sessionDomains{
    {"netflix", {"netflix.com"}}, {"prime", {"primevideo.com", "amazon.com", "amazon.in"}},
    {"hbomax", {"hbomax.com", "max.com"}}, {"disney", {"disneyplus.com", "disney.com", "bamgrid.com"}},
    {"hotstar", {"hotstar.com", "jiohotstar.com"}},
    {"appletv", {"apple.com"}}, {"applemusic", {"apple.com"}},
    {"crunchyroll", {"crunchyroll.com"}}, {"youtube", {"youtube.com", "google.com"}},
    {"ytmusic", {"youtube.com", "google.com"}}, {"hulu", {"hulu.com"}}, {"mubi", {"mubi.com"}},
    {"spotify", {"spotify.com"}}, {"kindle", {"amazon.com", "amazon.in", "primevideo.com"}},
    {"playbooks", {"google.com", "youtube.com"}}, {"mangaplus", {"mangaplus.shueisha.co.jp"}},
    {"viz", {"viz.com"}}, {"webtoon", {"webtoons.com"}},
    {"dcui", {"dcuniverseinfinite.com"}}, {"marvel", {"marvel.com"}}
};
QVariantMap originHistory(const QString &root) {
    QFile file(root + "/origins.json");
    if (root.isEmpty() || !file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
}
}


void FeriaBrowserPolicy::rememberOrigin(const QString &provider, const QUrl &url) {
    if (m_storageRoot.isEmpty() || !sessionDomains.contains(provider) || !allows(url)
        || (url.scheme() != "http" && url.scheme() != "https")) return;
    const QString origin = url.adjusted(QUrl::RemoveUserInfo | QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment).toString();
    auto history = originHistory(m_storageRoot);
    auto origins = history.value(provider).toStringList();
    if (origins.contains(origin)) return;
    origins.append(origin);
    while (origins.size() > 64) origins.removeFirst();
    history.insert(provider, origins);
    QDir().mkpath(m_storageRoot);
    QSaveFile file(m_storageRoot + "/origins.json");
    const QByteArray data = QJsonDocument(QJsonObject::fromVariantMap(history)).toJson(QJsonDocument::Compact);
    if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size()) file.commit();
}

QVariantMap FeriaBrowserPolicy::sessionScope(const QString &provider) const {
    if (m_storageRoot.isEmpty() || !sessionDomains.contains(provider)) return {};
    QStringList domains = sessionDomains.value(provider), origins;
    for (const auto &domain : domains) {
        origins << "https://" + domain << "https://www." + domain;
    }
    // Common sign-in and player origins, including shared provider SSO.
    for (const QString &origin : {"https://accounts.google.com", "https://play.google.com", "https://music.youtube.com",
         "https://read.amazon.com", "https://tv.apple.com", "https://music.apple.com", "https://appleid.apple.com",
         "https://idmsa.apple.com", "https://open.spotify.com", "https://accounts.spotify.com", "https://play.hbomax.com", "https://play.max.com", "https://read.marvel.com"})
        if (cookieInScope(QUrl(origin).host(), domains)) origins.append(origin);
    const auto history = originHistory(m_storageRoot);
    for (const auto &origin : history.value(provider).toStringList()) {
        const QUrl url(origin);
        if (url.scheme() != "http" && url.scheme() != "https") continue;
        origins.append(origin); domains.append(url.host());
    }
    // Clear visited subdomains of the same SSO family as well.
    for (const auto &value : history)
        for (const auto &origin : value.toStringList())
            if (cookieInScope(QUrl(origin).host(), domains)) origins.append(origin);
    domains.removeDuplicates(); origins.removeDuplicates();
    return {{"domains", domains}, {"origins", origins}};
}

int FeriaBrowserPolicy::clearQtCookies(const QString &profilePath, const QStringList &domains) {
    const int request = ++m_cookieRequest;
    // Called before a Qt browser context is created, after the provider view has
    // closed. Qt 6.11's public enumeration drops its callback, so remove only
    // matching host records from this app-owned, closed profile transactionally.
    // No cookie values or encrypted credentials are selected or exposed.
    auto *retry = new QTimer(this);
    retry->setInterval(150);
    auto attempts = std::make_shared<int>(0);
    const quint64 generation = m_storageGeneration;
    const auto attempt = [this, retry, attempts, request, profilePath, domains, generation] {
        if (generation != m_storageGeneration || profilePath != m_storageRoot) {
            retry->stop(); retry->deleteLater(); emit cookiesCleared(request, false); return;
        }
        // A retained WebEngine profile owns a live cookie database. Wait for
        // QObject destruction, not merely a hidden or parked browser surface.
        if (!m_runtimeProfiles.isEmpty() || !m_runtimeViews.isEmpty()) {
            if (++*attempts >= 60) {
                retry->stop(); retry->deleteLater(); emit cookiesCleared(request, false);
            }
            return;
        }
        bool ok = !profilePath.isEmpty() && !domains.isEmpty();
        QString cookieFile;
        for (const auto &relative : {"/qtwebengine/Cookies", "/qtwebengine/Network/Cookies"}) {
            const auto candidate = profilePath + relative;
            if (QFile::exists(candidate)) { cookieFile = candidate; break; }
        }
        if (ok && !cookieFile.isEmpty()) {
            const QString connection = "feria-signout-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
            {
                auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
                db.setDatabaseName(cookieFile); db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=100");
                ok = db.open() && db.transaction();
                if (ok) {
                    QStringList hosts;
                    {
                        QSqlQuery query(db);
                        ok = query.exec("SELECT DISTINCT host_key FROM cookies");
                        while (ok && query.next())
                            if (cookieInScope(query.value(0).toString(), domains)) hosts.append(query.value(0).toString());
                    }
                    if (ok) {
                        QSqlQuery query(db);
                        ok = query.prepare("DELETE FROM cookies WHERE host_key = :host");
                        for (const auto &host : hosts) {
                            if (!ok) break;
                            query.bindValue(":host", host); ok = query.exec();
                        }
                    }
                    if (ok) ok = db.commit();
                    else db.rollback();
                }
                db.close();
            }
            QSqlDatabase::removeDatabase(connection);
        }
        if (ok || ++*attempts >= 60 || profilePath.isEmpty() || domains.isEmpty()) {
            retry->stop(); retry->deleteLater(); emit cookiesCleared(request, ok);
        }
    };
    connect(retry, &QTimer::timeout, retry, attempt);
    retry->start();
    return request;
}


#include "FeriaAccountStore.h"
#include <QDesktopServices>
#include <QFileInfo>
#include <QJSValue>
#if defined(Q_OS_UNIX)
#include <sys/stat.h>
#elif defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
bool safeDownloadText(const QString &value) {
    if (value.isEmpty() || value.size() > 4096) return false;
    for (const auto c : value)
        if (c.category() == QChar::Other_Control) return false;
    return true;
}
bool safeDownloadLeaf(const QString &value) {
    if (!safeDownloadText(value) || value.size() > 255 || value == "." || value == ".."
        || value.endsWith('.') || value.endsWith(' ') || value.contains(QRegularExpression("[\\\\/:<>\"|?*]"))) return false;
    static const QRegularExpression reserved("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)",
                                              QRegularExpression::CaseInsensitiveOption);
    return !reserved.match(value).hasMatch();
}
bool safeDownloadPath(const QString &path) {
    return safeDownloadText(path) && QDir::isAbsolutePath(path)
        && !path.startsWith("//") && !path.startsWith('\\') && QDir::cleanPath(path) == path;
}
// Native identifiers detect same-path replacement even when size and bytes match.
// The final shell launch is path based; this cannot make the OS launch atomic
// with validation against a concurrent filesystem actor.
QVariantMap downloadNativeIdentity(const QString &path, bool directory) {
#if defined(Q_OS_UNIX)
    struct stat value {};
    const auto encoded = QFile::encodeName(path);
    if (::lstat(encoded.constData(), &value) != 0
        || (directory ? !S_ISDIR(value.st_mode) : !S_ISREG(value.st_mode))) return {};
    QVariantMap result{{"nativeId", QString::number(qulonglong(value.st_dev)) + ':' + QString::number(qulonglong(value.st_ino))}};
    if (!directory) {
        result.insert("size", QString::number(qlonglong(value.st_size)));
#if defined(Q_OS_DARWIN)
        result.insert("modifiedNs", QString::number(qlonglong(value.st_mtimespec.tv_sec)) + ':' + QString::number(qlonglong(value.st_mtimespec.tv_nsec)));
        result.insert("changedNs", QString::number(qlonglong(value.st_ctimespec.tv_sec)) + ':' + QString::number(qlonglong(value.st_ctimespec.tv_nsec)));
#else
        result.insert("modifiedNs", QString::number(qlonglong(value.st_mtim.tv_sec)) + ':' + QString::number(qlonglong(value.st_mtim.tv_nsec)));
        result.insert("changedNs", QString::number(qlonglong(value.st_ctim.tv_sec)) + ':' + QString::number(qlonglong(value.st_ctim.tv_nsec)));
#endif
    }
    return result;
#elif defined(Q_OS_WIN)
    const auto native = QDir::toNativeSeparators(path);
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    BY_HANDLE_FILE_INFORMATION value {};
    const bool valid = GetFileInformationByHandle(handle, &value)
        && !(value.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        && bool(value.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory;
    CloseHandle(handle);
    if (!valid) return {};
    const auto wide = [](DWORD high, DWORD low) { return (quint64(high) << 32) | low; };
    QVariantMap result{{"nativeId", QString::number(value.dwVolumeSerialNumber) + ':'
        + QString::number(wide(value.nFileIndexHigh, value.nFileIndexLow))}};
    if (!directory) {
        result.insert("size", QString::number(wide(value.nFileSizeHigh, value.nFileSizeLow)));
        result.insert("modifiedNs", QString::number(wide(value.ftLastWriteTime.dwHighDateTime, value.ftLastWriteTime.dwLowDateTime)));
        result.insert("changedNs", QString::number(wide(value.ftCreationTime.dwHighDateTime, value.ftCreationTime.dwLowDateTime)));
    }
    return result;
#else
    Q_UNUSED(path); Q_UNUSED(directory);
    return {}; // Do not replace native identity with a size-only guess.
#endif
}
bool identityShape(const QVariantMap &value, bool directory) {
    const QStringList keys = directory ? QStringList{"nativeId"}
                                      : QStringList{"nativeId", "size", "modifiedNs", "changedNs"};
    if (value.size() != keys.size()) return false;
    for (const auto &key : keys) {
        const auto text = value.value(key);
        if (text.metaType().id() != QMetaType::QString || text.toString().isEmpty() || text.toString().size() > 100) return false;
    }
    if (!directory) {
        bool ok = false;
        const auto size = value.value("size").toString().toLongLong(&ok);
        if (!ok || size < 0) return false;
    }
    return true;
}
QVariantMap fileOutcome(const QString &status, const QString &reason = {}) {
    return {{"status", status}, {"reason", reason}};
}
}

QVariantMap FeriaBrowserPolicy::admitDownloadDestination(const QString &directory, const QString &fileName) const {
    const auto normalized = QDir::fromNativeSeparators(directory);
    if (!safeDownloadLeaf(fileName) || !safeDownloadText(normalized) || !QDir::isAbsolutePath(normalized)
        || normalized.startsWith("//") || normalized.startsWith('\\')
        || normalized.split('/').contains("..")) return fileOutcome("rejected", "unsafe-destination");
    // Preparing the configured default destination is part of admission, not a
    // grant to overwrite an existing file or to persist private metadata.
    if (!QDir().mkpath(normalized)) return fileOutcome("rejected", "destination-unavailable");
    const auto canonical = QFileInfo(normalized).canonicalFilePath();
    const auto directoryIdentity = downloadNativeIdentity(canonical, true);
    const auto path = QDir(canonical).filePath(fileName);
    const QFileInfo target(path);
    if (!safeDownloadPath(canonical) || directoryIdentity.isEmpty() || !safeDownloadPath(path)
        || target.exists() || target.isSymLink()) return fileOutcome("rejected", "destination-unavailable-or-existing");
    return {{"status", "admitted"}, {"directory", canonical}, {"path", path},
            {"fileName", fileName}, {"directoryIdentity", directoryIdentity}};
}

bool FeriaBrowserPolicy::validDownloadFileSnapshot(const QVariantMap &snapshot) {
    if (snapshot.size() != 6 || snapshot.value("state") != "completed") return false;
    for (const auto &key : {"path", "directory", "fileName"})
        if (snapshot.value(key).metaType().id() != QMetaType::QString) return false;
    const auto path = snapshot.value("path").toString(), directory = snapshot.value("directory").toString();
    const auto fileName = snapshot.value("fileName").toString();
    return safeDownloadPath(path) && safeDownloadPath(directory) && safeDownloadLeaf(fileName)
        && path == QDir(directory).filePath(fileName)
        && snapshot.value("directoryIdentity").metaType().id() == QMetaType::QVariantMap
        && snapshot.value("identity").metaType().id() == QMetaType::QVariantMap
        && identityShape(snapshot.value("directoryIdentity").toMap(), true)
        && identityShape(snapshot.value("identity").toMap(), false);
}

QVariantMap FeriaBrowserPolicy::completedDownloadFile(const QVariantMap &admission) const {
    if (admission.value("status") != "admitted") return {};
    const auto path = admission.value("path").toString();
    QVariantMap snapshot{{"state", "completed"}, {"path", path},
        {"directory", admission.value("directory")}, {"fileName", admission.value("fileName")},
        {"directoryIdentity", admission.value("directoryIdentity")}, {"identity", downloadNativeIdentity(path, false)}};
    return checkDownloadSnapshot(snapshot).value("status") == "available" ? snapshot : QVariantMap{};
}

QVariantMap FeriaBrowserPolicy::checkDownloadSnapshot(const QVariantMap &snapshot) {
    if (!validDownloadFileSnapshot(snapshot)) return fileOutcome("rejected", "not-an-admitted-completed-file");
    const auto path = snapshot.value("path").toString(), directory = snapshot.value("directory").toString();
    const QFileInfo currentFile(path), currentDirectory(directory);
    if (!currentFile.exists() || !currentDirectory.exists()) return fileOutcome("unavailable", "file-or-directory-missing");
    if (currentFile.isSymLink() || !currentFile.isFile() || currentFile.canonicalFilePath() != path
        || currentDirectory.canonicalFilePath() != directory
        || downloadNativeIdentity(directory, true) != snapshot.value("directoryIdentity").toMap()
        || downloadNativeIdentity(path, false) != snapshot.value("identity").toMap())
        return fileOutcome("replaced", "file-or-directory-identity-changed");
    return fileOutcome("available");
}

void FeriaBrowserPolicy::setDownloadAccountStore(FeriaAccountStore *store) {
    m_downloadAccountStore = store;
    m_downloadAccountGeneration = store ? store->downloadScopeGeneration() : 0;
    m_downloadAccountStorageGeneration = m_storageGeneration;
    emit downloadAccountScopeChanged();
}

bool FeriaBrowserPolicy::currentDownloadAccount(const FeriaAccountStore *store) const {
    return store && m_downloadAccountStore.data() == store && store->hasDownloadScope()
        && store->downloadScopeGeneration() == m_downloadAccountGeneration
        && m_storageGeneration == m_downloadAccountStorageGeneration;
}

bool FeriaBrowserPolicy::registerDownloadOwner(QObject *owner, QObject *accountStore) {
    const auto store = qobject_cast<FeriaAccountStore *>(accountStore);
    if (!owner || !currentDownloadAccount(store)) return false;
    const bool fresh = !m_downloadOwners.contains(owner);
    m_downloadOwners.insert(owner, {store, store->downloadScopeGeneration(), m_storageGeneration});
    if (fresh) connect(owner, &QObject::destroyed, this, [this, owner] { m_downloadOwners.remove(owner); });
    return true;
}

bool FeriaBrowserPolicy::unregisterDownloadOwner(QObject *owner) {
    return m_downloadOwners.remove(owner) != 0;
}

QVariantMap FeriaBrowserPolicy::downloadOwnerScope(QObject *owner) const {
    const auto found = m_downloadOwners.constFind(owner);
    if (found == m_downloadOwners.cend()) return {};
    const auto store = qobject_cast<FeriaAccountStore *>(found->accountStore.data());
    if (!currentDownloadAccount(store) || store->downloadScopeGeneration() != found->accountGeneration
        || m_storageGeneration != found->storageGeneration) return {};
    return {{"accountGeneration", QString::number(found->accountGeneration)},
            {"storageGeneration", QString::number(found->storageGeneration)}};
}

QVariantMap FeriaBrowserPolicy::resolveDownloadSnapshot(QObject *owner, const QString &id) const {
    if (!owner || id.isEmpty() || id.size() > 100) return {};
    if (const auto store = qobject_cast<FeriaAccountStore *>(owner))
        return currentDownloadAccount(store) ? store->resolveDownloadFile(id) : QVariantMap{};
    const auto found = m_downloadOwners.constFind(owner);
    if (found == m_downloadOwners.cend()) return {};
    const auto scope = *found;
    const auto current = [&] {
        const auto store = qobject_cast<FeriaAccountStore *>(scope.accountStore.data());
        return currentDownloadAccount(store) && store->downloadScopeGeneration() == scope.accountGeneration
            && m_storageGeneration == scope.storageGeneration && m_downloadOwners.contains(owner);
    };
    if (!current()) return {};
    QPointer<QObject> lifetime(owner);
    QVariant value;
    if (!QMetaObject::invokeMethod(owner, "resolveDownloadFile", Q_RETURN_ARG(QVariant, value),
                                  Q_ARG(QVariant, QVariant(id))) || !lifetime || !current()) return {};
    if (value.metaType().id() == qMetaTypeId<QJSValue>()) value = value.value<QJSValue>().toVariant();
    return value.toMap();
}

QVariantMap FeriaBrowserPolicy::downloadFileStatus(QObject *owner, const QString &id) const {
    const auto snapshot = resolveDownloadSnapshot(owner, id);
    return snapshot.isEmpty() ? fileOutcome("invalidated", "owner-or-file-id-retired") : checkDownloadSnapshot(snapshot);
}

QVariantMap FeriaBrowserPolicy::launchDownloadedFile(QObject *owner, const QString &id, bool folder) const {
    const auto snapshot = resolveDownloadSnapshot(owner, id);
    if (snapshot.isEmpty()) return fileOutcome("invalidated", "owner-or-file-id-retired");
    const auto status = checkDownloadSnapshot(snapshot);
    if (status.value("status") != "available") return status;
    const auto target = snapshot.value(folder ? "directory" : "path").toString();
    return QDesktopServices::openUrl(QUrl::fromLocalFile(target))
        ? fileOutcome("opened", folder ? "containing-folder-dispatched" : "file-dispatched")
        : fileOutcome("failed", "system-open-failed");
}

QVariantMap FeriaBrowserPolicy::openDownloadedFile(QObject *owner, const QString &id) const {
    return launchDownloadedFile(owner, id, false);
}

QVariantMap FeriaBrowserPolicy::revealDownloadedFile(QObject *owner, const QString &id) const {
    return launchDownloadedFile(owner, id, true);
}
