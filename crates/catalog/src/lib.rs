//! Catalog domain — scaffolding for the ralph loop.
//!
//! Contract: `Catalog` + `Series`. Storage is SQLite (rusqlite, bundled).
//! Search is LIKE-based for the core slice; tantivy is the planned upgrade
//! (TODO.md) — the public API should not leak which one is behind it.

use rusqlite::Connection;
use serde::Serialize;
use std::path::Path;
use std::sync::Mutex;

pub struct Catalog {
    _db: Mutex<Connection>,
}

#[derive(Serialize)]
pub struct Series {
    pub id: i64,
    pub title: String,
    pub source: String,
}

impl Catalog {
    pub fn open(_path: &Path) -> rusqlite::Result<Self> {
        todo!("TODO.md: catalog-core")
    }

    pub fn open_in_memory() -> rusqlite::Result<Self> {
        todo!("TODO.md: catalog-core")
    }

    pub fn seed_demo(&self) {
        todo!("TODO.md: catalog-core")
    }

    /// Substring match, case-insensitive for ASCII; user input must not be
    /// able to inject LIKE wildcards.
    pub fn search(&self, _q: &str) -> Vec<Series> {
        todo!("TODO.md: catalog-core")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    #[ignore = "scaffold spec: enable in TODO.md catalog-core"]
    fn search_matches_substring_case_insensitively() {
        let catalog = Catalog::open_in_memory().unwrap();
        catalog.seed_demo();

        let hits = catalog.search("alpha");
        assert_eq!(hits.len(), 1);
        assert_eq!(hits[0].title, "Demo Series Alpha");
        assert!(catalog.search("nonexistent").is_empty());
        assert_eq!(
            catalog.search("100%").len(),
            0,
            "% is not a wildcard from user input"
        );
    }
}
