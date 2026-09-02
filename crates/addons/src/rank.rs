//! Pure ranking module — the port of `qml/Torrentio.js` / `qml/AddonClient.js`
//! parse + sort semantics. No I/O, no async: every function is deterministic
//! and unit-testable in isolation.
//!
//! The parse functions (`detect`, `seeders`, `size`, `size_bytes`, `release`,
//! `languages`) port the JS one-for-one. The sort precedence is the slice's
//! spec — quality → seeders → release → language → add-on install priority —
//! and it is the one place this crate deliberately departs from the JS oracle
//! (see the `// Divergence:` note on [`compare`]).

use std::cmp::Ordering;

use serde::{Deserialize, Serialize};

use crate::stream::Stream;

/// Stream quality ladder, lowest to highest. `derive(Ord)` puts the variants in
/// SD < 480p < 720p < 1080p < 4K order, so a descending sort is
/// `b.quality.cmp(&a.quality)`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Kind {
    Torrent,
    Direct,
}

/// Stream quality ladder, lowest to highest.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum Quality {
    Sd,
    P480,
    P720,
    P1080,
    K4,
}

impl Quality {
    /// JS `_rank`: 4K=4, 1080p=3, 720p=2, 480p=1, SD=0.
    pub fn rank(self) -> u8 {
        match self {
            Quality::Sd => 0,
            Quality::P480 => 1,
            Quality::P720 => 2,
            Quality::P1080 => 3,
            Quality::K4 => 4,
        }
    }

    /// The wire label for the quality (JS `_quality`'s returned string).
    pub fn as_str(self) -> &'static str {
        match self {
            Quality::Sd => "SD",
            Quality::P480 => "480p",
            Quality::P720 => "720p",
            Quality::P1080 => "1080p",
            Quality::K4 => "4K",
        }
    }

    /// JS `_quality`: classify from `name` + `title`, in this exact order.
    pub fn detect(name: Option<&str>, title: Option<&str>) -> Quality {
        let hay = format!("{} {}", name.unwrap_or(""), title.unwrap_or("")).to_lowercase();
        if hay.contains("2160p") || contains_word(&hay, "4k") || hay.contains("uhd") {
            Quality::K4
        } else if hay.contains("1080p") {
            Quality::P1080
        } else if hay.contains("720p") {
            Quality::P720
        } else if hay.contains("480p") {
            Quality::P480
        } else {
            Quality::Sd
        }
    }
}

/// A whole-word match, approximating JS's `\bword\b` (a word is split on any
/// non-alphanumeric char). Used only for the `4k` branch of `_quality`.
fn contains_word(hay: &str, word: &str) -> bool {
    hay.split(|c: char| !c.is_alphanumeric())
        .any(|token| token == word)
}

/// JS `_seeders`: the first `👤 N` in the title, else -1.
pub fn seeders(stream: &Stream) -> i64 {
    let title = stream.title.as_deref().unwrap_or("");
    let Some(idx) = title.find('👤') else {
        return -1;
    };
    let rest = title[idx + '👤'.len_utf8()..].trim_start();
    let digits: String = rest.chars().take_while(|c| c.is_ascii_digit()).collect();
    digits.parse().unwrap_or(-1)
}

/// JS `_size`: the `💾 18.42 GB` capture with whitespace collapsed to a single
/// space and trimmed. Empty when the title has no size.
pub fn size(stream: &Stream) -> String {
    let Some((num, unit)) = size_capture(stream) else {
        return String::new();
    };
    format!("{num} {unit}")
}

/// The same capture converted to bytes. Binary units (KB=1024, MB=1024², …);
/// fractional sizes round to the nearest byte.
pub fn size_bytes(stream: &Stream) -> Option<u64> {
    let (num, unit) = size_capture(stream)?;
    let value: f64 = num.parse().ok()?;
    let multiplier: u64 = match unit.to_ascii_uppercase().as_str() {
        "KB" => 1024,
        "MB" => 1024 * 1024,
        "GB" => 1024 * 1024 * 1024,
        "TB" => 1024 * 1024 * 1024 * 1024,
        _ => return None,
    };
    Some((value * multiplier as f64).round() as u64)
}

