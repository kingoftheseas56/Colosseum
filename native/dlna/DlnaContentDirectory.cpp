#include "DlnaContentDirectory.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace DlnaContentDirectory {

namespace {

const QString kStreamingFlags = QStringLiteral(
    "DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01500000000000000000000000000000");

QString didlOpen()
{
    return QStringLiteral(
        "<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" "
        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
        "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" "
        "xmlns:dlna=\"urn:schemas-dlna-org:metadata-1-0/\" "
        "xmlns:sec=\"urn:samsung.com:sec\">");
}

// Filter mask (Kodi's ConvertFilterToMask, simplified honestly): "*" or "" admits
// everything; otherwise the element name (text before '@', case-insensitive) must be listed.
// dc:title is always on.
class ElementFilter
{
public:
    explicit ElementFilter(const QString& filter)
    {
        const QString f = filter.trimmed();
        m_all = f.isEmpty() || f == QLatin1String("*");
        if (!m_all) {
            const QStringList parts = f.split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (QString p : parts) {
                p = p.trimmed();
                const int at = p.indexOf(QLatin1Char('@'));
                if (at > 0)
                    p.truncate(at);
                m_set.insert(p.toLower());
            }
        }
    }

    bool accepts(const QString& element) const
    {
        return m_all || m_set.contains(element.toLower());
    }

private:
    bool m_all = true;
    QSet<QString> m_set;
};

QString mediaUrl(const QString& baseUrl, const DlnaItem& item)
{
    const QString suffix = QFileInfo(item.filePath).suffix().toLower();
    if (suffix.isEmpty())
        return baseUrl + QLatin1String("/m/") + item.id;
    return baseUrl + QLatin1String("/m/") + item.id + QLatin1Char('.') + suffix;
}

// upnp:albumArtURI for items with a local cover; served through /art with the DLNA thumbnail
// profile the cover's extension implies (JPEG_TN/PNG_TN, Kodi's GetImageDLNAProfile).
QString albumArtElement(const ElementFilter& filter, const QString& baseUrl, const DlnaItem& item)
{
    if (item.coverPath.isEmpty() || !filter.accepts(QStringLiteral("upnp:albumArtURI")))
        return QString();
    const QString suffix = QFileInfo(item.coverPath).suffix().toLower();
    const QString profile = thumbProfileForSuffix(suffix);
    const QString url = baseUrl + QLatin1String("/art/") + item.id;
    if (profile.isEmpty())
        return QStringLiteral("<upnp:albumArtURI>%1</upnp:albumArtURI>").arg(escape(url));
    return QStringLiteral("<upnp:albumArtURI dlna:profileID=\"%1\">%2</upnp:albumArtURI>")
        .arg(profile, escape(url));
}

QString resElement(const ElementFilter& filter, const QString& baseUrl, const DlnaItem& item)
{
    if (!filter.accepts(QStringLiteral("res")))
        return QString();
    QString attrs;
    if (item.size > 0)
        attrs += QStringLiteral(" size=\"%1\"").arg(item.size);
    const QString dur = durationString(item.durationSec);
    if (!dur.isEmpty())
        attrs += QStringLiteral(" duration=\"%1\"").arg(dur);
    return QStringLiteral("<res%1 protocolInfo=\"%2\">%3</res>")
        .arg(attrs, escape(protocolInfoForMime(item.mime)), escape(mediaUrl(baseUrl, item)));
}

// A same-basename .srt becomes an extra res (Samsung-friendly text/srt) plus a
// sec:CaptionInfoEx element pointing at the same URL (Kodi's subtitle emission, Samsung's
// published spelling).
QString subtitleElements(const ElementFilter& filter, const QString& baseUrl, const DlnaItem& item)
{
    if (!item.mime.startsWith(QLatin1String("video/")))
        return QString();
    if (subtitlePathFor(item).isEmpty())
        return QString();
    const QString url = baseUrl + QLatin1String("/sub/") + item.id + QLatin1String(".srt");
    QString out;
    if (filter.accepts(QStringLiteral("res")))
        out += QStringLiteral("<res protocolInfo=\"http-get:*:text/srt:*\">%1</res>")
                   .arg(escape(url));
    out += QStringLiteral("<sec:CaptionInfoEx>%1</sec:CaptionInfoEx>").arg(escape(url));
    return out;
}

// Metadata block shared by both element orders, following Platinum's ToDidl order; the plain
// upnp:class closes the object (after res), exactly like Platinum serializes it.
QString itemDidl(const ElementFilter& filter, const QString& baseUrl, const DlnaItem& item)
{
    QString out;
    out += QStringLiteral("<item id=\"%1\" parentID=\"%2\" restricted=\"1\">")
               .arg(escape(item.id), escape(item.parentId));
    out += QStringLiteral("<dc:title>%1</dc:title>").arg(escape(item.title));
    if (item.year > 0 && filter.accepts(QStringLiteral("dc:date")))
        out += QStringLiteral("<dc:date>%1-01-01</dc:date>").arg(item.year);
    if (!item.author.isEmpty()) {
        if (filter.accepts(QStringLiteral("dc:creator")))
            out += QStringLiteral("<dc:creator>%1</dc:creator>").arg(escape(item.author));
        if (filter.accepts(QStringLiteral("upnp:author")))
            out += QStringLiteral("<upnp:author>%1</upnp:author>").arg(escape(item.author));
    }
    if (!item.genres.isEmpty() && filter.accepts(QStringLiteral("upnp:genre")))
        out += QStringLiteral("<upnp:genre>%1</upnp:genre>").arg(escape(item.genres));
    if (!item.seriesTitle.isEmpty() && filter.accepts(QStringLiteral("upnp:seriesTitle")))
        out += QStringLiteral("<upnp:seriesTitle>%1</upnp:seriesTitle>").arg(escape(item.seriesTitle));
    if (item.episode > 0 && filter.accepts(QStringLiteral("upnp:episodeNumber")))
        out += QStringLiteral("<upnp:episodeNumber>%1</upnp:episodeNumber>").arg(item.episode);
    if (item.season >= 0 && !item.seriesTitle.isEmpty()
        && filter.accepts(QStringLiteral("upnp:episodeSeason")))
        out += QStringLiteral("<upnp:episodeSeason>%1</upnp:episodeSeason>").arg(item.season);
    if (!item.synopsis.isEmpty() && filter.accepts(QStringLiteral("dc:description")))
        out += QStringLiteral("<dc:description>%1</dc:description>").arg(escape(item.synopsis));
    out += albumArtElement(filter, baseUrl, item);
    out += resElement(filter, baseUrl, item);
    out += subtitleElements(filter, baseUrl, item);
    out += QStringLiteral("<upnp:class>%1</upnp:class>").arg(escape(item.upnpClass));
    out += QStringLiteral("</item>");
    return out;
}

QString containerDidl(const DlnaContainer& c, int childCount)
{
    return QStringLiteral(
               "<container id=\"%1\" parentID=\"%2\" restricted=\"1\" childCount=\"%3\">"
               "<dc:title>%4</dc:title>"
               "<upnp:class>%5</upnp:class>"
               "</container>")
        .arg(escape(c.id), escape(c.parentId))
        .arg(childCount)
        .arg(escape(c.title), escape(c.upnpClass));
}

bool isVideoMime(const QString& mime) { return mime.startsWith(QLatin1String("video/")); }
bool isAudioMime(const QString& mime) { return mime.startsWith(QLatin1String("audio/")); }
bool isImageMime(const QString& mime) { return mime.startsWith(QLatin1String("image/")); }

} // namespace

