#pragma once

#include <QString>
#include <QStringList>

// Plans the identity queries fanned out through Tankorent's universal comics
// filter for a single collected edition. Pure logic, no I/O — the canonical
// edition title, ISBN, and collected-range variants, deduplicated by a
// case/punctuation/whitespace-folded key while preserving human-readable form.
class ComicTorrentQueryPlanner
{
public:
    static QStringList automaticQueries(const QString& seriesTitle,
                                        const QString& editionTitle,
                                        const QString& isbn,
                                        const QString& collects);
    static QStringList manualQuery(const QString& query);
};
