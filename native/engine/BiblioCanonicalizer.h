#pragma once

#include "BiblioCatalogTypes.h"
#include "BiblioProviders.h"

#include <QDateTime>
#include <QList>
#include <QString>

// Canonicalization (spec 6.2): reconcile per-source BiblioSourceRecord evidence
// into canonical BiblioWork values with nested editions and per-field
// provenance.
//
// Identity is resolved by LAYERED evidence, strongest first:
//   1. Open Library work key,
//   2. ISBN / other authority identifiers,
//   3. normalized title + author,
//   4. original & edition publication dates,
//   5. language & translator evidence,
//   6. publisher & edition notes.
// Title-only equality can NEVER silently merge two works: the same title by two
// different authors stays two works. Ordinary format changes (ebook/print/audio)
// remain editions nested under ONE canonical work; a materially different
// translation keeps its own edition metadata but still routes through the
// canonical work with the original language retained.
//
// Source responsibilities are honored on merge (spec 6.1): Open Library governs
// work identity and earliest-publication evidence; Apple governs the Apple
// chart, Apple ratings, and Apple artwork / current storefront activity. Every
// merged field keeps the source, sourceId and observedAt it came from.

// Provenance for one canonical field: which source set it, that source's record
// id, and when it was observed.
struct BiblioFieldSource {
    QString   field;      // canonical field name ("title","originalLanguage","firstPublished","publisher","rating","appleChartScore","openLibraryPopularity")
    QString   source;     // "apple" | "openlibrary"
    QString   sourceId;   // the winning record's sourceId
    QDateTime observedAt; // when that record was observed
};

// One canonical work plus the provenance of each field that was populated. The
// nested editions live on `work.editions`.
struct BiblioCanonicalWork {
    BiblioWork work;
    QList<BiblioFieldSource> fieldSources;
};

namespace BiblioCanonicalizer {

// Reconcile `records` (from any mix of Apple RSS, Apple Search and Open Library)
// into canonical works. The result is ordered deterministically by canonicalId.
QList<BiblioCanonicalWork> merge(const QList<BiblioSourceRecord> &records);

} // namespace BiblioCanonicalizer
