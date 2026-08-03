#pragma once

#include "BiblioCatalogTypes.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <QList>

// Versioned controlled-vocabulary mapper (spec 6.3) plus the pure facet-key
// helpers behind the Discover / Explore filters (spec 4.3).
//
// Every key produced here is a stable lowercase identifier. The mapper merges
// synonyms, case, spelling and singular/plural variants, folds imprints under
// their parent publisher, and NEVER turns an unknown provider string into a
// visible filter. The curated tables live in the .cpp so they are checked-in
// and testable — the harness is the oracle for these tables.
namespace BiblioTaxonomy {

// Curated-publisher coverage floor: a normalized publisher only becomes a
// visible filter once at least this many canonical works map to it.
inline constexpr int kPublisherCoverageFloor = 25;

// The controlled filter axes and the values each advertises. This is the single
// source of truth the QML filter UI renders; the value keys here must stay
// consistent with what the helper functions below return.
QVector<BiblioFilterGroup> filterGroups();

// Map a raw provider tag on `axis` to its stable canonical key, or an empty
// string when the tag is unknown / unsupported (ambiguous, noisy or sparse tags
// stay hidden). Recognized axes: "genre", "audience", "theme", "setting",
// "period", "publisher". Any other axis yields "".
QString normalize(const QString &axis, const QString &raw);

// Length bucket from the representative English edition's page count:
// Short <200, Standard 200-499, Long 500-799, Epic 800+. pages <= 0 => "" (no
// reliable pagination => the work enters no Length result).
QString lengthKey(int pages);

// Publication-era bucket from the earliest reliable publication year. year <= 0
// => "" (unknown year => the work enters no Era result).
QString eraKey(int year);

// English vs Translated. An English original is "english"; a work originally in
// another language WITH an English-readable edition is "translated"; a work
// originally in another language WITHOUT an English edition is "" (it is outside
// the English-readable catalogue entirely).
// Contract: `originalLanguage` is expected to be a canonicalized ISO-ish token
// (english/en/eng); the Task 2 canonicalizer guarantees these normalized tokens.
QString languageKey(const QString &originalLanguage, bool englishEditionAvailable);

// The normalized publishers that clear kPublisherCoverageFloor across `works`,
// sorted ascending. Imprints are folded under their parent before counting, and
// unknown publisher strings are never counted.
QStringList curatedPublishers(const QList<BiblioWork> &works);

} // namespace BiblioTaxonomy
