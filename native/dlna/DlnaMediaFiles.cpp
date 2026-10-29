#include "DlnaMediaFiles.h"

#include "DlnaContentDirectory.h"

#include <QFileInfo>
#include <QString>

#include "upnp.h"
#include "UpnpExtraHeaders.h"
#include "UpnpFileInfo.h"
#include "list.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// What a vdir path resolved to. File serving re-verifies identity at Open; the icon/scpd
// entries are served straight from memory.
struct Resolved {
    enum Kind { Media, Art, Sub, Icon, Scpd, None } kind = None;
    QString itemId;      // Media/Art/Sub
    QString fsPath;      // Media/Art/Sub
    QString mime;        // Content-Type
    QString dlnaFeatures; // contentFeatures.dlna.org value ("" = omit)
    QString transferMode; // default when the request did not ask for one
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
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg"))
        return QStringLiteral("image/jpeg");
    return QStringLiteral("application/octet-stream");
}

// Route + resolve a vdir path against the CURRENT snapshot. Returns Kind::None for anything
// unknown, not in the snapshot, or whose file vanished (that is a 404).
Resolved resolve(DlnaServerCore* core, const QString& path)
{
    Resolved out;
    const auto snap = core->provider();
    if (path.startsWith(QLatin1String("/m/"))) {
        QString id = path.mid(3);
        const int dot = id.indexOf(QLatin1Char('.'));
        if (dot >= 0)
            id.truncate(dot); // the extension is a client-sniffing courtesy only
        const auto it = snap->items.constFind(id);
        if (it == snap->items.constEnd() || !QFileInfo::exists(it->filePath))
            return out;
        out.kind = Resolved::Media;
        out.itemId = id;
        out.fsPath = it->filePath;
        out.mime = it->mime;
        out.dlnaFeatures = DlnaContentDirectory::contentFeaturesForMime(it->mime);
        const bool av = it->mime.startsWith(QLatin1String("video/"))
                        || it->mime.startsWith(QLatin1String("audio/"));
        out.transferMode = av ? QStringLiteral("Streaming") : QStringLiteral("Background");
    } else if (path.startsWith(QLatin1String("/art/"))) {
        const QString id = path.mid(5);
        const auto it = snap->items.constFind(id);
        if (it == snap->items.constEnd() || it->coverPath.isEmpty()
            || !QFileInfo::exists(it->coverPath))
            return out;
        const QString suffix = QFileInfo(it->coverPath).suffix().toLower();
        out.kind = Resolved::Art;
        out.itemId = id;
        out.fsPath = it->coverPath;
        out.mime = imageMimeForSuffix(suffix);
        const QString profile = DlnaContentDirectory::thumbProfileForSuffix(suffix);
        out.dlnaFeatures = profile.isEmpty() ? QStringLiteral("DLNA.ORG_OP=01;DLNA.ORG_CI=0")
                                             : QStringLiteral("DLNA.ORG_PN=%1").arg(profile);
        out.transferMode = QStringLiteral("Interactive");
    } else if (path.startsWith(QLatin1String("/sub/"))) {
        QString id = path.mid(5);
        if (id.endsWith(QLatin1String(".srt")))
            id.chop(4);
        const auto it = snap->items.constFind(id);
        if (it == snap->items.constEnd())
            return out;
        const QString srt = DlnaContentDirectory::subtitlePathFor(*it);
        if (srt.isEmpty() || !QFileInfo::exists(srt))
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
// nodes). Echo transferMode.dlna.org when the client asked for a specific one.
QString requestedTransferMode(UpnpFileInfo* info)
{
    const UpnpListHead* list = UpnpFileInfo_get_ExtraHeadersList(info);
    for (UpnpListIter it = UpnpListBegin(const_cast<UpnpListHead*>(list));
         it != UpnpListEnd(const_cast<UpnpListHead*>(list));
         it = UpnpListNext(const_cast<UpnpListHead*>(list), it)) {
        const UpnpExtraHeaders* h = reinterpret_cast<const UpnpExtraHeaders*>(it);
        const char* name = UpnpExtraHeaders_get_name_cstr(h);
        if (name && QString::fromLatin1(name).toLower()
                       == QLatin1String("transfermode.dlna.org")) {
            const char* value = UpnpExtraHeaders_get_value_cstr(h);
            if (value && *value)
                return QString::fromLatin1(value);
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

int vdir_get_info(const char* filename, UpnpFileInfo* info, const void* cookie,
                  const void** /*requestCookie*/)
{
    DlnaServerCore* core = const_cast<DlnaServerCore*>(
        static_cast<const DlnaServerCore*>(cookie));
    const QString path = stripQuery(filename);
    const QByteArray* memory = nullptr;
    if (path == QLatin1String("/icons/colosseum-48.png")) {
        memory = &core->icon48;
    } else if (path == QLatin1String("/icons/colosseum-120.png")) {
        memory = &core->icon120;
    } else if (path == QLatin1String("/scpd/cd.xml")) {
        memory = &core->cdScpd;
    } else if (path == QLatin1String("/scpd/cm.xml")) {
        memory = &core->cmScpd;
    }
    if (memory) {
        UpnpFileInfo_set_FileLength(info, static_cast<int64_t>(memory->size()));
        UpnpFileInfo_set_LastModified(info, 0);
        UpnpFileInfo_set_IsDirectory(info, 0);
        UpnpFileInfo_set_IsReadable(info, 1);
        UpnpFileInfo_set_ContentType(
            info, path.startsWith(QLatin1String("/icons")) ? "image/png" : "text/xml");
        return 0;
    }

    const Resolved r = resolve(core, path);
    if (r.kind == Resolved::None)
        return -1;
    const QFileInfo fi(r.fsPath);
    if (!fi.isFile())
        return -1;
    UpnpFileInfo_set_FileLength(info, static_cast<int64_t>(fi.size()));
    UpnpFileInfo_set_LastModified(info, fi.lastModified().toSecsSinceEpoch());
    UpnpFileInfo_set_IsDirectory(info, 0);
    UpnpFileInfo_set_IsReadable(info, 1);
    UpnpFileInfo_set_ContentType(info, r.mime.toLatin1().constData());
    if (!r.dlnaFeatures.isEmpty())
        addHeader(info, QStringLiteral("contentFeatures.dlna.org: %1").arg(r.dlnaFeatures));
    const QString asked = requestedTransferMode(info);
    addHeader(info, QStringLiteral("transferMode.dlna.org: %1")
                        .arg(asked.isEmpty() ? r.transferMode : asked));
    return 0;
}

UpnpWebFileHandle vdir_open(const char* filename, enum UpnpOpenFileMode mode, const void* cookie,
                            const void* /*requestCookie*/)
{
    if (mode != UPNP_READ)
        return nullptr;
    DlnaServerCore* core = const_cast<DlnaServerCore*>(
        static_cast<const DlnaServerCore*>(cookie));
    const QString path = stripQuery(filename);

    const QByteArray* memory = nullptr;
    if (path == QLatin1String("/icons/colosseum-48.png"))
        memory = &core->icon48;
    else if (path == QLatin1String("/icons/colosseum-120.png"))
        memory = &core->icon120;
    else if (path == QLatin1String("/scpd/cd.xml"))
        memory = &core->cdScpd;
    else if (path == QLatin1String("/scpd/cm.xml"))
        memory = &core->cmScpd;
    if (memory) {
        auto* mf = new MediaFile;
        mf->mem = *memory;
        mf->epoch = core->revokeEpoch.load();
        return static_cast<UpnpWebFileHandle>(mf);
    }

    const Resolved r = resolve(core, path);
    if (r.kind == Resolved::None)
        return nullptr;
    // Identity re-check for on-disk entries: the snapshot said size+mtime; if the file moved
    // under us, this id no longer names those bytes — 404.
    const QFileInfo fi(r.fsPath);
    auto snap = core->provider();
    const auto it = snap->items.constFind(r.itemId);
    const bool identityOk = it != snap->items.constEnd() && fi.isFile()
        && (r.kind != Resolved::Media
            || (fi.size() == it->size && fi.lastModified().toMSecsSinceEpoch() == it->mtimeMs));
    if (!identityOk)
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
    mf->snap = std::move(snap);
    mf->epoch = core->revokeEpoch.load();
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
    // checks are cheap (epoch load + one hash lookup) and run on every read call.
    if (core->revokeEpoch.load() != mf->epoch)
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
    return _fseeki64(mf->f, offset, origin);
}

int vdir_close(UpnpWebFileHandle h, const void* /*cookie*/, const void* /*requestCookie*/)
{
    auto* mf = static_cast<MediaFile*>(h);
    if (mf->f)
        fclose(mf->f);
    delete mf;
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

void DlnaMediaFiles::uninstall()
{
    UpnpRemoveVirtualDir("/m");
    UpnpRemoveVirtualDir("/art");
    UpnpRemoveVirtualDir("/sub");
    UpnpRemoveVirtualDir("/icons");
    UpnpRemoveVirtualDir("/scpd");
}
