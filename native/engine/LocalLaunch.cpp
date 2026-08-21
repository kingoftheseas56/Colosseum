#include "LocalLaunch.h"

#include "CbzArchive.h"
#include "VaultIdentity.h"
#include "VaultKit.h"
#include "player/MediaAdmissionProbe.h"

#include <QFileInfo>
#include <QUrl>

LocalLaunch::Family LocalLaunch::classify(const QString& path)
{
    switch (VaultKit::kindForFile(path)) {
    case VaultKit::MediaKind::Comic: return Family::Comic;
    case VaultKit::MediaKind::Book:  return Family::Book;
    case VaultKit::MediaKind::Video: return Family::Video;
    case VaultKit::MediaKind::Unknown: break;
    }
    return Family::Unknown;
}

QString LocalLaunch::familyName(Family f)
{
    switch (f) {
    case Family::Comic: return QStringLiteral("comic");
    case Family::Book:  return QStringLiteral("book");
    case Family::Video: return QStringLiteral("video");
    case Family::Unknown: break;
    }
    return QStringLiteral("unknown");
}

QString LocalLaunch::rejectName(Reject r)
{
    switch (r) {
    case Reject::None: return QStringLiteral("none");
    case Reject::NotFound: return QStringLiteral("not-found");
    case Reject::Unsupported: return QStringLiteral("unsupported");
    case Reject::Corrupt: return QStringLiteral("corrupt");
    case Reject::NoDecoder: return QStringLiteral("no-decoder");
    }
    return QStringLiteral("?");
}

bool LocalLaunch::validateComic(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    // VaultPageStore and the current comic reader can enumerate CBZ pages only. A CBR is still
    // classified as comic by VaultKit for shelving, but launch must fail closed until a real CBR
    // backend exists. Extension-only admission creates a session that cannot render a single page.
    if (ext != QLatin1String("cbz"))
        return false;
    QString err;
    return !MangaTankoban::CbzArchive::imageEntries(path, &err).isEmpty();
}

bool LocalLaunch::validateVideo(const QString& path)
{
    return MediaAdmissionProbe::isAdmitted(MediaAdmissionProbe::probe(path).verdict);
}

bool LocalLaunch::validateBook(const QString& path)
{
    // Arc 14 D6 capability matrix — ground-truthed against the vendored Reader 2 engine
    // (resources/reader2/vendor/foliate-anx/src/book.js + mobi.js), not assumed from extension:
    //
    //   ext   | verdict            | evidence
    //   epub  | renders            | engine-dispatch: book.js isZip() -> new EPUB().init()
    //   pdf   | renders            | engine-dispatch: book.js isPDF() magic '%PDF-' -> pdf.js makePDF
    //   fb2   | renders            | engine-dispatch: book.js isFB2() -> fb2.js makeFB2
    //   mobi  | renders            | engine-execution: mobi.js isMOBI() (PalmDB 'BOOKMOBI' magic,
    //         |                    | content-based, NOT extension) -> MOBI.open() -> MOBI6; verified
    //         |                    | live under Node against a synthetic PalmDOC fixture (real text
    //         |                    | extracted end to end through the unmodified vendored module).
    //   azw3  | renders            | engine-execution: AZW3 IS KF8-in-a-PalmDB container. The SAME
    //         |                    | isMOBI() magic check admits it (extension irrelevant to the
    //         |                    | engine); mobi.js's MOBI.open() reads MOBI_HEADER.version >= 8
    //         |                    | and dispatches to the real KF8 class. Verified live under Node:
    //         |                    | a synthetic KF8 fixture drove the unmodified KF8 class through
    //         |                    | real FDST + INDX/TAGX skeleton+fragment table parsing and real
    //         |                    | EXTH metadata parsing to a successful KF8.init() (no synthetic
    //         |                    | content pages were authored, so no page render was attempted —
    //         |                    | structural dispatch only, not full render).
    //   djvu  | rejected-unsupported| No DJVU path exists anywhere in the engine: it is not a zip,
    //         |                    | not '%PDF-', fails the MOBI PalmDB magic check, and is not FB2
    //         |                    | by name/type, so book.js's getView() falls through every branch
    //         |                    | and throws "File type not supported". Verified: a DJVU magic
    //         |                    | ('AT&TFORM') fixture run through the real detection functions
    //         |                    | hits every reject branch.
    //
    // Only DJVU is fail-closed here; the QML picker (qml/BiblioApi.js) mirrors this so a LibGen/
    // torrent pick never lands a file the reader provably cannot open.
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext != QLatin1String("djvu");
}

LocalLaunch::Route LocalLaunch::route(const QString& path)
{
    Route r;
    const QFileInfo fi(path);
    if (!fi.exists()) {
        r.reject = Reject::NotFound;
        r.detail = QStringLiteral("file not found");
        return r;
    }

    r.family = classify(path);
    switch (r.family) {
    case Family::Comic: {
        const QString ext = fi.suffix().toLower();
        r.accepted = validateComic(path);
        if (r.accepted) {
            r.reject = Reject::None;
        } else if (ext == QLatin1String("cbr")) {
            r.reject = Reject::Unsupported;
            r.detail = QStringLiteral("CBR reading is not available in the current comic reader");
        } else {
            r.reject = Reject::Corrupt;
        }
        break;
    }
    case Family::Book: {
        const QString ext = fi.suffix().toLower();
        r.accepted = validateBook(path);
        if (r.accepted) {
            r.reject = Reject::None;
        } else {
            r.reject = Reject::Unsupported;
            if (ext == QLatin1String("djvu")) {
                r.detail = QStringLiteral("DJVU reading is not available — no DJVU decoder exists "
                                           "in the Reader 2 engine");
            }
        }
        break;
    }
    case Family::Video:
        r.accepted = validateVideo(path);
        r.reject = r.accepted ? Reject::None : Reject::NoDecoder;
        break;
    case Family::Unknown:
        r.accepted = false;
        r.reject = Reject::Unsupported;
        break;
    }

    if (r.accepted) {
        r.vaultId = VaultIdentity::computeId(
            path, fi.size(), fi.lastModified().toMSecsSinceEpoch());
    }
    return r;
}

