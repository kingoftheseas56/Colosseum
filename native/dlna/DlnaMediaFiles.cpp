#include "DlnaMediaFiles.h"

#include "DlnaContentDirectory.h"

#include <QFileInfo>
#include <QString>

#include "upnp.h"
#include "UpnpExtraHeaders.h"
#include "UpnpFileInfo.h"
#include "list.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// Largest cover /art/ will serve. Thumbnails are tens of KB; anything this big is not one.
constexpr qint64 kMaxArtBytes = 10 * 1024 * 1024;

std::atomic<int> g_activeHandles{0}; // open vdir handles (disk + memory), see activeHandles()

// What a vdir path resolved to. get_info checks size+mtime against the snapshot and hands the
// resulting identity token to open() through the request cookie; the icon/scpd entries are
// served straight from memory.
struct Resolved {
    enum Kind { Media, Art, Sub, Icon, Scpd, None } kind = None;
    QString itemId;      // Media/Art/Sub
    QString fsPath;      // Media/Art/Sub
    QString mime;        // Content-Type
    QString dlnaFeatures; // contentFeatures.dlna.org value ("" = omit)
    QString transferMode; // default when the request did not ask for one
    DlnaItem item;       // Media: the snapshot's copy (size + mtime are the identity to match)
};

QString stripQuery(const char* filename)
{
    QString p = QString::fromLatin1(filename);
    const int q = p.indexOf(QLatin1Char('?'));
    if (q >= 0)
        p.truncate(q);
    return p;
}

QString imageMimeForSuffix(const QString& suffix)
{
    if (suffix == QLatin1String("png"))
        return QStringLiteral("image/png");
    return QStringLiteral("image/jpeg"); // only jpg/jpeg/png ever reach here
}

bool isServedCoverSuffix(const QString& suffix)
{
    return suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")
        || suffix == QLatin1String("png");
}

// Route + resolve a vdir path against the CURRENT snapshot. Returns Kind::None for anything
// unknown, not in the snapshot, or not currently being served (that is a 404). File existence
// and identity are the callers' stat.
Resolved resolve(DlnaServerCore* core, const QString& path)
{
    Resolved out;
    if (!core->serving.load())
        return out;
    const auto snap = core->provider();
    if (path.startsWith(QLatin1String("/m/"))) {
        QString id = path.mid(3);
        const int dot = id.indexOf(QLatin1Char('.'));
        if (dot >= 0)
            id.truncate(dot); // the extension is a client-sniffing courtesy only
        const auto it = snap->items.constFind(id);
        if (it == snap->items.constEnd())
            return out;
        out.kind = Resolved::Media;
        out.itemId = id;
        out.fsPath = it->filePath;
        out.mime = it->mime;
        out.item = *it;
        out.dlnaFeatures = DlnaContentDirectory::contentFeaturesForMime(it->mime);
        const bool av = it->mime.startsWith(QLatin1String("video/"))
                        || it->mime.startsWith(QLatin1String("audio/"));
        out.transferMode = av ? QStringLiteral("Streaming") : QStringLiteral("Background");
    } else if (path.startsWith(QLatin1String("/art/"))) {
        const QString id = path.mid(5);
        const auto it = snap->items.constFind(id);
        if (it == snap->items.constEnd() || it->coverPath.isEmpty())
            return out;
        const QString suffix = QFileInfo(it->coverPath).suffix().toLower();
        if (!isServedCoverSuffix(suffix))
            return out;
        out.kind = Resolved::Art;
        out.itemId = id;
        out.fsPath = it->coverPath;
        out.mime = imageMimeForSuffix(suffix);
        const QString profile = DlnaContentDirectory::thumbProfileForSuffix(suffix);
        out.dlnaFeatures = QStringLiteral("DLNA.ORG_PN=%1").arg(profile);
        out.transferMode = QStringLiteral("Interactive");
    } else if (path.startsWith(QLatin1String("/sub/"))) {
        QString id = path.mid(5);
        if (id.endsWith(QLatin1String(".srt")))
            id.chop(4);
        const auto it = snap->items.constFind(id);
        if (it == snap->items.constEnd())
            return out;
        const QString srt = DlnaContentDirectory::subtitlePathFor(*it); // isFile && !isSymLink
        if (srt.isEmpty())
            return out;
        out.kind = Resolved::Sub;
        out.itemId = id;
        out.fsPath = srt;
        out.mime = QStringLiteral("text/srt");
        out.transferMode = QStringLiteral("Background");
    }
    return out;
}

