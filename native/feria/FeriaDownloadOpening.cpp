#include "FeriaAccountStore.h"
#include "FeriaBrowserRuntime.h"
#include <QtWebEngineQuick/QQuickWebEngineProfile>
#include <QtWebEngineQuick/QQuickWebEngineDownloadRequest>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QUrl>
#include <QUuid>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <Windows.h>
#include <ShObjIdl.h>
#else
#include <sys/stat.h>
#endif

namespace {
QVariantMap refused(const char *reason) { return {{"ok", false}, {"reason", QString::fromLatin1(reason)}}; }

// Metadata identity catches removal, replacement, ordinary edits and path
// redirection. It is not a content signature or an immutable execution promise.
QVariantMap fileSnapshot(const QString &path) {
    if (path.isEmpty() || !QDir::isAbsolutePath(path) || path.startsWith("//") || path.startsWith("\\\\")) return {};
    const QFileInfo file(path);
    if (!file.exists() || !file.isFile() || file.isSymLink()) return {};
    const QString canonical = file.canonicalFilePath();
    const QString absolute = QDir::cleanPath(file.absoluteFilePath());
#ifdef Q_OS_WIN
    if (canonical.compare(absolute, Qt::CaseInsensitive) != 0 || absolute.mid(2).contains(':')) return {};
#else
    if (canonical != absolute) return {};
#endif
    QVariantMap result{{"path", canonical}, {"size", file.size()}, {"modified", file.lastModified().toMSecsSinceEpoch()}};
#ifdef Q_OS_WIN
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(canonical.utf16()), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    BY_HANDLE_FILE_INFORMATION info{};
    const bool valid = GetFileInformationByHandle(handle, &info)
        && !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
    CloseHandle(handle);
    if (!valid) return {};
    result.insert("identity", QString::number(info.dwVolumeSerialNumber) + ':'
        + QString::number(info.nFileIndexHigh) + ':' + QString::number(info.nFileIndexLow));
    result.insert("writeTime", QString::number(info.ftLastWriteTime.dwHighDateTime) + ':'
        + QString::number(info.ftLastWriteTime.dwLowDateTime));
#else
    struct stat info{};
    if (::stat(QFile::encodeName(canonical).constData(), &info) != 0) return {};
    result.insert("identity", QString::number(info.st_dev) + ':' + QString::number(info.st_ino));
#endif
    return result;
}

bool programRisk(const QString &path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (QStringList{"exe", "com", "scr", "pif", "msi", "msp", "bat", "cmd", "ps1", "psm1",
        "vbs", "vbe", "js", "jse", "wsf", "wsh", "hta", "lnk", "url", "desktop", "sh", "app",
        "docm", "xlsm", "pptm", "jar", "jnlp", "reg", "inf", "application", "appref-ms",
        "gadget", "sct", "chm", "msix", "appx", "html", "htm", "svg", "mhtml", "mht"}.contains(suffix)) return true;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly) && file.read(2) == "MZ") return true;
#ifndef Q_OS_WIN
    if (QFileInfo(path).permissions() & (QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther)) return true;
#endif
    return false;
}

QVariantMap platformOpen(const QString &path, const QString &source) {
#ifdef Q_OS_WIN
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return refused("os-unavailable");
    IAttachmentExecute *attachment = nullptr;
    HRESULT result = CoCreateInstance(CLSID_AttachmentServices, nullptr, CLSCTX_INPROC_SERVER,
        __uuidof(IAttachmentExecute), reinterpret_cast<void **>(&attachment));
    static const GUID client{0x1cd2c7be, 0x64dc, 0x4cc0, {0x99, 0x88, 0x16, 0xc1, 0x34, 0x92, 0x6c, 0x60}};
    if (SUCCEEDED(result)) result = attachment->SetClientGuid(client);
    if (SUCCEEDED(result)) result = attachment->SetClientTitle(L"Colosseum");
    if (SUCCEEDED(result)) result = attachment->SetLocalPath(reinterpret_cast<LPCWSTR>(path.utf16()));
    // No source evidence means Restricted Zone, not a trusted local-file origin.
    if (SUCCEEDED(result)) result = attachment->SetSource(source.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(source.utf16()));
    if (SUCCEEDED(result)) result = attachment->CheckPolicy();
    if (result == S_OK || result == S_FALSE) {
        // Execute performs the OS-required prompt/scanning and may alter/delete
        // the file. Never fall back to ShellExecute after refusal/cancellation.
        result = attachment->Execute(nullptr, L"open", nullptr);
    }
    if (attachment) attachment->Release();
    CoUninitialize();
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return refused("os-canceled");
    if (result != S_OK) return refused("os-blocked");
    return {{"ok", true}, {"reason", "request-started"}};
#else
    Q_UNUSED(source);
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path))
        ? QVariantMap{{"ok", true}, {"reason", "request-started"}} : refused("open-failed");
