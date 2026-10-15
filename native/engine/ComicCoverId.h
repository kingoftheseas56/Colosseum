// native/engine/ComicCoverId.h
//
// The id half of image://comiccover/<id> (Task 3, 2026-08-06 CBZ-in-place
// arc) -- pure QString/QByteArray, Qt6::Core only. Split out of
// ComicCoverProvider.{h,cpp} on an Opus-advisor review: ComicDownloader.cpp
// is the only OTHER caller (its downloadedIssues() builds the art URL for an
// archive row), and it compiles into four harness targets that have no other
// reason to link Qt6::Gui/Qt6::Quick or pull in CbzArchive.cpp/miniz.c --
// buildId() being a string function living in the QQuickImageProvider
// translation unit dragged all of that into every one of them.
#pragma once

#include <QString>

namespace Colosseum {

// Base64url-encodes archivePath and entryName into the id half of
// "image://comiccover/<id>" -- no '+', '/', or padding in the output, so an
// archive path containing anything (spaces, parentheses, unicode) can never
// collide with the '/' this function uses as the segment separator.
QString buildComicCoverId(const QString& archivePath, const QString& entryName);

// The inverse: splits id on its first '/' and decodes both segments. Returns
// false (and leaves both out-params empty) for anything malformed -- no
// slash, an empty segment, or an empty id -- so callers never need to
// re-derive what "malformed" means.
bool parseComicCoverId(const QString& id, QString* archivePath, QString* entryName);

} // namespace Colosseum