// The request's unrecognized headers arrive pre-loaded in the ExtraHeaders list (name/value
// nodes). Finds one by its lower-cased name; `present` says whether the client sent it at all
// (some are flags whose value does not matter).
QString requestHeader(UpnpFileInfo* info, const char* lowerName, bool* present = nullptr)
{
    if (present)
        *present = false;
    const UpnpListHead* list = UpnpFileInfo_get_ExtraHeadersList(info);
    for (UpnpListIter it = UpnpListBegin(const_cast<UpnpListHead*>(list));
         it != UpnpListEnd(const_cast<UpnpListHead*>(list));
         it = UpnpListNext(const_cast<UpnpListHead*>(list), it)) {
        const UpnpExtraHeaders* h = reinterpret_cast<const UpnpExtraHeaders*>(it);
        const char* name = UpnpExtraHeaders_get_name_cstr(h);
        if (name && QString::fromLatin1(name).toLower() == QLatin1String(lowerName)) {
            if (present)
                *present = true;
            const char* value = UpnpExtraHeaders_get_value_cstr(h);
            return value ? QString::fromLatin1(value) : QString();
        }
    }
    return QString();
}

void addHeader(UpnpFileInfo* info, const QString& line)
{
    // The web server emits only the `resp` field, as the full "Name: value" line.
    UpnpExtraHeaders* h = UpnpExtraHeaders_new();
    UpnpExtraHeaders_set_resp(h, line.toLatin1().constData());
    UpnpFileInfo_add_to_list_ExtraHeadersList(
        info, const_cast<UpnpListHead*>(UpnpExtraHeaders_get_node(h)));
}

// What get_info promised the client in its headers, as one non-zero 64-bit token (FNV-1a over
// kind, item id, byte size and mtime). get_info sends it to open() through the request cookie;
// open() recomputes it from a fresh stat and refuses on mismatch, so a file that changed
// between the advertised Content-Length and the first byte is a clean 404-class failure
// instead of a truncated 200. A pointer-sized integer, not an allocation: pupnp never frees
// a request cookie (a HEAD request never reaches open/close).
quint64 identityToken(const Resolved& r, qint64 size, qint64 mtimeMs)
{
    quint64 h = 1469598103934665603ull;
    auto mix = [&h](const void* data, size_t n) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < n; i++) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
    };
    const int kind = int(r.kind);
    mix(&kind, sizeof(kind));
    const QByteArray id = r.itemId.toUtf8();
    mix(id.constData(), size_t(id.size()));
    mix(&size, sizeof(size));
    mix(&mtimeMs, sizeof(mtimeMs));
    return h ? h : 1;
}

// The per-request file handle. Disk entries own a FILE*; the four memory entries (icons,
// SCPDs) carry their bytes instead. The snapshot is kept alive so revocation checks compare
// against the live one instead of dangling.
struct MediaFile {
    FILE* f = nullptr;
    QByteArray mem;    // non-empty: serve from memory (icons/SCPDs)
    qint64 memPos = 0;
    QString itemId;
    std::shared_ptr<const DlnaSnapshot> snap;
    uint64_t epoch = 0;
};

// The icons and SCPDs served straight from memory; null for anything else.
const QByteArray* memoryEntry(const DlnaServerCore* core, const QString& path)
{
    if (path == QLatin1String("/icons/colosseum-48.png"))
        return &core->icon48;
    if (path == QLatin1String("/icons/colosseum-120.png"))
        return &core->icon120;
    if (path == QLatin1String("/scpd/cd.xml"))
        return &core->cdScpd;
    if (path == QLatin1String("/scpd/cm.xml"))
        return &core->cmScpd;
    if (path == QLatin1String("/scpd/mrr.xml"))
        return &core->mrrScpd;
    return nullptr;
}

