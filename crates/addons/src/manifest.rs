//! Minimal Stremio add-on manifest, sufficient for stream-resource matching.
//!
//! The JS oracle (`qml/AddonClient.js` `accepts`) also handles *object* resource
//! entries (`{ name, types, idPrefixes }`); no seeded add-on uses them, so this
//! slice models only the bare-string form plus manifest-level `types` /
//! `idPrefixes`. Object entries are a later-slice addition if a real add-on
//! needs them.

use serde::{Deserialize, Serialize};

/// The subset of a Stremio manifest this crate consumes.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Manifest {
    pub id: String,
    pub version: String,
    pub name: String,
    #[serde(default)]
    pub description: Option<String>,
    #[serde(default)]
    pub types: Vec<String>,
    #[serde(default)]
    pub resources: Vec<String>,
    #[serde(default)]
    pub id_prefixes: Vec<String>,
}

impl Manifest {
    /// Port of `AddonClient.accepts` for the bare-string resource path:
    /// the manifest must name `resource`, support `media_type`, and any
    /// `id_prefixes` must match the id. Stremio ids keep their colons
    /// (`tt123`, `tt123:1:2`) and are matched raw.
    pub fn accepts(&self, resource: &str, media_type: &str, id: &str) -> bool {
        if !self.resources.iter().any(|r| r == resource) {
            return false;
        }
        if !self.types.iter().any(|t| t == media_type) {
            return false;
        }
        prefix_ok(&self.id_prefixes, id)
    }
}

fn prefix_ok(prefixes: &[String], id: &str) -> bool {
    if prefixes.is_empty() {
        return true;
    }
    prefixes.iter().any(|p| id.starts_with(p))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn manifest(resources: &[&str], types: &[&str], prefixes: &[&str]) -> Manifest {
        Manifest {
            id: "test.addon".into(),
            version: "0.0.0".into(),
            name: "Test".into(),
            description: None,
            resources: resources.iter().map(|s| s.to_string()).collect(),
            types: types.iter().map(|s| s.to_string()).collect(),
            id_prefixes: prefixes.iter().map(|s| s.to_string()).collect(),
        }
    }

    #[test]
    fn accepts_named_resource_matching_type_and_id() {
        let m = manifest(&["stream"], &["movie", "series"], &["tt"]);
        assert!(m.accepts("stream", "series", "tt123:1:2"));
        assert!(m.accepts("stream", "movie", "tt123"));
        assert!(!m.accepts("stream", "series", "kitsu:123"));
        assert!(!m.accepts("meta", "series", "tt123"));
        assert!(!m.accepts("stream", "anime", "tt123"));
    }

    #[test]
    fn empty_id_prefixes_accept_any_id() {
        let m = manifest(&["stream"], &["series"], &[]);
        assert!(m.accepts("stream", "series", "anything:1:2"));
    }
}
