//! Catalog domain — the local series store + browse/search queries.
//!
//! Contract: `Catalog`, `Series`, `SeriesDetail`, `Home`. Storage is SQLite
//! (rusqlite, bundled). Search is LIKE-based for the core slice; tantivy is
//! the planned upgrade (TODO.md) — the public API should not leak which one is
//! behind it.
//!
//! The `series` table carries the browse fields the daemon's home/detail
//! endpoints serve. `source` is the origin label for a row (`demo` for the
//! seeded POC rows, `local` for the local vault); provider integrations
//! (jikan/kitsu/anilist/metahub) are a later daemon-phase and are deliberately
//! not modelled here. Timestamps are opaque RFC 3339 strings owned by the
//! seed — deterministic, so tests and the daemon's demo output never drift.

use rusqlite::Connection;
use serde::Serialize;
use std::path::Path;
use std::sync::Mutex;

pub struct Catalog {
    db: Mutex<Connection>,
}

/// A full `series` row. Served by search and both home rails. The watch
/// fields let the home `continue_watching` rail show progress; unused columns
/// are serialized as-is so consumers can ignore what they don't need.
#[derive(Serialize)]
pub struct Series {
    pub id: i64,
    pub title: String,
    pub source: String,
    pub description: String,
    pub poster_color: String,
    pub added_at: String,
    pub last_watched_at: Option<String>,
    pub watch_position_secs: i64,
    pub duration_secs: i64,
    pub episode_count: Option<i64>,
}

/// The series-detail projection: everything the detail screen needs, nothing
/// playback-progress specific.
#[derive(Serialize)]
pub struct SeriesDetail {
    pub id: i64,
    pub title: String,
    pub source: String,
    pub description: String,
    pub poster_color: String,
    pub added_at: String,
    pub episode_count: Option<i64>,
}

/// The home aggregate: what to resume, plus what to surface as trending.
#[derive(Serialize)]
pub struct Home {
    pub continue_watching: Vec<Series>,
    pub trending: Vec<Series>,
}

/// Column list for `Series`-shaped rows. Order matters: `series_from_row` and
/// `detail_from_row` index into it positionally.
const SERIES_COLUMNS: &str = "id, title, source, description, poster_color, \
                              added_at, last_watched_at, watch_position_secs, \
                              duration_secs, episode_count";

impl Catalog {
    /// Opens (creating it if needed) the SQLite catalog at `path`. The schema
    /// is created idempotently and legacy tables are migrated on open, so the
    /// daemon can reopen a data-dir file from an older build without losing
    /// rows.
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

    /// Seeds the demo catalog: ten fixed rows with varied titles, sources and
    /// added_at ordering, exactly one of which ("Demo Series Beta") has a
    /// nonzero watch position. Idempotent so a daemon restart over the same
    /// data-dir file does not grow or error: fixed ids + `INSERT OR REPLACE`.
    pub fn seed_demo(&self) {
        let conn = self.db.lock().expect("catalog lock poisoned");
        conn.execute_batch(
            "INSERT OR REPLACE INTO series
                (id, title, source, added_at, last_watched_at,
                 watch_position_secs, duration_secs, description,
                 poster_color, episode_count)
             VALUES
                (1,  'Demo Series Alpha', 'demo',  '2026-01-01T00:00:00Z', NULL, 0,   1500,
                     'A first light in the demo universe.', '#e57373', 12),
                (2,  'Demo Series Beta',  'demo',  '2026-01-02T00:00:00Z', '2026-03-06T20:15:00Z', 720, 1440,
                     'The sequel that never sleeps.', '#64b5f6', 24),
                (3,  'Demo Series Gamma', 'demo',  '2026-01-03T00:00:00Z', NULL, 0,   1380,
                     'The third orbit settles old scores.', '#81c784', 12),
                (4,  'Neon Samurai',      'local', '2026-01-10T00:00:00Z', NULL, 0,   1300,
                     'A cyber-ronin cuts through a rain-soaked megacity.', '#ffb74d', 26),
                (5,  'Orbital Frontier',  'demo',  '2026-01-15T00:00:00Z', NULL, 0,   1560,
                     'Surveyors push past the Kuiper line.', '#4dd0e1', 13),
                (6,  'The Clockwork Vale', 'local', '2026-01-20T00:00:00Z', NULL, 0,   1420,
                     'Gears turn beneath a sleeping valley.', '#b39ddb', 20),
                (7,  'Starlight Academy', 'demo',  '2026-02-01T00:00:00Z', NULL, 0,   1400,
                     'Rival clubs chase the winter constellation cup.', '#f06292', 12),
                (8,  'Midnight Ramen',    'local', '2026-02-05T00:00:00Z', NULL, 0,   1350,
                     'A late-night counter and its regulars.', '#a1887f', 10),
                (9,  'Verdant Saga',      'demo',  '2026-02-10T00:00:00Z', NULL, 0,   1520,
                     'An overgrown world remembers its gardeners.', '#aed581', 24),
                (10, 'Iron Harvest',      'local', '2026-02-14T00:00:00Z', NULL, 0,   1480,
                     'Reapers walk fields of steel after the last war.', '#90a4ae', 16);",
        )
        .expect("seed demo catalog");
    }

