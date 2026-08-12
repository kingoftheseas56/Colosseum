#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

class ComicsCatalog;
class ImdbCatalog;
class MalCatalog;
class VaultIndex;

// The Vault's certainty gate. This class owns the small, reversible identity
// transaction, while VaultIndex remains the durable source of truth.
class VaultIdentifier final : public QObject
{
    Q_OBJECT

public:
    struct Match {
        bool adopted = false;
        QString source;
        QString sourceId;
        QString title;
        QString synopsis;
        QString coverUrl;
        QString world;
        int year = 0;
        // Slice 2 (browse-face execution plan): the total exact-candidate count from the last
        // matchGroup() catalogue lookup (comics+MAL, or IMDB alone) — 0 when no catalogue lookup
        // ran at all (e.g. the book path, or an ineligible group). >1 means the exactly-one
        // certainty gate correctly declined adoption for AMBIGUITY, not absence — the fact
        // recordAmbiguous() makes durable.
        int candidateCount = 0;
    };

    explicit VaultIdentifier(VaultIndex* index, ComicsCatalog* comics,
                             MalCatalog* mal, ImdbCatalog* imdb,
                             QObject* parent = nullptr);

    Match matchGroup(const QString& groupKey) const;
    // Apply the same certainty gate to every existing group. The caller owns scheduling;
    // this method is a synchronous, testable pass over the current index snapshot.
    int autoIdentifyExisting();
    bool applyGroup(const QString& groupKey, const Match& match);
    // Explicit manual selection: bypasses the auto-certainty matcher but retains the
    // same decorate-only write, preserving every file id and progress field.
    bool identifyGroupWith(const QString& groupKey, const Match& match);
    bool unidentifyGroup(const QString& groupKey);
    bool reshelveGroup(const QString& groupKey, const QString& kind);
    // Slice 2 (browse-face execution plan): record a durable "Vault isn't sure" fact when
    // matchGroup() found MORE THAN ONE exact catalogue candidate (auto-adoption correctly
    // declined by the exactly-one certainty gate — this does not change that gate). Never
    // overwrites an already-adopted or user-suppressed group. Owner-thread, decorate-only write
    // — same shape as applyGroup/unidentifyGroup; no independent off-thread SQLite writes.
    bool recordAmbiguous(const QString& groupKey, int candidateCount);

private:
    VaultIndex* m_index = nullptr;
    ComicsCatalog* m_comics = nullptr;
    MalCatalog* m_mal = nullptr;
    ImdbCatalog* m_imdb = nullptr;
};