namespace {
QString toLocalPath(const QString& s)
{
    return s.startsWith(QLatin1String("file:")) ? QUrl(s).toLocalFile() : s;
}

// The taskbar tile / reader title show a cleaned name, not a raw filename
// (Preflight §8). Strip the extension, turn separators into spaces, collapse.
QString cleanFileTitle(const QString& path)
{
    QString t = QFileInfo(path).completeBaseName();
    t.replace(QLatin1Char('_'), QLatin1Char(' '));
    t.replace(QLatin1Char('.'), QLatin1Char(' '));
    t = t.simplified();
    return t.isEmpty() ? QFileInfo(path).fileName() : t;
}
} // namespace

QVariantMap LocalLaunch::routeInfo(const QString& pathOrUrl)
{
    const QString path = toLocalPath(pathOrUrl);
    const Route r = route(path);
    QVariantMap m;
    m[QStringLiteral("path")]     = path;
    m[QStringLiteral("family")]   = familyName(r.family);
    m[QStringLiteral("accepted")] = r.accepted;
    m[QStringLiteral("reject")]   = rejectName(r.reject);
    m[QStringLiteral("vaultId")]  = r.vaultId;
    m[QStringLiteral("detail")]   = r.detail;
    m[QStringLiteral("title")]    = cleanFileTitle(path);
    if (m_identity && r.accepted) {
        const QFileInfo fi(path);
        const QVariantMap identity = m_identity->observeFile(
            path, fi.size(), fi.lastModified().toMSecsSinceEpoch());
        m[QStringLiteral("vaultId")] = identity.value(QStringLiteral("id"));
        for (const QString& key : {QStringLiteral("prompt"), QStringLiteral("type"),
                                   QStringLiteral("relationship"), QStringLiteral("oldId"),
                                   QStringLiteral("newId"), QStringLiteral("oldPath"),
                                   QStringLiteral("newPath")}) {
            if (identity.contains(key))
                m[key] = identity.value(key);
        }
    }
    return m;
}

bool LocalLaunch::decideIdentityCeremony(const QString& relationship, const QString& choice)
{
    return m_identity && m_identity->decideCeremony(relationship, choice);
}

QVariantMap LocalLaunch::open(const QStringList& pathsOrUrls)
{
    if (pathsOrUrls.isEmpty()) {
        QVariantMap m;
        m[QStringLiteral("path")]     = QString();
        m[QStringLiteral("family")]   = familyName(Family::Unknown);
        m[QStringLiteral("accepted")] = false;
        m[QStringLiteral("reject")]   = rejectName(Reject::NotFound);
        m[QStringLiteral("vaultId")]  = QString();
        m[QStringLiteral("detail")]   = QStringLiteral("no file");
        m[QStringLiteral("title")]    = QString();
        m[QStringLiteral("ignored")]  = 0;
        m[QStringLiteral("staged")]   = m_nextToOpen.size();
        return m;
    }
    QVariantMap m = openSingle(pathsOrUrls.first());
    m[QStringLiteral("ignored")] = pathsOrUrls.size() - 1;
    for (int i = 1; i < pathsOrUrls.size(); ++i)
        m_nextToOpen.append(routeInfo(pathsOrUrls.at(i)));
    if (pathsOrUrls.size() > 1)
        emit nextToOpenChanged();
    m[QStringLiteral("staged")] = m_nextToOpen.size();
    return m;
}

QVariantMap LocalLaunch::openSingle(const QString& pathOrUrl)
{
    QVariantMap m = routeInfo(pathOrUrl);
    // An accepted open is remembered for one-click reopen (Slice 9); a rejection is not.
    if (m.value(QStringLiteral("accepted")).toBool()) {
        m_recent.record(m.value(QStringLiteral("path")).toString(),
                        m.value(QStringLiteral("title")).toString(),
                        m.value(QStringLiteral("family")).toString(),
                        m.value(QStringLiteral("vaultId")).toString());
        emit recentChanged();
    }
    return m;
}

QVariantMap LocalLaunch::openNextToOpen(int index)
{
    if (index < 0 || index >= m_nextToOpen.size()) {
        QVariantMap m;
        m[QStringLiteral("accepted")] = false;
        m[QStringLiteral("reject")] = rejectName(Reject::NotFound);
        m[QStringLiteral("detail")] = QStringLiteral("staged item not found");
        m[QStringLiteral("staged")] = m_nextToOpen.size();
        return m;
    }
    const QString path = m_nextToOpen.at(index).toMap().value(QStringLiteral("path")).toString();
    m_nextToOpen.removeAt(index);
    emit nextToOpenChanged();
    QVariantMap m = openSingle(path); // re-probe: the file may have changed while staged
    m[QStringLiteral("ignored")] = 0;
    m[QStringLiteral("staged")] = m_nextToOpen.size();
    return m;
}

void LocalLaunch::removeNextToOpen(int index)
{
    if (index < 0 || index >= m_nextToOpen.size())
        return;
    m_nextToOpen.removeAt(index);
    emit nextToOpenChanged();
}

bool LocalLaunch::isDir(const QString& pathOrUrl) const
{
    return QFileInfo(toLocalPath(pathOrUrl)).isDir();
}

void LocalLaunch::clearRecent()
{
    m_recent.clear();      // wipes shortcuts only — reading progress is a separate store
    emit recentChanged();
}
