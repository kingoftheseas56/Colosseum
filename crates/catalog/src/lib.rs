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
    db: Mutex<Connection>,
}

#[derive(Serialize)]
pub struct Series {
    pub id: i64,
    pub title: String,
    pub source: String,
}

impl Catalog {
    /// Opens (creating it if needed) the SQLite catalog at `path`. The schema
    /// is created idempotently so the daemon can reopen a data-dir file.
    pub fn open(path: &Path) -> rusqlite::Result<Self> {
        let conn = Connection::open(path)?;
        init_schema(&conn)?;
        Ok(Self {
            db: Mutex::new(conn),
        })
    }

    pub fn open_in_memory() -> rusqlite::Result<Self> {
        let conn = Connection::open_in_memory()?;
        init_schema(&conn)?;
        Ok(Self {
            db: Mutex::new(conn),
        })
    }

    /// Seeds the three demo rows. Idempotent so a daemon restart over the same
    /// data-dir file does not grow or error: fixed ids + `INSERT OR REPLACE`.
    pub fn seed_demo(&self) {
        let conn = self.db.lock().expect("catalog lock poisoned");
        conn.execute_batch(
            "INSERT OR REPLACE INTO series (id, title, source) VALUES
                (1, 'Demo Series Alpha', 'demo'),
                (2, 'Demo Series Beta', 'demo'),
                (3, 'Demo Series Gamma', 'demo');",
        )
        .expect("seed demo catalog");
    }

    /// Substring match on title, case-insensitive for ASCII; user input must
    /// not be able to inject LIKE wildcards (`%` and `_` are escaped).
    pub fn search(&self, q: &str) -> Vec<Series> {
        let conn = self.db.lock().expect("catalog lock poisoned");
        let pattern = format!("%{}%", escape_like(q));
        let mut stmt = conn
            .prepare(
                "SELECT id, title, source FROM series
                 WHERE title LIKE ?1 ESCAPE '\\'
                 ORDER BY title, id",
            )
            .expect("prepare search query");
        stmt.query_map(rusqlite::params![pattern], |row| {
            Ok(Series {
                id: row.get(0)?,
                title: row.get(1)?,
                source: row.get(2)?,
            })
        })
        .expect("run search query")
        .filter_map(Result::ok)
        .collect()
    }
}

/// Escapes LIKE wildcards (`%`, `_`) and the escape character itself so user
/// input is always matched literally.
fn escape_like(input: &str) -> String {
    let mut escaped = String::with_capacity(input.len());
    for ch in input.chars() {
        if matches!(ch, '%' | '_' | '\\') {
            escaped.push('\\');
        }
        escaped.push(ch);
    }
    escaped
}

fn init_schema(conn: &Connection) -> rusqlite::Result<()> {
    conn.execute_batch(
        "CREATE TABLE IF NOT EXISTS series (
            id INTEGER PRIMARY KEY,
            title TEXT NOT NULL,
            source TEXT NOT NULL
        );",
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
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

    #[test]
    fn search_escapes_underscore_wildcard() {
        let catalog = Catalog::open_in_memory().unwrap();
        catalog.seed_demo();
        {
            let conn = catalog.db.lock().unwrap();
            conn.execute_batch(
                "INSERT INTO series (id, title, source) VALUES (99, 'A_B', 'demo');",
            )
            .unwrap();
        }

        // With `_` escaped, a literal underscore matches only the literal title,
        // not e.g. "AXB" that an unescaped `_` wildcard would also match.
        assert_eq!(catalog.search("A_B").len(), 1);
        assert_eq!(catalog.search("AXB").len(), 0);
    }

    #[test]
    fn open_and_seed_are_idempotent() {
        let dir = std::env::temp_dir().join(format!("catalog-core-test-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("catalog.db");

        let catalog = Catalog::open(&path).unwrap();
        catalog.seed_demo();
        drop(catalog);

        let catalog = Catalog::open(&path).unwrap();
        catalog.seed_demo();
        drop(catalog);

        let catalog = Catalog::open(&path).unwrap();
        assert_eq!(catalog.search("demo").len(), 3);
        drop(catalog);

        std::fs::remove_dir_all(&dir).unwrap();
    }
}