    /// Substring match on title, case-insensitive for ASCII; user input must
    /// not be able to inject LIKE wildcards (`%` and `_` are escaped).
    pub fn search(&self, q: &str) -> Vec<Series> {
        let conn = self.db.lock().expect("catalog lock poisoned");
        let pattern = format!("%{}%", escape_like(q));
        let mut stmt = conn
            .prepare(&format!(
                "SELECT {SERIES_COLUMNS} FROM series
                 WHERE title LIKE ?1 ESCAPE '\\'
                 ORDER BY title, id"
            ))
            .expect("prepare search query");
        stmt.query_map(rusqlite::params![pattern], series_from_row)
            .expect("run search query")
            .filter_map(Result::ok)
            .collect()
    }

    /// Home aggregate. `continue_watching` is every series with a nonzero
    /// watch position, most-recently-watched first; `trending` is the whole
    /// catalog, newest-added first. Both are deterministic given the seed.
    pub fn home(&self) -> Home {
        let conn = self.db.lock().expect("catalog lock poisoned");
        Home {
            continue_watching: query_series(
                &conn,
                "WHERE watch_position_secs > 0 ORDER BY last_watched_at DESC, id",
            ),
            trending: query_series(&conn, "ORDER BY added_at DESC, id"),
        }
    }

    /// Series detail by id, or `None` when the id is unknown.
    pub fn series(&self, id: i64) -> Option<SeriesDetail> {
        let conn = self.db.lock().expect("catalog lock poisoned");
        let mut stmt = conn
            .prepare(&format!(
                "SELECT {SERIES_COLUMNS} FROM series WHERE id = ?1"
            ))
            .expect("prepare series detail query");
        stmt.query_row(rusqlite::params![id], detail_from_row).ok()
    }
}

/// Run a fixed `WHERE`/`ORDER BY` clause against the series table and collect
/// full rows.
fn query_series(conn: &Connection, clause: &str) -> Vec<Series> {
    let mut stmt = conn
        .prepare(&format!("SELECT {SERIES_COLUMNS} FROM series {clause}"))
        .expect("prepare series query");
    stmt.query_map([], series_from_row)
        .expect("run series query")
        .filter_map(Result::ok)
        .collect()
}

fn series_from_row(row: &rusqlite::Row<'_>) -> rusqlite::Result<Series> {
    Ok(Series {
        id: row.get(0)?,
        title: row.get(1)?,
        source: row.get(2)?,
        description: row.get(3)?,
        poster_color: row.get(4)?,
        added_at: row.get(5)?,
        last_watched_at: row.get(6)?,
        watch_position_secs: row.get(7)?,
        duration_secs: row.get(8)?,
        episode_count: row.get(9)?,
    })
}

fn detail_from_row(row: &rusqlite::Row<'_>) -> rusqlite::Result<SeriesDetail> {
    Ok(SeriesDetail {
        id: row.get(0)?,
        title: row.get(1)?,
        source: row.get(2)?,
        description: row.get(3)?,
        poster_color: row.get(4)?,
        added_at: row.get(5)?,
        episode_count: row.get(9)?,
    })
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

/// Create the current schema, then migrate any legacy table in place. Column
/// additions are guarded by `PRAGMA table_info` so a fresh DB and an old one
/// both converge on the same shape.
fn init_schema(conn: &Connection) -> rusqlite::Result<()> {
    conn.execute_batch(
        "CREATE TABLE IF NOT EXISTS series (
            id INTEGER PRIMARY KEY,
            title TEXT NOT NULL,
            source TEXT NOT NULL,
            added_at TEXT NOT NULL DEFAULT '',
            last_watched_at TEXT,
            watch_position_secs INTEGER NOT NULL DEFAULT 0,
            duration_secs INTEGER NOT NULL DEFAULT 0,
            description TEXT NOT NULL DEFAULT '',
            poster_color TEXT NOT NULL DEFAULT '',
            episode_count INTEGER
        );",
    )?;
    migrate_series(conn)
}

