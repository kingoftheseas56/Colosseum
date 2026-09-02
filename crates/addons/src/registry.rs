//! Add-on registry: install-priority ordering, the [`Addon`] trait, the two
//! seeded deterministic fake add-ons, and the aggregation that produces the
//! `GET /catalog/series/{id}/sources` wire shape.
//!
//! Everything here is offline and deterministic. The seeded fakes serve
//! in-repo fixture JSON embedded at compile time (`fixtures/*.json`); the live
//! HTTP-backed Torrentio client slots in later behind an `ADDONS_LIVE` flag
//! (see the crate-level docs) and is deliberately not built here.

use std::collections::{BTreeMap, HashSet};
use std::sync::Arc;

use serde::Serialize;

use crate::manifest::Manifest;
use crate::rank::{self, Kind, RankedStream};
use crate::stream::Stream;

/// A stream add-on. Deterministic and offline for this slice.
///
/// The live client will need an async body (network I/O); this trait is sync
/// because the seeded fakes are pure in-memory. The `ADDONS_LIVE` slice will
/// either split this into an async companion trait or wrap the HTTP call in a
/// blocking runtime — a decision for that slice, not this one.
pub trait Addon: Send + Sync {
    fn id(&self) -> &str;
    fn manifest(&self) -> &Manifest;
    fn streams(&self, media_type: &str, id: &str) -> Vec<Stream>;
}

/// Installed add-ons, in install (priority) order. Priority is the index at
/// install time — the JS "ask order".
pub struct Registry {
    entries: Vec<Entry>,
}

struct Entry {
    id: String,
    name: String,
    priority: usize,
    addon: Arc<dyn Addon>,
}

/// One installed add-on descriptor, in install (priority) order.
#[derive(Clone, Debug, Serialize)]
pub struct InstalledAddon {
    pub id: String,
    pub name: String,
    pub priority: usize,
}

/// One source row in `GET /catalog/series/{id}/sources`.
///
/// Direct rows carry `url`; torrent rows carry `info_hash` + `file_idx`. The
/// `id` is the JS `_rowKey` (`u:<url>` / `t:<infoHash>:<fileIdx>`) and is
/// stable across restarts.
#[derive(Clone, Debug, Serialize)]
pub struct Candidate {
    pub id: String,
    pub addon: String,
    pub kind: Kind,
    pub label: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub quality: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub url: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub info_hash: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub file_idx: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub size_bytes: Option<u64>,
}

/// The full `/sources` response body.
#[derive(Clone, Debug, Serialize)]
pub struct Sources {
    pub candidates: Vec<Candidate>,
    pub counts_by_addon: BTreeMap<String, usize>,
    pub installed_addons: Vec<InstalledAddon>,
}

impl Default for Registry {
    fn default() -> Self {
        Self::seeded()
    }
}

impl Registry {
    /// The empty registry.
    pub fn new() -> Self {
        Self {
            entries: Vec::new(),
        }
    }

    /// The seeded registry: Torrentio-like (priority 0) then NoTorrent-like
    /// (priority 1). Deterministic, offline, no network.
    pub fn seeded() -> Self {
        Self::new()
            .install(FakeAddon::torrentio())
            .install(FakeAddon::notorrent())
    }

    /// Install one add-on at the next priority (install order).
    pub fn install(mut self, addon: impl Addon + 'static) -> Self {
        let priority = self.entries.len();
        let id = addon.id().to_string();
        let name = addon.manifest().name.clone();
        self.entries.push(Entry {
            id,
            name,
            priority,
            addon: Arc::new(addon),
        });
        self
    }

    /// The installed descriptors, in install (priority) order.
    pub fn installed(&self) -> Vec<InstalledAddon> {
        self.entries
            .iter()
            .map(|e| InstalledAddon {
                id: e.id.clone(),
                name: e.name.clone(),
                priority: e.priority,
            })
            .collect()
    }

    /// Ask every installed add-on that accepts this stream resource, in
    /// install order, dedup by row key (first = higher priority), then sort per
    /// [`rank::compare`]. The result is the `/sources` wire body.
    pub fn sources(&self, media_type: &str, id: &str) -> Sources {
        let mut seen: HashSet<String> = HashSet::new();
        let mut rows: Vec<RankedStream> = Vec::new();
        let mut counts: BTreeMap<String, usize> = BTreeMap::new();

        for entry in &self.entries {
            if !entry.addon.manifest().accepts("stream", media_type, id) {
                continue;
            }
            let streams = entry.addon.streams(media_type, id);
            let mut added = 0usize;
            for stream in &streams {
                let Some(row) = rank::parse(stream, &entry.id, &entry.name, entry.priority) else {
                    continue;
                };
                let key = rank::row_key(&row);
                if !seen.insert(key) {
                    continue; // first (higher-priority) answer keeps the row
                }
                added += 1;
                rows.push(row);
            }
            if added > 0 {
                counts.insert(entry.name.clone(), added);
            }
        }

        rank::sort_rows(&mut rows);

        let candidates = rows
            .into_iter()
            .map(|row| Candidate {
                id: rank::row_key(&row),
                addon: row.addon_name,
                kind: row.kind,
                label: row.release,
                quality: Some(row.quality.as_str().to_string()),
                url: row.url,
                info_hash: row.info_hash,
                file_idx: match row.kind {
                    Kind::Torrent => Some(row.file_idx),
                    Kind::Direct => None,
                },
                size_bytes: row.size_bytes,
            })
            .collect();

        Sources {
            candidates,
            counts_by_addon: counts,
            installed_addons: self.installed(),
        }
    }
}