QString escape(const QString& text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar ch : text) {
        switch (ch.unicode()) {
        case '&': out += QStringLiteral("&amp;"); break;
        case '<': out += QStringLiteral("&lt;"); break;
        case '>': out += QStringLiteral("&gt;"); break;
        case '"': out += QStringLiteral("&quot;"); break;
        case '\'': out += QStringLiteral("&apos;"); break;
        default: out += ch;
        }
    }
    return out;
}

QString durationString(double seconds)
{
    if (seconds <= 0)
        return QString();
    const int total = int(seconds + 0.5);
    const int h = total / 3600;
    const int m = (total % 3600) / 60;
    const int s = total % 60;
    return QStringLiteral("%1:%2:%3.000")
        .arg(h)
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

QString contentFeaturesForMime(const QString& mime)
{
    // Images carry only their PN (Platinum's image rows have no flags and no OP); audio and
    // video are full streaming entries; everything else (epub/pdf/cbz/...) is a plain
    // range-capable download; unknown mimes fall back to the bare wildcard.
    if (isImageMime(mime))
        return QStringLiteral("DLNA.ORG_OP=01;DLNA.ORG_CI=0");
    if (isVideoMime(mime) || isAudioMime(mime))
        return kStreamingFlags;
    if (mime == QLatin1String("application/octet-stream"))
        return QStringLiteral("*");
    return QStringLiteral("DLNA.ORG_OP=01;DLNA.ORG_CI=0");
}

QString protocolInfoForMime(const QString& mime)
{
    return QStringLiteral("http-get:*:%1:%2").arg(mime, contentFeaturesForMime(mime));
}

QString thumbProfileForSuffix(const QString& suffixLower)
{
    if (suffixLower == QLatin1String("png"))
        return QStringLiteral("PNG_TN");
    if (suffixLower == QLatin1String("jpg") || suffixLower == QLatin1String("jpeg"))
        return QStringLiteral("JPEG_TN");
    return QString();
}

QString subtitlePathFor(const DlnaItem& item)
{
    if (!item.mime.startsWith(QLatin1String("video/")))
        return QString();
    const QFileInfo fi(item.filePath);
    const QFileInfo srt(QDir(fi.absolutePath()), fi.completeBaseName() + QLatin1String(".srt"));
    return srt.isFile() ? srt.absoluteFilePath() : QString();
}

BrowseResponse browse(const DlnaSnapshot& snapshot, const QString& baseUrl,
                      const BrowseRequest& request)
{
    BrowseResponse out;
    const ElementFilter filter(request.filter);

    const auto foundContainer = snapshot.containers.constFind(request.objectId);
    const bool isItem = foundContainer == snapshot.containers.constEnd()
                        && snapshot.items.contains(request.objectId);
    if (foundContainer == snapshot.containers.constEnd() && !isItem) {
        out.errorCode = 701; // no such object
        return out;
    }
    if (request.metadata) {
        out.ok = true;
        if (isItem) {
            out.didl = didlOpen()
                       + itemDidl(filter, baseUrl, snapshot.items.value(request.objectId))
                       + QStringLiteral("</DIDL-Lite>");
        } else {
            const DlnaContainer& c = foundContainer.value();
            out.didl = didlOpen()
                       + containerDidl(c, c.childContainerIds.size() + c.childItemIds.size())
                       + QStringLiteral("</DIDL-Lite>");
        }
        out.numberReturned = out.totalMatches = 1;
        return out;
    }
    if (isItem) {
        // Items have no children; an empty page is the honest answer.
        out.ok = true;
        out.didl = didlOpen() + QStringLiteral("</DIDL-Lite>");
        return out;
    }

    const DlnaContainer& container = foundContainer.value();
    QStringList entries; // "c<id>" / "i<id>" markers keep container-then-item order
    for (const QString& id : container.childContainerIds)
        entries << QLatin1Char('c') + id;
    for (const QString& id : container.childItemIds)
        entries << QLatin1Char('i') + id;
    if (request.sortDescending)
        std::reverse(entries.begin(), entries.end());
    out.totalMatches = quint32(entries.size());

    int count = request.requestedCount == 0 ? kMaxReturnedItems : int(request.requestedCount);
    count = qMin(count, kMaxReturnedItems);
    const int start = qMin(int(request.startingIndex), entries.size());
    const int end = qMin(start + count, entries.size());

    QString body;
    for (int i = start; i < end; i++) {
        const QString& marker = entries.at(i);
        if (marker.startsWith(QLatin1Char('c'))) {
            const DlnaContainer& child = snapshot.containers.value(marker.mid(1));
            // Real ChildCount: our index makes counting free (Kodi deliberately reports none).
            body += containerDidl(child, child.childContainerIds.size() + child.childItemIds.size());
        } else {
            body += itemDidl(filter, baseUrl, snapshot.items.value(marker.mid(1)));
        }
    }
    out.ok = true;
    out.didl = didlOpen() + body + QStringLiteral("</DIDL-Lite>");
    out.numberReturned = quint32(end - start);
    return out;
}

QStringList sourceProtocolInfos(const DlnaSnapshot& snapshot)
{
    QStringList out;
    QSet<QString> seen;
    for (auto it = snapshot.items.constBegin(); it != snapshot.items.constEnd(); ++it) {
        const QString pi = protocolInfoForMime(it->mime);
        if (!seen.contains(pi)) {
            seen.insert(pi);
            out.append(pi);
        }
    }
    return out;
}

} // namespace DlnaContentDirectory