/// The `💾`-anchored `(number, unit)` capture from the title. The unit is
/// exactly `[KMGT]B` (uppercase, matching JS's `_size` regex), so trailing
/// title content (e.g. a language-flag line) is ignored.
fn size_capture(stream: &Stream) -> Option<(String, String)> {
    let title = stream.title.as_deref().unwrap_or("");
    let idx = title.find('💾')?;
    let rest = title[idx + '💾'.len_utf8()..].trim_start();

    let mut end = 0;
    let mut dot_seen = false;
    for (i, c) in rest.char_indices() {
        if c.is_ascii_digit() {
            end = i + c.len_utf8();
        } else if c == '.' && !dot_seen {
            dot_seen = true;
            end = i + c.len_utf8();
        } else {
            break;
        }
    }
    if end == 0 {
        return None;
    }
    let num = rest[..end].to_string();

    // Unit: optional whitespace then one of K/M/G/T followed by B.
    let mut unit_chars = rest[end..].trim_start().chars();
    let (Some(letter), Some(b)) = (unit_chars.next(), unit_chars.next()) else {
        return None;
    };
    if !matches!(letter, 'K' | 'M' | 'G' | 'T') || b != 'B' {
        return None;
    }
    Some((num, format!("{letter}B")))
}

/// JS `_release`: the first line of `title`, else `name`, else `description`,
/// trimmed. Empty-string fallbacks follow JS truthiness (an empty title falls
/// through to the name).
pub fn release(stream: &Stream) -> String {
    let src = first_nonempty(&stream.title)
        .or_else(|| first_nonempty(&stream.name))
        .or_else(|| first_nonempty(&stream.description))
        .unwrap_or("");
    src.split('\n').next().unwrap_or("").trim().to_string()
}

fn first_nonempty(value: &Option<String>) -> Option<&str> {
    value.as_deref().filter(|s| !s.is_empty())
}

/// JS `_languages`: decode regional-indicator emoji pairs in the title back to
/// two-letter ISO codes, deduped in first-seen order.
pub fn languages(stream: &Stream) -> Vec<String> {
    let title = stream.title.as_deref().unwrap_or("");
    let chars: Vec<char> = title.chars().collect();
    let mut out: Vec<String> = Vec::new();

    let mut i = 0;
    while i + 1 < chars.len() {
        let a = chars[i] as u32;
        let b = chars[i + 1] as u32;
        if (0x1F1E6..=0x1F1FF).contains(&a) && (0x1F1E6..=0x1F1FF).contains(&b) {
            let code = format!(
                "{}{}",
                char::from_u32(65 + (a - 0x1F1E6)).expect("regional indicator maps to A-Z"),
                char::from_u32(65 + (b - 0x1F1E6)).expect("regional indicator maps to A-Z"),
            );
            if !out.contains(&code) {
                out.push(code);
            }
            i += 2;
        } else {
            i += 1;
        }
    }
    out
}

/// One interpreted row: the JS `parseStream` result, minus the display-only
/// fields (`tags`, `qualityLine`, `sourceName`, `audio`) that belong to a later
/// SourcesSheet slice. Carries the add-on provenance the JS passes in.
#[derive(Clone, Debug)]
pub struct RankedStream {
    pub quality: Quality,
    pub seeders: i64,
    pub size_bytes: Option<u64>,
    pub release: String,
    pub languages: Vec<String>,
    pub kind: Kind,
    pub info_hash: Option<String>,
    pub url: Option<String>,
    pub file_idx: u64,
    pub addon_id: String,
    pub addon_name: String,
    pub addon_priority: usize,
}

/// Port of `AddonClient.parseStream`: a torrent row is one with a non-empty
/// `infoHash`; a direct row is one with a `url` and no `infoHash`; rows with
/// neither are dropped (the player can't carry them).
pub fn parse(
    stream: &Stream,
    addon_id: &str,
    addon_name: &str,
    addon_priority: usize,
) -> Option<RankedStream> {
    let is_torrent = stream.info_hash.as_deref().is_some_and(|h| !h.is_empty());
    let direct_url = if is_torrent {
        None
    } else {
        stream.url.clone().filter(|u| !u.is_empty())
    };
    if !is_torrent && direct_url.is_none() {
        return None;
    }

    Some(RankedStream {
        quality: Quality::detect(stream.name.as_deref(), stream.title.as_deref()),
        seeders: if is_torrent { seeders(stream) } else { -1 },
        size_bytes: size_bytes(stream),
        release: release(stream),
        languages: languages(stream),
        kind: if is_torrent {
            Kind::Torrent
        } else {
            Kind::Direct
        },
        info_hash: if is_torrent {
            stream.info_hash.clone()
        } else {
            None
        },
        url: direct_url,
        file_idx: stream.file_idx.unwrap_or(0),
        addon_id: addon_id.to_string(),
        addon_name: addon_name.to_string(),
        addon_priority,
    })
}

