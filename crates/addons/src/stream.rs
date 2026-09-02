//! Raw Stremio stream-row types, shaped exactly like what an add-on returns
//! from `GET {base}/stream/{type}/{id}.json` (the `{ "streams": [...] }`
//! envelope). Field names follow the wire (camelCase); `serde` handles the
//! mapping to the snake_case Rust fields.

use serde::{Deserialize, Serialize};

/// One raw stream row as an add-on returns it. Parsing/semantics (quality,
/// seeders, release, languages) live in [`crate::rank`] — this type is the
/// uninterpreted wire data.
#[derive(Clone, Debug, PartialEq, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Stream {
    pub name: Option<String>,
    pub title: Option<String>,
    pub description: Option<String>,
    pub url: Option<String>,
    pub info_hash: Option<String>,
    pub file_idx: Option<u64>,
    pub behavior_hints: Option<BehaviorHints>,
}

/// The `behaviorHints` sub-object. Only the fields the ranking/transport model
/// actually reads are modelled here; the full Stremio shape has more, which a
/// later slice can add without breaking this one.
#[derive(Clone, Debug, Default, PartialEq, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct BehaviorHints {
    pub binge_group: Option<String>,
    pub filename: Option<String>,
}

/// The `{ "streams": [...] }` envelope a stream endpoint returns.
#[derive(Clone, Debug, Default, Deserialize)]
pub struct StreamResponse {
    #[serde(default)]
    pub streams: Vec<Stream>,
}
