#pragma once

#include <QObject>
#include <QString>

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
    };

    explicit VaultIdentifier(VaultIndex* index, ComicsCatalog* comics,
                             MalCatalog* mal, ImdbCatalog* imdb,
                             QObject* parent = nullptr);

    Match matchGroup(const QString& groupKey) const;
    // Apply the same certainty gate to every existing group. The caller owns scheduling;
    // this method is a synchronous, testable pass over the current index snapshot.
    int autoIdentifyExisting();
    bool applyGroup(const QString& groupKey, const Match& match);
    bool unidentifyGroup(const QString& groupKey);
    bool reshelveGroup(const QString& groupKey, const QString& kind);

private:
    VaultIndex* m_index = nullptr;
    ComicsCatalog* m_comics = nullptr;
    MalCatalog* m_mal = nullptr;
    ImdbCatalog* m_imdb = nullptr;
};