/// The row's dedup identity — JS `_rowKey`: `u:<url>` for direct rows,
/// `t:<lowercased infoHash>:<fileIdx>` for torrent rows.
pub fn row_key(row: &RankedStream) -> String {
    match &row.url {
        Some(url) => format!("u:{url}"),
        None => format!(
            "t:{}:{}",
            row.info_hash.as_deref().unwrap_or("").to_lowercase(),
            row.file_idx
        ),
    }
}

/// Total ordering: quality (desc) → seeders (desc, unknown -1 last) → release
/// (asc) → language (fewer languages first, then joined codes asc) → add-on
/// install priority (asc). Ties return `Ordering::Equal` and are resolved by
/// the stable sort's original (deterministic) order.
///
/// # Divergence
///
/// `AddonClient._sortRows` sorts **add-on priority first**, then rank, then
/// seeders — the behavior that buried freshly-seeded extensions' rows below
/// every earlier extension's (see
/// `docs/research/theatre-http-source/02-notorrent-burial-dossier.md`). This
/// crate sorts content keys first and uses install priority only as the final
/// tiebreak, per the slice spec ("quality→seeders→release→language, then
/// addon install-priority"). `Torrentio.js` sorts only rank→seeders; release
/// and language are additions from the same spec.
pub fn compare(a: &RankedStream, b: &RankedStream) -> Ordering {
    b.quality
        .cmp(&a.quality)
        .then_with(|| b.seeders.cmp(&a.seeders))
        .then_with(|| a.release.cmp(&b.release))
        .then_with(|| language_key(a).cmp(&language_key(b)))
        .then_with(|| a.addon_priority.cmp(&b.addon_priority))
}

/// Language sort key: fewer languages first (a single-audio English stream
/// sorts before a multi-audio stream), then the joined ISO codes
/// lexicographically.
fn language_key(row: &RankedStream) -> (usize, String) {
    (row.languages.len(), row.languages.join(","))
}

/// Stable sort by [`compare`].
pub fn sort_rows(rows: &mut [RankedStream]) {
    rows.sort_by(compare);
}

#[cfg(test)]
mod tests {
    use super::*;

    fn stream(title: &str, name: Option<&str>) -> Stream {
        Stream {
            name: name.map(str::to_string),
            title: Some(title.to_string()),
            description: None,
            url: None,
            info_hash: None,
            file_idx: None,
            behavior_hints: None,
        }
    }

    #[test]
    fn quality_detect_matches_js_ladder() {
        assert_eq!(Quality::detect(None, Some("X 2160p WEB-DL")), Quality::K4);
        assert_eq!(Quality::detect(None, Some("X 4K HEVC")), Quality::K4);
        assert_eq!(Quality::detect(None, Some("X UHD REMUX")), Quality::K4);
        assert_eq!(
            Quality::detect(None, Some("X 1080p WEB-DL")),
            Quality::P1080
        );
        assert_eq!(Quality::detect(None, Some("X 720p WEBRip")), Quality::P720);
        assert_eq!(Quality::detect(None, Some("X 480p x264")), Quality::P480);
        assert_eq!(Quality::detect(None, Some("X DVDRip")), Quality::Sd);
        // "4k" must be a whole word: "mkv4k" does not match JS's \b4k\b.
        assert_eq!(Quality::detect(None, Some("X mkv4k HEVC")), Quality::Sd);
    }

    #[test]
    fn seeders_parse_from_title_else_minus_one() {
        assert_eq!(
            seeders(&stream("Line One\n👤 1520 💾 18.42 GB\n", None)),
            1520
        );
        assert_eq!(seeders(&stream("👤  42  something", None)), 42);
        assert_eq!(seeders(&stream("no seeders here", None)), -1);
        assert_eq!(seeders(&stream("👤 nope", None)), -1);
    }