/// Add any columns the legacy (id, title, source) schema is missing.
fn migrate_series(conn: &Connection) -> rusqlite::Result<()> {
    let mut stmt = conn.prepare("PRAGMA table_info(series)")?;
    let existing: Vec<String> = stmt
        .query_map([], |row| row.get::<_, String>(1))?
        .collect::<Result<_, _>>()?;

    let add = |name: &str, ddl: &str| -> rusqlite::Result<()> {
        if !existing.iter().any(|column| column == name) {
            conn.execute(&format!("ALTER TABLE series ADD COLUMN {ddl}"), [])?;
        }
        Ok(())
    };

    add("added_at", "added_at TEXT NOT NULL DEFAULT ''")?;
    add("last_watched_at", "last_watched_at TEXT")?;
    add(
        "watch_position_secs",
        "watch_position_secs INTEGER NOT NULL DEFAULT 0",
    )?;
    add("duration_secs", "duration_secs INTEGER NOT NULL DEFAULT 0")?;
    add("description", "description TEXT NOT NULL DEFAULT ''")?;
    add("poster_color", "poster_color TEXT NOT NULL DEFAULT ''")?;
    add("episode_count", "episode_count INTEGER")?;
    Ok(())
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
    fn home_continue_watching_puts_seeded_watched_series_first() {
        let catalog = Catalog::open_in_memory().unwrap();
        catalog.seed_demo();

        let home = catalog.home();
        assert_eq!(
            home.continue_watching.len(),
            1,
            "exactly one demo row is continued"
        );
        let first = &home.continue_watching[0];
        assert_eq!(first.title, "Demo Series Beta");
        assert!(first.watch_position_secs > 0);
        assert_eq!(first.duration_secs, 1440);
    }

    #[test]
    fn home_trending_is_newest_added_first() {
        let catalog = Catalog::open_in_memory().unwrap();
        catalog.seed_demo();

        let trending = catalog.home().trending;
        assert_eq!(trending.len(), 10);
        assert_eq!(trending[0].title, "Iron Harvest");
        assert_eq!(trending[1].title, "Verdant Saga");
        assert_eq!(trending[9].title, "Demo Series Alpha");
    }

    #[test]
    fn series_detail_returns_shape_and_none_for_unknown_id() {
        let catalog = Catalog::open_in_memory().unwrap();
        catalog.seed_demo();

        let detail = catalog.series(7).unwrap();
        assert_eq!(detail.id, 7);
        assert_eq!(detail.title, "Starlight Academy");
        assert_eq!(detail.source, "demo");
        assert!(!detail.description.is_empty());
        assert!(!detail.poster_color.is_empty());
        assert_eq!(detail.added_at, "2026-02-01T00:00:00Z");
        assert_eq!(detail.episode_count, Some(12));

        assert!(catalog.series(999).is_none());
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
        assert_eq!(catalog.home().trending.len(), 10);
        drop(catalog);

        std::fs::remove_dir_all(&dir).unwrap();
    }

    #[test]
    fn open_migrates_legacy_schema_without_losing_rows() {
        let dir = std::env::temp_dir().join(format!("catalog-migrate-test-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("legacy.db");

        {
            let conn = rusqlite::Connection::open(&path).unwrap();
            conn.execute_batch(
                "CREATE TABLE series (
                    id INTEGER PRIMARY KEY,
                    title TEXT NOT NULL,
                    source TEXT NOT NULL
                 );
                 INSERT INTO series (id, title, source) VALUES (100, 'Legacy Row', 'legacy');",
            )
            .unwrap();
        }

        let catalog = Catalog::open(&path).unwrap();
        catalog.seed_demo();

        // Legacy row survives the migration; the seed adds its ten rows around it.
        assert_eq!(catalog.home().trending.len(), 11);
        let legacy = catalog.series(100).unwrap();
        assert_eq!(legacy.title, "Legacy Row");
        assert_eq!(legacy.source, "legacy");
        assert!(
            legacy.description.is_empty(),
            "migrated columns default empty"
        );

        drop(catalog);
        std::fs::remove_dir_all(&dir).unwrap();
    }
}