#endif
}
}

bool FeriaAccountStore::watchBrowserDownloads(QObject *object, QObject *runtimeObject) {
    auto *profile = qobject_cast<QQuickWebEngineProfile *>(object);
    auto *runtime = qobject_cast<FeriaBrowserRuntime *>(runtimeObject);
    if (!profile || !runtime || !runtime->hasProfile(profile)
        || profile->thread() != thread() || runtime->thread() != thread() || QThread::currentThread() != thread()) return false;
    if (m_downloadProfiles.contains(profile)) return true;
    const QPointer<FeriaBrowserRuntime> guardedRuntime(runtime);
    m_downloadProfiles.insert(profile, connect(profile, &QQuickWebEngineProfile::downloadRequested, this,
        [this, profile, guardedRuntime](QQuickWebEngineDownloadRequest *request) {
            if (!request || request->parent() != profile || request->thread() != thread()
                || !guardedRuntime || !guardedRuntime->hasProfile(profile)) return;
            DownloadCapture capture;
            capture.authority = QUuid::createUuid().toString(QUuid::WithoutBraces);
            capture.generation = m_generation; capture.profile = profile; capture.runtime = guardedRuntime;
            m_downloadCaptures.insert(request, capture);
            connect(request, &QWebEngineDownloadRequest::stateChanged, this, [this, request] { captureDownloadCompletion(request); });
            connect(request, &QWebEngineDownloadRequest::isFinishedChanged, this, [this, request] { captureDownloadCompletion(request); });
            connect(request, &QObject::destroyed, this, [this, request] { m_downloadCaptures.remove(request); });
            captureDownloadCompletion(request);
        }));
    connect(profile, &QObject::destroyed, this, [this, profile] { m_downloadProfiles.remove(profile); });
    return true;
}

void FeriaAccountStore::captureDownloadCompletion(QObject *object) {
    auto found = m_downloadCaptures.find(object);
    if (found == m_downloadCaptures.end() || found->generation != m_generation || !found->profile) return;
    auto *request = qobject_cast<QWebEngineDownloadRequest *>(object);
    if (!request) return;
    if (request->state() != QWebEngineDownloadRequest::DownloadCompleted || !request->isFinished()) return;
    if (!found->row.isEmpty()) return;
    const auto path = QDir(request->downloadDirectory()).filePath(request->downloadFileName());
    found->row = {{"id", found->authority}, {"path", path}, {"fileName", request->downloadFileName()},
        {"url", request->url().toString(QUrl::FullyEncoded)}, {"mimeType", request->mimeType()},
        {"state", "completed"}, {"downloadSnapshot", fileSnapshot(path)},
        {"privateBrowsing", qobject_cast<QQuickWebEngineProfile *>(found->profile.data())->isOffTheRecord()}};
}

QString FeriaAccountStore::browserDownloadAuthority(QObject *request) const {
    const auto found = m_downloadCaptures.constFind(request);
    return found != m_downloadCaptures.cend() && found->generation == m_generation ? found->authority : QString();
}

void FeriaAccountStore::forgetBrowserDownloadAuthority(const QString &authority) {
    for (auto it = m_downloadCaptures.begin(); it != m_downloadCaptures.end();) {
        if (it->authority == authority) it = m_downloadCaptures.erase(it); else ++it;
    }
}