/// A fixture-backed fake add-on. `fixture` is the `{ "streams": [...] }` body,
/// embedded at compile time — no network, no file I/O at runtime.
struct FakeAddon {
    id: String,
    manifest: Manifest,
    streams: Vec<Stream>,
}

impl FakeAddon {
    fn from_fixture(id: &str, manifest: Manifest, fixture: &str) -> Self {
        let response: crate::stream::StreamResponse =
            serde_json::from_str(fixture).expect("seeded add-on fixture is valid JSON");
        Self {
            id: id.to_string(),
            manifest,
            streams: response.streams,
        }
    }

    /// Torrentio-like fake: torrent rows (name + title carrying
    /// quality/seeders/size/language, `infoHash`, optional `fileIdx`).
    pub fn torrentio() -> Self {
        Self::from_fixture(
            "com.stremio.torrentio.addon",
            Manifest {
                id: "com.stremio.torrentio.addon".into(),
                version: "0.0.0".into(),
                name: "Torrentio".into(),
                description: Some("Seeded fake Torrentio (fixture-backed)".into()),
                types: vec!["movie".into(), "series".into()],
                resources: vec!["stream".into()],
                id_prefixes: vec![],
            },
            include_str!("../fixtures/torrentio.json"),
        )
    }

    /// NoTorrent/VidKing-like fake: direct `url` rows, no `infoHash`.
    pub fn notorrent() -> Self {
        Self::from_fixture(
            "org.notorrent.addon",
            Manifest {
                id: "org.notorrent.addon".into(),
                version: "2.7.0".into(),
                name: "NoTorrent".into(),
                description: Some("Seeded fake NoTorrent/VidKing (fixture-backed)".into()),
                types: vec!["movie".into(), "series".into()],
                resources: vec!["stream".into(), "catalog".into()],
                id_prefixes: vec![],
            },
            include_str!("../fixtures/notorrent.json"),
        )
    }
}

impl Addon for FakeAddon {
    fn id(&self) -> &str {
        &self.id
    }

    fn manifest(&self) -> &Manifest {
        &self.manifest
    }

