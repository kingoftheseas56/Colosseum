CREATE TABLE IF NOT EXISTS musicbrainz_entities (
  kind TEXT NOT NULL, mbid TEXT NOT NULL, fetched_at TEXT NOT NULL,
  source_url TEXT NOT NULL, payload_json TEXT NOT NULL,
  PRIMARY KEY(kind,mbid)
);
