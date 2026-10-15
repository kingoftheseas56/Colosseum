#pragma once

#include <QString>
#include <QList>
#include <QVector>
#include <QDate>
#include <QDateTime>

// Pure value types for the native BiblioCatalog engine (Discover / Explore).
//
// Task 1 foundation only: no networking, no SQL, no QML. These are plain,
// deterministic value structs that the controlled-vocabulary taxonomy mapper
// (BiblioTaxonomy) and the ranking functions (BiblioRanking) operate on. Later
// tasks add providers, a canonicalizer, a SQLite store and a QML-facing service
// on top of these types — QML must never compute rankings itself.

// Average rating + how many ratings stand behind it. Both signals are needed so
// Top Rated can weigh confidence, not just the raw average.
struct BiblioRatingEvidence {
    double average = 0.0; // mean star rating, 0..5
    int    count   = 0;   // number of ratings behind `average`
};

// One concrete edition of a canonical work (a specific printing / format).
struct BiblioEdition {
    QString editionId;
    QString language;                 // language of THIS edition
    bool    englishReadable = false;  // true if this edition can be read in English
    int     pageCount = 0;            // 0 => no reliable pagination
    QString publisher;                // raw publisher / imprint string for this edition
    QDate   published;                // this edition's publication date (reprint / ebook / audio ...)
    QString format;                   // "print" | "ebook" | "audiobook" | ...
};

// A canonical work: the deduplicated title that many editions collapse into.
struct BiblioWork {
    QString canonicalId;              // stable canonical work id
    QString title;
    QString author;                   // representative primary author (for the card's author-at-rest)
    QString originalLanguage;         // language the work was first written in
    QDate   canonicalFirstPublished;  // earliest reliable first-publication date (NEVER a reprint)
    QString publisher;                // canonical / representative publisher (raw; normalized via taxonomy)
    QString coverUrl;                 // representative cover art (Apple-owned; Open Library fallback)
    BiblioRatingEvidence rating;

    // Popularity evidence. Each is a raw signal; the ranker normalizes it against
    // the active population at rank time. A zero here means "no evidence for that
    // signal", which drops only that signal — never the work.
    double appleChartScore       = 0.0; // Apple Books chart-performance signal
    double openLibraryPopularity = 0.0; // Open Library popularity signal

    QList<BiblioEdition> editions;
};

// A dated demand reading for one work, captured daily. Trending diffs two of
// these (>= 6 days apart) to measure honest seven-day momentum.
struct BiblioRankSnapshot {
    QString   canonicalId;
    QDateTime capturedAt;         // when this reading was taken
    double    demandScore = 0.0;  // composite chart-movement / rating-volume reading
};

// One controlled filter value inside an axis (e.g. key="science-fiction",
// label="Science Fiction").
struct BiblioFacet {
    QString key;
    QString label;
};

// One filter axis and the controlled values it advertises (e.g. axis="genre",
// label="Genre"). filterGroups() returns these.
struct BiblioFilterGroup {
    QString axis;
    QString label;
    QVector<BiblioFacet> facets;
};
