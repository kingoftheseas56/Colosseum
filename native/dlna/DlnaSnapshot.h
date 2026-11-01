#pragma once
// DlnaSnapshot - the immutable contract between DlnaLibrary (Qt owner thread) and the pupnp
// server threads. DlnaLibrary builds one of these from the Vault + audiobook owners and
// publishes it as std::shared_ptr<const DlnaSnapshot>; server threads only ever read it.
// Plain data, QtCore only. No file path in here is ever sent to a client (filePath is the
// server's own handle to the bytes; every id is opaque).

#include <QHash>
#include <QString>
#include <QStringList>
#include <QtGlobal>

struct DlnaItem {
    QString id;          // opaque, stable: "i" + first 16 hex of sha1(vault id or source key)
    QString parentId;    // container id
    QString title;
    QString upnpClass;   // object.item.videoItem.movie | object.item.videoItem |
                         // object.item.audioItem.audioBook | object.item.textItem (books)
                         // | object.item (comics/other files)
    QString filePath;    // absolute; never sent to clients
    QString mime;        // truthful, by extension
    qint64  size = 0;    // from a fresh stat at snapshot time
    qint64  mtimeMs = 0; // from the same stat; identity check at open: size+mtime must still match
    double  durationSec = -1;
    int     season = 0, episode = 0, year = 0;
    QString seriesTitle; // shows
    QString genres;      // display string
    QString synopsis;
    QString author;      // books/comics/audiobooks
    QString coverPath;   // local image path if cheaply known, else empty

    bool operator==(const DlnaItem& o) const
    {
        return id == o.id && parentId == o.parentId && title == o.title && upnpClass == o.upnpClass
            && filePath == o.filePath && mime == o.mime && size == o.size && mtimeMs == o.mtimeMs
            && durationSec == o.durationSec && season == o.season && episode == o.episode
            && year == o.year && seriesTitle == o.seriesTitle && genres == o.genres
            && synopsis == o.synopsis && author == o.author && coverPath == o.coverPath;
    }
};

struct DlnaContainer {
    QString id, parentId, title;
    QString upnpClass;   // object.container.storageFolder (or .album.* / .person.* where natural)
    QStringList childContainerIds;
    QStringList childItemIds;

    bool operator==(const DlnaContainer& o) const
    {
        return id == o.id && parentId == o.parentId && title == o.title && upnpClass == o.upnpClass
            && childContainerIds == o.childContainerIds && childItemIds == o.childItemIds;
    }
};

struct DlnaSnapshot {
    quint32 systemUpdateId = 0;               // increments on every published change
    QHash<QString, DlnaContainer> containers; // "0" is root (always present, even when empty)
    QHash<QString, DlnaItem> items;
    QStringList typeOrder;                    // non-empty top-level types in order
    int itemCount = 0;

    // Content equality, ignoring systemUpdateId. DlnaLibrary bumps the id only when this is false.
    bool sameContent(const DlnaSnapshot& o) const
    {
        return itemCount == o.itemCount && typeOrder == o.typeOrder
            && containers == o.containers && items == o.items;
    }
};
