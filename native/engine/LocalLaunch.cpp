#include "LocalLaunch.h"

#include "CbzArchive.h"
#include "VaultIdentity.h"
#include "VaultKit.h"
#include "player/MediaAdmissionProbe.h"

#include <QFileInfo>

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
    if (ext == QLatin1String("cbr"))
        return true; // no general in-place CBR reader yet — accept by extension
    QString err;
    return !MangaTankoban::CbzArchive::imageEntries(path, &err).isEmpty();
}

bool LocalLaunch::validateVideo(const QString& path)
{
    return MediaAdmissionProbe::isAdmitted(MediaAdmissionProbe::probe(path).verdict);
}

bool LocalLaunch::validateBook(const QString& /*path*/)
{
    // Reader 2's backend is authoritative at open; a known book extension routes.
    return true;
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
    case Family::Comic:
        r.accepted = validateComic(path);
        r.reject = r.accepted ? Reject::None : Reject::Corrupt;
        break;
    case Family::Book:
        r.accepted = validateBook(path);
        r.reject = r.accepted ? Reject::None : Reject::Unsupported;
        break;
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