    fn streams(&self, _media_type: &str, _id: &str) -> Vec<Stream> {
        self.streams.clone()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn seeded_registry_installs_torrentio_then_notorrent() {
        let registry = Registry::seeded();
        let installed = registry.installed();
        assert_eq!(installed.len(), 2);
        assert_eq!(installed[0].name, "Torrentio");
        assert_eq!(installed[0].priority, 0);
        assert_eq!(installed[1].name, "NoTorrent");
        assert_eq!(installed[1].priority, 1);
    }

    #[test]
    fn torrentio_fixture_sorts_quality_seeders_release_language() {
        let registry = Registry::new().install(FakeAddon::torrentio());
        let sources = registry.sources("series", "tt123:1:2");

        let releases: Vec<&str> = sources
            .candidates
            .iter()
            .map(|c| c.label.as_str())
            .collect();
        assert_eq!(
            releases,
            vec![
                "Zeta.Release.2022.2160p.WEB-DL.DDP5.1.Atmos.HDR.DV.HEVC-FLUX",
                "Alpha.Release.2022.1080p.WEB-DL.DDP5.1.H.264-GRP",
                "Alpha.Release.2022.1080p.WEB-DL.DDP5.1.H.264-GRP",
                "Beta.Release.2022.1080p.WEB-DL.DDP5.1.H.264-GRP",
                "Alpha.Release.2022.1080p.WEBRip.x264-RLS",
                "Alpha.Release.2022.720p.WEB-DL.H.264-GRP",
            ]
        );

        let order: Vec<(String, String)> = sources
            .candidates
            .iter()
            .map(|c| (c.quality.clone().unwrap(), c.addon.clone()))
            .collect();
        assert_eq!(order[0], ("4K".to_string(), "Torrentio".to_string()));
        assert_eq!(order[1], ("1080p".to_string(), "Torrentio".to_string()));
        assert_eq!(order[5], ("720p".to_string(), "Torrentio".to_string()));
    }

    #[test]
    fn aggregated_sources_rank_content_first_then_addon_priority() {
        let registry = Registry::seeded();
        let sources = registry.sources("series", "2");

        assert_eq!(sources.candidates.len(), 9);

        // 4K rows lead across both add-ons: Torrentio (seeders 50) then
        // NoTorrent (direct, no seeders). A NoTorrent 4K sorts ABOVE a
        // Torrentio 1080p — install priority is a tiebreak, not the lead key.
        assert_eq!(sources.candidates[0].kind, Kind::Torrent);
        assert_eq!(sources.candidates[0].addon, "Torrentio");
        assert_eq!(sources.candidates[0].quality.as_deref(), Some("4K"));
        assert_eq!(sources.candidates[1].kind, Kind::Direct);
        assert_eq!(sources.candidates[1].addon, "NoTorrent");
        assert_eq!(sources.candidates[1].quality.as_deref(), Some("4K"));

        // Full order, as (addon, quality, kind):
        let order: Vec<(String, String, Kind)> = sources
            .candidates
            .iter()
            .map(|c| (c.addon.clone(), c.quality.clone().unwrap(), c.kind))
            .collect();
        assert_eq!(
            order,
            vec![
                ("Torrentio".into(), "4K".into(), Kind::Torrent),
                ("NoTorrent".into(), "4K".into(), Kind::Direct),
                ("Torrentio".into(), "1080p".into(), Kind::Torrent),
                ("Torrentio".into(), "1080p".into(), Kind::Torrent),
                ("Torrentio".into(), "1080p".into(), Kind::Torrent),
                ("Torrentio".into(), "1080p".into(), Kind::Torrent),
                ("NoTorrent".into(), "1080p".into(), Kind::Direct),
                ("Torrentio".into(), "720p".into(), Kind::Torrent),
                ("NoTorrent".into(), "720p".into(), Kind::Direct),
            ]
        );

        // counts + installed descriptors
        assert_eq!(sources.counts_by_addon["Torrentio"], 6);
        assert_eq!(sources.counts_by_addon["NoTorrent"], 3);
        assert_eq!(sources.installed_addons.len(), 2);
    }

    #[test]
    fn direct_rows_carry_url_and_torrent_rows_carry_info_hash() {
        let registry = Registry::seeded();
        let sources = registry.sources("series", "2");

        let direct: Vec<&Candidate> = sources
            .candidates
            .iter()
            .filter(|c| c.kind == Kind::Direct)
            .collect();
        assert_eq!(direct.len(), 3);
        assert!(direct
            .iter()
            .all(|c| c.url.is_some() && c.info_hash.is_none()));

        let torrent: Vec<&Candidate> = sources
            .candidates
            .iter()
            .filter(|c| c.kind == Kind::Torrent)
            .collect();
        assert_eq!(torrent.len(), 6);
        assert!(torrent
            .iter()
            .all(|c| c.info_hash.is_some() && c.url.is_none() && c.file_idx.is_some()));
    }

    #[test]
    fn dedup_keeps_first_higher_priority_answer() {
        // A second add-on returning the exact same torrent infoHash is dropped:
        // the higher-priority (earlier) add-on keeps the row.
        let duplicate = FakeAddon::from_fixture(
            "dup.addon",
            Manifest {
                id: "dup.addon".into(),
                version: "0.0.0".into(),
                name: "Duplicate".into(),
                description: None,
                types: vec!["series".into()],
                resources: vec!["stream".into()],
                id_prefixes: vec![],
            },
            r#"{ "streams": [
                { "name": "Torrentio", "title": "Zeta.Release.2022.2160p.WEB-DL\n👤 50 💾 18.42 GB\n🇬🇧", "infoHash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "fileIdx": 0 }
            ] }"#,
        );

        let registry = Registry::new()
            .install(FakeAddon::torrentio())
            .install(duplicate);
        let sources = registry.sources("series", "2");

        let zeta: Vec<&Candidate> = sources
            .candidates
            .iter()
            .filter(|c| c.label.starts_with("Zeta.Release"))
            .collect();
        assert_eq!(zeta.len(), 1, "duplicate infoHash row is deduped");
        assert_eq!(zeta[0].addon, "Torrentio", "higher priority keeps the row");
        assert_eq!(sources.counts_by_addon["Torrentio"], 6);
        assert!(!sources.counts_by_addon.contains_key("Duplicate"));
    }

    #[test]
    fn fixtures_on_disk_parse_as_valid_stremio_streams() {
        for rel in ["fixtures/torrentio.json", "fixtures/notorrent.json"] {
            let path = format!("{}/{}", env!("CARGO_MANIFEST_DIR"), rel);
            let text = std::fs::read_to_string(&path).unwrap();
            let response: crate::stream::StreamResponse = serde_json::from_str(&text).unwrap();
            assert!(!response.streams.is_empty());
        }
    }
}
