#pragma once
// DlnaContentDirectory — the Kodi-style AV logic of the DLNA server (Arc 71): DIDL-Lite
// generation from an immutable DlnaSnapshot, ContentDirectory Browse (metadata/direct
// children, paging, filters, the two sort criteria we actually honour), caps strings and
// ConnectionManager answers. Pure functions of (snapshot, baseUrl): no pupnp types here, no
// QObject, callable from any thread — the SOAP glue in DlnaServer runs on pupnp workers.

#include "DlnaSnapshot.h"

#include <QString>
#include <QStringList>

namespace DlnaContentDirectory {

// XML-escape a text/attribute value (& < > " ').
QString escape(const QString& text);

// Kodi's res@duration format: H:MM:SS.000 (hours unpadded). Empty when seconds <= 0.
QString durationString(double seconds);

// Full res protocolInfo for an item: http-get:*:<mime>:<4th field>. OP=01 (byte ranges) and
// CI=0 always; audio/video carry the 01500000... streaming+background flags; non-media
// documents are plain downloads; unknown mimes get the bare "*".
QString protocolInfoForMime(const QString& mime);

// The 4th-field value used both in res@protocolInfo and in contentFeatures.dlna.org.
QString contentFeaturesForMime(const QString& mime);
// DLNA.ORG_PN for a thumbnail by image extension (empty when unknown).
QString thumbProfileForSuffix(const QString& suffixLower);

// Path of a same-basename .srt beside a video, when one exists ("" otherwise).
QString subtitlePathFor(const DlnaItem& item);

struct BrowseRequest {
    QString objectId;
    bool metadata = false;      // BrowseMetadata vs BrowseDirectChildren
    QString filter;             // "*" / "" / comma list
    quint32 startingIndex = 0;
    quint32 requestedCount = 0; // 0 = as many as allowed
    bool sortDescending = false;
};

struct BrowseResponse {
    QString didl;               // full <DIDL-Lite>...</DIDL-Lite> document
    quint32 numberReturned = 0;
    quint32 totalMatches = 0;
    bool ok = false;            // false + errorCode below on failure
    int errorCode = 0;          // 402 / 701 / 709
};

// Browse per Kodi behaviour (section 6 of the reference). Unknown ids -> 701, bad args ->
// 402, unapplied sort criteria -> 709. RequestedCount 0 returns everything up to the cap.
BrowseResponse browse(const DlnaSnapshot& snapshot, const QString& baseUrl,
                      const BrowseRequest& request);

// The distinct protocolInfos the current snapshot serves — ConnectionManager Source list.
QStringList sourceProtocolInfos(const DlnaSnapshot& snapshot);

// The highest page size a BrowseDirectChildren may return (Kodi caps at 200; ours is larger
// because the whole projection is an in-memory hash walk).
inline constexpr int kMaxReturnedItems = 500;

} // namespace DlnaContentDirectory
