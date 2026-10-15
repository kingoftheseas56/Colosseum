#pragma once

// Arc 18 M4 — indexing coordinator. Plainly: it takes one discovered Nyaa
// candidate plus that candidate's .torrent BYTES (already fetched by the
// caller — the indexer owns no network), decodes the real file list through
// the metainfo seam, verifies the decoded infoHash against the candidate, and
// persists ONLY exact isolable archive mappings into MangaTorrentIndex.
//
// Flow per candidate (guides/IMPLEMENTATION-PLAN.md M4):
//   verify infoHash -> upsert torrent row -> persist raw file list
//   -> per archive: solve volume identity with the ONE shared grammar
//   -> one mapping row per (volumeId, infoHash, fileIndex)
//
// Honesty rules pinned here (contract §3/§6/§7):
//   * a Nyaa TITLE claim alone never becomes a mapping — only real file
//     entries parsed by the shared grammar do;
//   * a combined multi-volume archive ("Volumes 1-12.cbz") is a rejected
//     diagnostic, never twelve mappings;
//   * two archives claiming the same canonical volume are ambiguous — NEITHER
//     maps;
//   * named/special volumes map only when the canonical series volume list
//     contains the exact folded token (fail closed otherwise);
//   * a decoded infoHash that disagrees with the candidate fails the whole
//     candidate before anything persists.
//
// Rejected outcomes are audit STRINGS in IndexerOutcome, not mapping rows —
// the store keeps only real identity (positive or runtime-flagged), so it
// never has to outrank speculative negatives (contract §6 "if useful" — it
// isn't; the diagnostics carry the audit).
//
// The resolver enters through the IMangaTorrentMetainfoResolver SEAM, so this
// class compiles without libtorrent — the production resolver plugs in beside
// the real engine, and libtorrent-free harnesses can inject a fake.

#include "engine/MangaTankobanTypes.h"
#include "torrent/IMangaTorrentMetainfoResolver.h"
#include "torrent/MangaTorrentIndex.h"
#include "torrent/MangaNyaaSource.h"

#include <QStringList>

namespace MangaTankoban {

struct IndexerOutcome {
    int verifiedCount = 0;   // verified mapping rows persisted this pass
    int rejectedCount = 0;   // combined / ambiguous / named-fail-closed events
    QStringList diagnostics; // human-readable audit trail (also the why-not)
};

class MangaTorrentIndexer
{
public:
    MangaTorrentIndexer(IMangaTorrentMetainfoResolver* resolver, MangaTorrentIndex* index);

    // Index one candidate's metainfo. Returns false ONLY on hard failure
    // (bytes undecodable, infoHash mismatch, store write failed) — nothing
    // verified persists in that case. A torrent whose archives are all
    // combined/ambiguous/unparsed is a SUCCESS with diagnostics: "nothing
    // honestly mappable here" is an indexed answer, not an error.
    bool indexCandidate(const SeriesSnapshot& series,
                        const MangaNyaaCandidate& candidate,
                        const QByteArray& torrentBytes,
                        qint64 nowMs,
                        IndexerOutcome* outcome = nullptr);

private:
    IMangaTorrentMetainfoResolver* m_resolver = nullptr; // not owned (seam)
    MangaTorrentIndex* m_index = nullptr;                // not owned (store)
};

} // namespace MangaTankoban
