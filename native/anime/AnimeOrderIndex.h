#pragma once

// AnimeOrderIndex — pure, immutable parser + resolver for keyless anime ordering.
//
// It owns no network, files, threads, or QML objects. It parses the Fribb
// cross-provider identity list (JSON) and the Anime-Lists AniDB↔TVDB mapping
// list (XML) into immutable lookup tables, then answers a single question:
// given the identities a caller honestly possesses and the provider's episode
// rows, what is the canonical annotation of those rows — without ever inventing,
// dropping, or replacing a provider stream ID.
//
// Construct only via fromSources(); the result is shared and const so any number
// of threads/surfaces may resolve against one generation safely.

#include <QByteArray>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

class AnimeOrderIndex final {
public:
    // Parse both sources into an immutable index. Returns nullptr and sets
    // *error (deterministic, non-empty) on malformed JSON, a non-`anime-list`
    // XML root, duplicate AniDB records, or a source with zero usable entries.
    static std::shared_ptr<const AnimeOrderIndex> fromSources(
        const QByteArray& fribbJson, const QByteArray& animeListXml, QString* error);

    // Resolve an identity candidate set to at most one AniDB work and annotate
    // the provider episode rows. Every input row and its stream ID survive.
    QVariantMap resolve(const QVariantMap& identities,
                        const QVariantList& providerEpisodes) const;

    // Number of Fribb-backed identity entries in this index.
    int entryCount() const;

private:
    struct Data;
    explicit AnimeOrderIndex(std::shared_ptr<const Data> data);
    std::shared_ptr<const Data> m_data;
};