QVariantMap FeriaAccountStore::downloadOpenRow(const QString &id, bool current) const {
    if (current) {
        for (const auto &capture : m_downloadCaptures) {
            auto *runtime = qobject_cast<FeriaBrowserRuntime *>(capture.runtime.data());
            if (capture.authority == id && capture.generation == m_generation && capture.profile
                && runtime && runtime->hasProfile(capture.profile)) return capture.row;
        }
        return {};
    }
    for (const auto &value : browserDownloadHistory()) {
        const auto row = value.toMap(); if (row.value("id").toString() == id) return row;
    }
    return {};
}

QVariantMap FeriaAccountStore::prepareBrowserDownloadOpen(const QString &id, int generation, bool current) {
    if (generation != m_generation) return refused("stale-account");
    const auto row = downloadOpenRow(id, current);
    if (row.isEmpty()) return refused("unowned-download");
    if (row.value("state") != "completed") return refused("incomplete-download");
    const auto snapshot = fileSnapshot(row.value("path").toString());
    if (snapshot.isEmpty()) return refused("missing-or-redirected-file");
    const QString path = snapshot.value("path").toString();
    const auto name = QFileInfo(path).fileName();
    if (!row.value("fileName").toString().isEmpty() && row.value("fileName").toString() != name) return refused("path-substitution");
    const auto completed = row.value("downloadSnapshot").toMap();
    if (current && completed.isEmpty()) return refused("unverified-completion");
    if (!completed.isEmpty() && completed.value("path") != snapshot.value("path")) return refused("path-substitution");
    const QUrl url(safeUrl(row.value("url").toString()), QUrl::StrictMode);
    const QString source = url.isValid() && !url.host().isEmpty()
        && (url.scheme() == "https" || url.scheme() == "http") ? url.toString(QUrl::FullyEncoded) : QString();
    QStringList warnings;
    if (completed.isEmpty()) warnings << tr("This older download record cannot verify whether the file has changed.");
    else if (completed != snapshot) warnings << tr("This file has changed since the download completed.");
    if (source.isEmpty()) warnings << tr("The download's website could not be verified.");
    if (programRisk(path)) warnings << tr("This file can run a program or script. Open it only if you trust its source.");
    QString displayName = name;
    for (auto &character : displayName) if (character.category() == QChar::Other_Format) character = QLatin1Char('?');
    if (displayName != name) warnings << tr("The file name contains hidden direction or formatting characters.");
    const QString ticket = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_downloadOpenTickets.insert(ticket, {id, source, generation, current, snapshot});
    return {{"ok", true}, {"ticket", ticket}, {"fileName", displayName}, {"source", source},
        {"requiresConfirmation", !warnings.isEmpty()}, {"warning", warnings.join('\n')}};
}

QVariantMap FeriaAccountStore::finishBrowserDownloadOpen(const QString &key, bool confirmed) {
    const auto found = m_downloadOpenTickets.find(key);
    if (found == m_downloadOpenTickets.end()) return refused("stale-ticket");
    const auto ticket = found.value(); m_downloadOpenTickets.erase(found);
    if (!confirmed) return refused("canceled");
    if (ticket.generation != m_generation) return refused("stale-account");
    const auto row = downloadOpenRow(ticket.id, ticket.current);
    if (row.isEmpty() || row.value("state") != "completed") return refused("unowned-download");
    const QUrl currentSource(safeUrl(row.value("url").toString()), QUrl::StrictMode);
    const QString source = currentSource.isValid() && !currentSource.host().isEmpty()
        && (currentSource.scheme() == "https" || currentSource.scheme() == "http")
        ? currentSource.toString(QUrl::FullyEncoded) : QString();
    if (source != ticket.source) return refused("provenance-changed");
    if (!row.value("fileName").toString().isEmpty()
        && row.value("fileName").toString() != QFileInfo(ticket.snapshot.value("path").toString()).fileName()) return refused("path-substitution");
    if (fileSnapshot(row.value("path").toString()) != ticket.snapshot) return refused("file-changed");
    const QString path = ticket.snapshot.value("path").toString();
    // Copy the callback before invocation: it can synchronously destroy its owner.
    const auto opener = m_downloadOpener;
    return opener ? opener(path, ticket.source) : platformOpen(path, ticket.source);
}