    #[test]
    fn release_is_first_line_with_js_fallbacks() {
        assert_eq!(
            release(&stream("  Alpha.Release.1080p\n👤 1 💾 1 GB\n", None)),
            "Alpha.Release.1080p"
        );
        // empty title falls back to name
        let s = Stream {
            title: Some("".into()),
            name: Some("Fallback.Name".into()),
            ..stream("", None)
        };
        assert_eq!(release(&s), "Fallback.Name");
        assert_eq!(release(&stream("", None)), "");
    }

    #[test]
    fn size_parses_human_and_bytes() {
        let s = stream("Title\n👤 10 💾 18.42 GB\n", None);
        assert_eq!(size(&s), "18.42 GB");
        assert_eq!(
            size_bytes(&s),
            Some((18.42 * 1024.0_f64.powi(3)).round() as u64)
        );
        assert_eq!(
            size_bytes(&stream("💾 700 MB", None)),
            Some(700 * 1024 * 1024)
        );
        assert_eq!(size(&stream("no size", None)), "");
        assert_eq!(size_bytes(&stream("no size", None)), None);
        // Trailing title content after the unit (a language-flag line) is ignored.
        let s = stream("Title\n👤 50 💾 18.42 GB\n🇬🇧", None);
        assert_eq!(size(&s), "18.42 GB");
        assert_eq!(
            size_bytes(&s),
            Some((18.42 * 1024.0_f64.powi(3)).round() as u64)
        );
    }

    #[test]
    fn languages_decode_regional_indicator_pairs() {
        // GB flag = U+1F1EC U+1F1E7; PL flag = U+1F1F5 U+1F1F1
        assert_eq!(
            languages(&stream("Title\nline2\n🇬🇧 🇵🇱", None)),
            vec!["GB".to_string(), "PL".to_string()]
        );
        // dedup in first-seen order
        assert_eq!(
            languages(&stream("🇬🇧 🇬🇧 🇵🇱 🇬🇧", None)),
            vec!["GB".to_string(), "PL".to_string()]
        );
        assert!(languages(&stream("no flags", None)).is_empty());
    }

    fn ranked(
        quality: Quality,
        seeders: i64,
        release: &str,
        langs: &[&str],
        priority: usize,
    ) -> RankedStream {
        RankedStream {
            quality,
            seeders,
            size_bytes: None,
            release: release.to_string(),
            languages: langs.iter().map(|s| s.to_string()).collect(),
            kind: Kind::Torrent,
            info_hash: Some("h".into()),
            url: None,
            file_idx: 0,
            addon_id: "x".into(),
            addon_name: "X".into(),
            addon_priority: priority,
        }
    }

    #[test]
    fn compare_orders_quality_then_seeders_then_release_then_language_then_priority() {
        // quality dominates
        assert_eq!(
            compare(
                &ranked(Quality::K4, 1, "a", &[], 9),
                &ranked(Quality::P1080, 9999, "a", &[], 0)
            ),
            Ordering::Less
        );
        // seeders break a quality tie (unknown -1 last)
        assert_eq!(
            compare(
                &ranked(Quality::P1080, 900, "a", &[], 0),
                &ranked(Quality::P1080, 100, "a", &[], 0)
            ),
            Ordering::Less
        );
        assert_eq!(
            compare(
                &ranked(Quality::P1080, 100, "a", &[], 0),
                &ranked(Quality::P1080, -1, "a", &[], 0)
            ),
            Ordering::Less
        );
        // release breaks a quality+seeders tie (ascending)
        assert_eq!(
            compare(
                &ranked(Quality::P1080, 900, "Alpha", &[], 0),
                &ranked(Quality::P1080, 900, "Beta", &[], 0)
            ),
            Ordering::Less
        );
        // language breaks a full tie: single-audio before multi-audio
        assert_eq!(
            compare(
                &ranked(Quality::P1080, 900, "Alpha", &["GB"], 0),
                &ranked(Quality::P1080, 900, "Alpha", &["GB", "PL"], 0)
            ),
            Ordering::Less
        );
        // install priority is the final tiebreak
        assert_eq!(
            compare(
                &ranked(Quality::P1080, 900, "Alpha", &["GB"], 0),
                &ranked(Quality::P1080, 900, "Alpha", &["GB"], 1)
            ),
            Ordering::Less
        );
    }
}
