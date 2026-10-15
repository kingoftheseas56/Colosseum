// native/engine/ComicCoverProvider.h
//
// Task 3 (2026-08-06 comics CBZ-in-place arc): a cover thumbnail for an
// archive-shaped comic row has no loose page file to point at (that was the
// whole point of Task 2). This provider decodes one straight from the CBZ
// instead -- image://comiccover/<base64url(archivePath)>/<base64url(entryName)>.
//
// Fully STATELESS by design: it never holds a pointer to ComicDownloader or
// touches its m_index. Quick can call requestImage() off the GUI thread, so a
// stateful provider reaching into ComicDownloader would be a real
// GUI-thread/pool-thread race, not a theoretical one -- the self-contained
// URL sidesteps it entirely (same reasoning as Player2SubtitleImageProvider's
// weak-handle note, applied by removing the handle altogether).
//
// Synchronous (QQuickImageProvider::Image), unlike ComicReaderProvider's own
// QQuickAsyncImageProvider: that one exists for the reader's high-frequency
// page-turn path, where a blocking decode on Qt's image thread would stall
// every other pending request behind it. This provider serves the occasional
// library-grid thumbnail load -- a handful of requests, not a page-turn
// stream -- so a plain synchronous decode is the right-sized tool, not a
// smaller version of the reader's problem.
//
// Legacy `dir`-shaped rows are untouched by this provider -- they keep
// emitting a plain file:// URL from ComicDownloader::downloadedIssues(),
// unchanged. No UX gap mid-migration: only archive rows (Task 4+) ever
// produce an image://comiccover/ URL at all.
//
// The id itself (build/parse) lives in ComicCoverId.h, Qt6::Core only --
// ComicDownloader.cpp is the other caller (its downloadedIssues() builds the
// art URL for an archive row) and compiles into four Core/Network-only
// harness targets that have no other reason to link Qt6::Gui/Qt6::Quick or
// pull in CbzArchive.cpp/miniz.c.
#pragma once

#include <QImage>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>

namespace Colosseum {

class ComicCoverProvider final : public QQuickImageProvider
{
public:
    ComicCoverProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    // Decodes archivePath's entryName via CbzArchive::readEntry(), scaled to
    // requestedSize (or a bounded default when the caller doesn't specify
    // one) with QImageReader::setScaledSize() -- a 200px grid tile never pays
    // for a full multi-megapixel page decode. Never upscales a source
    // smaller than the target box. Returns a null QImage (and clears *size)
    // for any malformed id, missing archive, or missing/undecodable entry --
    // never throws, never crashes.
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

} // namespace Colosseum
