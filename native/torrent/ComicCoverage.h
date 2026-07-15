#pragma once

#include "ComicEditionIdentity.h"

#include <QList>
#include <QString>

// Pure format-aware coverage grammar for the Tankorent Comic volume-mode
// feature. Reads a torrent/file/dir name and reports which collection
// formats + inclusive number-ranges it advertises, so "Compendiums v01-v03"
// is recognized as covering Compendium 1..3 but never TPB 1 or issue 1.
// No network, no Qt GUI, no file I/O.
namespace ComicCoverage {

struct ComicCoverageSpan {
    ComicEditionIdentity::ComicCollectionFormat format = ComicEditionIdentity::ComicCollectionFormat::Unknown;
    int lo = -1;
    int hi = -1;
    QString evidenceText;
};

// Splits `text` on commas/semicolons/parentheses into clauses. Inside each
// clause, a number/range is bound only to the CLOSEST PRECEDING recognized
// format token in that same clause; a format token in one clause never
// binds a number in another. A generic `v01`/`Volume 1` with no other
// format token in its clause yields `Volume`, never `Compendium`/`TradePaperback`.
// A bare number with no preceding format token in its clause (e.g. a loose
// issue number) produces no span.
QList<ComicCoverageSpan> detectComicCoverage(const QString& text);

// True iff some span has the same format AND lo <= ordinal <= hi. Format
// equality is required — coverage never crosses collection formats.
bool coverageCovers(const QList<ComicCoverageSpan>& spans,
                     ComicEditionIdentity::ComicCollectionFormat format,
                     int ordinal);

} // namespace ComicCoverage