int vdir_get_info(const char* filename, UpnpFileInfo* info, const void* cookie,
                  const void** requestCookie)
{
    DlnaServerCore* core = const_cast<DlnaServerCore*>(
        static_cast<const DlnaServerCore*>(cookie));
    if (!core->serving.load())
        return -1;
    const QString path = stripQuery(filename);
    const QByteArray* memory = memoryEntry(core, path);
    if (memory) {
        UpnpFileInfo_set_FileLength(info, static_cast<int64_t>(memory->size()));
        UpnpFileInfo_set_LastModified(info, 0);
        UpnpFileInfo_set_IsDirectory(info, 0);
        UpnpFileInfo_set_IsReadable(info, 1);
        UpnpFileInfo_set_ContentType(info,
                                     path.startsWith(QLatin1String("/icons"))
                                         ? "image/png"
                                         : "text/xml; charset=\"utf-8\"");
        return 0;
    }

    const Resolved r = resolve(core, path);
    if (r.kind == Resolved::None)
        return -1;
    const QFileInfo fi(r.fsPath);
    if (!fi.isFile())
        return -1;
    const qint64 size = fi.size();
    const qint64 mtimeMs = fi.lastModified().toMSecsSinceEpoch();
    // Identity, checked BEFORE any header goes out: the snapshot said size+mtime; if the file
    // moved under us this id no longer names those bytes. The snapshot's size is the one we
    // advertise (it equals the stat just taken).
    if (r.kind == Resolved::Media && (size != r.item.size || mtimeMs != r.item.mtimeMs))
        return -1;
    if (r.kind == Resolved::Art && size > kMaxArtBytes)
        return -1;
    UpnpFileInfo_set_FileLength(info, static_cast<int64_t>(r.kind == Resolved::Media
                                                               ? r.item.size
                                                               : size));
    UpnpFileInfo_set_LastModified(info, mtimeMs / 1000);
    UpnpFileInfo_set_IsDirectory(info, 0);
    UpnpFileInfo_set_IsReadable(info, 1);
    UpnpFileInfo_set_ContentType(info, r.mime.toLatin1().constData());
    if (!r.dlnaFeatures.isEmpty())
        addHeader(info, QStringLiteral("contentFeatures.dlna.org: %1").arg(r.dlnaFeatures));
    const QString asked = requestHeader(info, "transfermode.dlna.org");
    addHeader(info, QStringLiteral("transferMode.dlna.org: %1")
                        .arg(asked.isEmpty() ? r.transferMode : asked));
    // Samsung TVs ask for the subtitle URL with a request header instead of reading the DIDL.
    if (r.kind == Resolved::Media && r.mime.startsWith(QLatin1String("video/"))) {
        bool wantsCaption = false;
        requestHeader(info, "getcaptioninfo.sec", &wantsCaption);
        if (wantsCaption && !DlnaContentDirectory::subtitlePathFor(r.item).isEmpty())
            addHeader(info, QStringLiteral("CaptionInfo.sec: %1/sub/%2.srt")
                                .arg(core->baseUrl, r.itemId));
    }
    if (requestCookie)
        *requestCookie = reinterpret_cast<const void*>(
            static_cast<uintptr_t>(identityToken(r, size, mtimeMs)));
    return 0;
}

UpnpWebFileHandle vdir_open(const char* filename, enum UpnpOpenFileMode mode, const void* cookie,
                            const void* requestCookie)
{
    if (mode != UPNP_READ)
        return nullptr;
    DlnaServerCore* core = const_cast<DlnaServerCore*>(
        static_cast<const DlnaServerCore*>(cookie));
    if (!core->serving.load())
        return nullptr;
    const QString path = stripQuery(filename);

    const QByteArray* memory = memoryEntry(core, path);
    if (memory) {
        auto* mf = new MediaFile;
        mf->mem = *memory;
        mf->epoch = core->revokeEpoch.load();
        g_activeHandles.fetch_add(1);
        return static_cast<UpnpWebFileHandle>(mf);
    }

    // The identity get_info advertised must still hold: same item, same size+mtime. A missing
    // cookie means this request never passed get_info (it must not read anything).
    const quint64 promised = quint64(reinterpret_cast<uintptr_t>(requestCookie));
    if (promised == 0)
        return nullptr;
    const Resolved r = resolve(core, path);
    if (r.kind == Resolved::None)
        return nullptr;
    const QFileInfo fi(r.fsPath);
    if (!fi.isFile()
        || identityToken(r, fi.size(), fi.lastModified().toMSecsSinceEpoch()) != promised)
        return nullptr;
    FILE* f = nullptr;
#ifdef Q_OS_WIN
    // Library paths are arbitrary Unicode; fopen()'s ANSI door would miss half of them.
    f = _wfopen(r.fsPath.toStdWString().c_str(), L"rb");
#else
    f = std::fopen(r.fsPath.toLocal8Bit().constData(), "rb");
#endif
    if (!f)
        return nullptr;
    auto* mf = new MediaFile;
    mf->f = f;
    mf->itemId = r.itemId;
    mf->snap = core->provider();
    mf->epoch = core->revokeEpoch.load();
    g_activeHandles.fetch_add(1);
    return static_cast<UpnpWebFileHandle>(mf);
}

