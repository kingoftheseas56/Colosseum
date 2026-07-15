#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Pure identity module for the Tankorent Comic volume-mode feature. Turns a
// GCD catalog collected edition (chId, series, title, format, ISBN, collects)
// into a canonical match target consumed by later ranking/selection code.
// No network, no Qt GUI, no file I/O.
namespace ComicEditionIdentity {

enum class ComicCollectionFormat {
    Unknown,
    Compendium,
    Omnibus,
    TradePaperback,
    Deluxe,
    Absolute,
    Hardcover,
    Collection,
    Volume,
    Book
};

struct ComicIssueRef {
    QString series;
    int number = -1;
};

struct ComicEditionTarget {
    QString editionId;       // existing ledger chId
    QString seriesId;
    QString seriesTitle;
    QString editionTitle;
    ComicCollectionFormat format = ComicCollectionFormat::Unknown;
    int ordinal = -1;        // collection number; -1 for named one-shots
    QString isbnDigits;
    QList<ComicIssueRef> collectedIssues; // canonical order, deduplicated
    // True only when every collected-issue fragment parsed cleanly. The
    // issue-range acquisition path is eligible ONLY when this is true — a
    // partial parse must never let a subset of issues download automatically.
    bool collectedIssuesComplete = false;
    // True when the explicit catalog format and a format detected inside the
    // edition title disagree. The target keeps its (explicit) `format` value
    // but is not eligible for automatic format matching downstream.
    bool formatAmbiguous = false;
};

struct CollectedIssues {
    QList<ComicIssueRef> issues;
    bool complete = false;
    QStringList diagnostics;
};

// Canonical name <-> enum helpers.
ComicCollectionFormat parseFormat(const QString& text);
QString formatName(ComicCollectionFormat format);

// Ordinal parsing scoped to the given format's recognized token(s): #1,
// No. 1, Vol 01, worded "Book One".."Book Twenty", Roman numerals I..XX, and
// a trailing bare number immediately adjacent to the token. A bare number
// elsewhere in the title (e.g. a year) is never treated as an ordinal.
int parseOrdinal(const QString& title, ComicCollectionFormat format);

// Splits `collects` on commas/semicolons, recognizes single issues (#0) and
// ranges (#14-16), and supports multiple named series in one string. A
// "Series #n" / "Series #a-b" fragment switches the current series; a bare
// "#n" continues the current series (starting from `seriesTitle`). Fragments
// that cannot be parsed into issue evidence are reported in `diagnostics`;
// `complete` is true only when every fragment parsed cleanly.
CollectedIssues parseCollectedIssues(const QString& seriesTitle, const QString& collects);

// Builds the canonical match target from the catalog fields already
// available in ComicDbLedger.qml. The explicit catalog format wins when it
// maps cleanly; a title-derived format only fills an Unknown catalog value.
ComicEditionTarget buildTarget(const QString& editionId,
                                const QString& seriesId,
                                const QString& seriesTitle,
                                const QString& editionTitle,
                                const QString& catalogFormat,
                                const QString& isbn,
                                const QString& collects);

} // namespace ComicEditionIdentity
