#pragma once

#include "BiblioCatalogTypes.h"

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

// Keyless provider parsing (spec 6.1) for the BiblioCatalog Discover/Explore
// engine. Two sources, no API keys, accounts, or tokens:
//
//   * Apple Books — the live ebook chart RSS (chart position, artwork, current
//     edition identity) and the iTunes Search API (ratings, descriptions,
//     genres, release activity, ebook/audiobook forms).
//   * Open Library — the search.json work/edition index (canonical work key,
//     ISBN/authority ids, first-publication year, authors, page counts,
//     language & English-edition evidence, publishers, subjects).
//
// Every parser is DEFENSIVE: missing artwork/rating, HTML descriptions, an RSS
// `entry` that is a single object OR an array, and malformed/partial records
// are all tolerated without crashing. Each parser produces per-source
// BiblioSourceRecord values carrying source/sourceId/observedAt provenance plus
// the raw evidence the canonicalizer (BiblioCanonicalizer) reconciles into
// canonical works. No networking lives here — the URL builders return the
// keyless request URLs a caller's transport fetches.

// One record as observed from a single provider before canonicalization. It
// carries its own provenance (source/sourceId/observedAt) and every field the
// layered identity resolver needs. This is a NEW Task-2 input type; the
// canonical output types (BiblioWork/BiblioEdition) live in BiblioCatalogTypes.h.
struct BiblioSourceRecord {
    // ── provenance ──
    QString   source;      // "apple" | "openlibrary"
    QString   sourceId;    // provider's stable id (Apple adam/track/collection id; OL work key)
    QDateTime observedAt;  // when this record was parsed/fetched

    // ── identity evidence (layered resolution, strongest first) ──
    QString     workKey;          // Open Library work key ("/works/OL...W"); empty for Apple
    QStringList isbns;            // ISBN-10/13 authority identifiers (Open Library)
    QStringList authorKeys;       // Open Library author keys (authority identifiers)
    QString     title;            // raw title as the source presented it
    QString     author;           // raw primary author
    QString     normalizedTitle;  // folded title for the title+author identity layer
    QString     normalizedAuthor; // folded primary author

    // ── publication / language evidence ──
    int     firstPublishYear = 0;   // Open Library first_publish_year (0 => unknown)
    QDate   published;              // this record's edition publication date
    QString language;               // normalized language token for THIS record ("en"/"fr"/...)
    bool    englishReadable = false;// this record is an English-readable edition

    // ── edition descriptors ──
    QString format;             // "print" | "ebook" | "audiobook"
    int     pageCount = 0;      // 0 => no reliable pagination
    QString publisher;          // raw publisher / imprint for this edition

    // ── Apple-owned signals ──
    double appleChartScore = 0.0;   // chart-performance signal (Apple RSS; 0 => not charting)
    bool   hasRating = false;       // whether this record carried a storefront rating
    BiblioRatingEvidence rating;    // Apple storefront rating + count
    QString artworkUrl;             // Apple cover art URL

    // ── Open Library-owned signals ──
    double openLibraryPopularity = 0.0; // Open Library popularity signal (0 => none)

    // ── shared descriptive metadata ──
    QString description;        // HTML-stripped description / synopsis
};

namespace BiblioProviders {

// Fold a title/author for the title+author identity layer: lowercase, drop
// parentheticals ("(Unabridged)"/"(Abridged)"), '&' -> " and ", strip other
// punctuation, collapse. Mirrors qml/BiblioApi.js pairKey so an ebook and its
// "(Unabridged)" audiobook fold to the same identity. Shared by the parsers and
// the canonicalizer so the identity fold has ONE definition (divergence would
// silently break edition merging).
QString foldTitleAuthor(const QString &raw);

// Parse the Apple Books "top ebooks" RSS feed JSON. Handles `feed.entry` as a
// single object (one result) OR an array (many). Missing artwork yields an empty
// artworkUrl; a missing title drops that entry. `observedAt` stamps provenance
// (defaults to now-UTC when not supplied).
QList<BiblioSourceRecord> parseAppleRss(const QByteArray &bytes,
                                        const QDateTime &observedAt = QDateTime());

// Parse an iTunes Search API JSON payload (media=ebook or media=audiobook).
// Strips HTML from descriptions, tolerates missing ratings/artwork, and skips
// results with no usable title. Audiobook wrappers become audiobook-format
// editions; everything else is an ebook-format edition.
QList<BiblioSourceRecord> parseAppleSearch(const QByteArray &bytes,
                                           const QDateTime &observedAt = QDateTime());

// Parse an Open Library search.json payload. Reads work key, ISBNs, authors,
// first-publication year, language(s), median page count, publisher and
// description (string OR {type,value} object). Missing fields are tolerated.
QList<BiblioSourceRecord> parseOpenLibrarySearch(const QByteArray &bytes,
                                                 const QDateTime &observedAt = QDateTime());

// ── Keyless request URL builders (no api_key/token/account ever) ──

// Apple "top ebooks" chart RSS. genreId 0 => the global chart; a positive id
// selects a genre chart (see BiblioGenreApi genre ids).
QUrl appleTopEbooksRssUrl(const QString &country = QStringLiteral("us"),
                          int limit = 100, int genreId = 0);

// iTunes Search API URL for `term` in the given media ("ebook" | "audiobook").
QUrl appleSearchUrl(const QString &term, const QString &media = QStringLiteral("ebook"),
                    int limit = 24, const QString &country = QStringLiteral("us"));

// Open Library search.json URL for a title (and optional author).
QUrl openLibrarySearchUrl(const QString &title, const QString &author = QString(),
                          int limit = 10);

} // namespace BiblioProviders