int vdir_read(UpnpWebFileHandle h, char* buf, size_t buflen, const void* cookie,
              const void* /*requestCookie*/)
{
    auto* mf = static_cast<MediaFile*>(h);
    if (!mf->mem.isEmpty()) {
        const qint64 left = mf->mem.size() - mf->memPos;
        const qint64 n = qMin<qint64>(qint64(buflen), qMax<qint64>(left, 0));
        if (n <= 0)
            return 0; // EOF
        memcpy(buf, mf->mem.constData() + mf->memPos, size_t(n));
        mf->memPos += n;
        return int(n);
    }
    DlnaServerCore* core = const_cast<DlnaServerCore*>(
        static_cast<const DlnaServerCore*>(cookie));
    // Revocation: the item must still be shared, and the server must not have moved on. Both
    // checks are cheap (epoch/flag load + one hash lookup) and run on every read call.
    if (!core->serving.load() || core->revokeEpoch.load() != mf->epoch)
        return -1;
    const auto snap = core->provider();
    if (snap != mf->snap && !snap->items.contains(mf->itemId))
        return -1;
    return int(fread(buf, 1, buflen, mf->f));
}

int vdir_seek(UpnpWebFileHandle h, int64_t offset, int origin, const void* /*cookie*/,
              const void* /*requestCookie*/)
{
    auto* mf = static_cast<MediaFile*>(h);
    if (!mf->mem.isEmpty()) {
        qint64 base = origin == SEEK_SET ? 0
                     : origin == SEEK_CUR ? mf->memPos
                                          : mf->mem.size();
        mf->memPos = qBound<qint64>(0, base + offset, mf->mem.size());
        return 0;
    }
#ifdef Q_OS_WIN
    return _fseeki64(mf->f, offset, origin);
#else
    return fseeko(mf->f, static_cast<off_t>(offset), origin);
#endif
}

int vdir_close(UpnpWebFileHandle h, const void* /*cookie*/, const void* /*requestCookie*/)
{
    auto* mf = static_cast<MediaFile*>(h);
    if (mf->f)
        fclose(mf->f);
    delete mf;
    g_activeHandles.fetch_sub(1);
    return 0;
}

int vdir_write(UpnpWebFileHandle /*h*/, char* /*buf*/, size_t /*buflen*/, const void* /*cookie*/,
               const void* /*requestCookie*/)
{
    return -1; // the server never accepts uploads
}

} // namespace

void DlnaMediaFiles::install(DlnaServerCore* core)
{
    UpnpVirtualDir_set_GetInfoCallback(vdir_get_info);
    UpnpVirtualDir_set_OpenCallback(vdir_open);
    UpnpVirtualDir_set_ReadCallback(vdir_read);
    UpnpVirtualDir_set_SeekCallback(vdir_seek);
    UpnpVirtualDir_set_CloseCallback(vdir_close);
    UpnpVirtualDir_set_WriteCallback(vdir_write);
    UpnpAddVirtualDir("/m", core, nullptr);
    UpnpAddVirtualDir("/art", core, nullptr);
    UpnpAddVirtualDir("/sub", core, nullptr);
    UpnpAddVirtualDir("/icons", core, nullptr);
    UpnpAddVirtualDir("/scpd", core, nullptr);
}

int DlnaMediaFiles::activeHandles()
{
    return g_activeHandles.load();
}
