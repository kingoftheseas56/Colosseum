CREATE TABLE IF NOT EXISTS "imdb__genre" (tt TEXT, genre TEXT);
CREATE INDEX IF NOT EXISTS "imdb__genre_lookup" ON "imdb__genre" (genre, tt);
CREATE TABLE IF NOT EXISTS "imdb__meta" (key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE IF NOT EXISTS "imdb__title" (tt TEXT PRIMARY KEY, type TEXT, title TEXT,
            year INTEGER, endYear INTEGER, runtimeMin INTEGER, genres TEXT,
            rating REAL, votes INTEGER, episodes INTEGER,
            origLang TEXT, isAnime INTEGER, norm_title TEXT);
CREATE INDEX IF NOT EXISTS "imdb__title_norm_title" ON "imdb__title" (norm_title);
CREATE INDEX IF NOT EXISTS "imdb__title_type_lang" ON "imdb__title" (type, origLang);
CREATE INDEX IF NOT EXISTS "imdb__title_type_year" ON "imdb__title" (type, year);
CREATE INDEX IF NOT EXISTS "imdb__title_type_rating" ON "imdb__title" (type, rating DESC, votes DESC);
CREATE INDEX IF NOT EXISTS "imdb__title_type_votes" ON "imdb__title" (type, votes DESC);
CREATE TABLE IF NOT EXISTS "mal__anime" (mal_id INTEGER PRIMARY KEY, title TEXT, title_english TEXT,
            type TEXT, score REAL, scored_by INTEGER, members INTEGER, status TEXT,
            episodes INTEGER, year INTEGER, cover TEXT, synopsis TEXT,
            credits TEXT, tags TEXT, norm_title TEXT, norm_title_english TEXT);
CREATE INDEX IF NOT EXISTS "mal__anime_norm_title_english_idx" ON "mal__anime" (norm_title_english);
CREATE INDEX IF NOT EXISTS "mal__anime_norm_title_idx" ON "mal__anime" (norm_title);
CREATE INDEX IF NOT EXISTS "mal__anime_status_type_year_idx" ON "mal__anime" (status, type, year);
CREATE INDEX IF NOT EXISTS "mal__anime_score_votes_idx" ON "mal__anime" (score DESC, scored_by DESC);
CREATE INDEX IF NOT EXISTS "mal__anime_members_idx" ON "mal__anime" (members DESC);
CREATE TABLE IF NOT EXISTS "mal__classification" (medium TEXT, axis TEXT, value TEXT, mal_id INTEGER);
CREATE INDEX IF NOT EXISTS "mal__idx_classification" ON "mal__classification" (medium, axis, value, mal_id);
CREATE TABLE IF NOT EXISTS "mal__classification_count" (medium TEXT, axis TEXT, value TEXT, total INTEGER,
            PRIMARY KEY (medium, axis, value));
CREATE TABLE IF NOT EXISTS "mal__manga" (mal_id INTEGER PRIMARY KEY, title TEXT, title_english TEXT,
            type TEXT, score REAL, scored_by INTEGER, members INTEGER, status TEXT,
            volumes INTEGER, chapters INTEGER, year INTEGER, cover TEXT, synopsis TEXT,
            credits TEXT, tags TEXT,
            explicit INTEGER NOT NULL DEFAULT 0, start_date TEXT NOT NULL DEFAULT '',
            favorites INTEGER NOT NULL DEFAULT 0, norm_title TEXT, norm_title_english TEXT);
CREATE INDEX IF NOT EXISTS "mal__manga_norm_title_english_idx" ON "mal__manga" (norm_title_english);
CREATE INDEX IF NOT EXISTS "mal__manga_norm_title_idx" ON "mal__manga" (norm_title);
CREATE INDEX IF NOT EXISTS "mal__manga_start_date_idx" ON "mal__manga" (start_date DESC);
CREATE INDEX IF NOT EXISTS "mal__manga_score_votes_idx" ON "mal__manga" (score DESC, scored_by DESC);
CREATE INDEX IF NOT EXISTS "mal__manga_members_idx" ON "mal__manga" (members DESC);
CREATE TABLE IF NOT EXISTS "mal__meta" (key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE IF NOT EXISTS "mal__tag" (medium TEXT, tag TEXT, mal_id INTEGER);
CREATE INDEX IF NOT EXISTS "mal__idx_tag" ON "mal__tag" (medium, tag, mal_id);
CREATE TABLE IF NOT EXISTS "mal__tag_count" (medium TEXT, tag TEXT, total INTEGER,
            PRIMARY KEY (medium, tag));
CREATE TABLE IF NOT EXISTS "comics__canonical_issue" (
        gcd_issue_id integer primary key,
        gcd_series_id integer not null,
        number text,
        publication_date text);
CREATE INDEX IF NOT EXISTS "comics__ix_canonical_issue_number" ON "comics__canonical_issue" (gcd_series_id, number);
CREATE INDEX IF NOT EXISTS "comics__ix_canonical_issue_series" ON "comics__canonical_issue" (gcd_series_id);
CREATE TABLE IF NOT EXISTS "comics__curated_edition" (
        id integer primary key autoincrement, locg_id text, title text,
        display_title text, format text, collects text, isbn text,
        pages int, published text, chid text, cover text, available int,
        getcomics_post text, creators text, description text);
CREATE INDEX IF NOT EXISTS "comics__ix_curated_edition_locg" ON "comics__curated_edition" (locg_id);
CREATE TABLE IF NOT EXISTS "comics__curated_genre" (locg_id text, genre text);
CREATE INDEX IF NOT EXISTS "comics__ix_curated_genre_locg" ON "comics__curated_genre" (locg_id);
CREATE TABLE IF NOT EXISTS "comics__curated_series" (
        locg_id text primary key, rank int, title text, norm_title text,
        year int, slug text, publisher text, cover text, synopsis text);
CREATE INDEX IF NOT EXISTS "comics__ix_curated_series_norm" ON "comics__curated_series" (norm_title);
CREATE TABLE IF NOT EXISTS "comics__curated_series_coverage" (
        locg_id text primary key,
        gcd_series_id integer,
        canonical_issue_count integer not null,
        covered_issue_count integer not null,
        coverage_ratio real not null,
        availability_state text not null,
        source_count integer not null,
        official_source_count integer not null,
        community_source_count integer not null,
        latest_source_date text,
        computed_at text,
        parser_version integer not null);
CREATE INDEX IF NOT EXISTS "comics__ix_curated_coverage_latest" ON "comics__curated_series_coverage" (latest_source_date);
CREATE INDEX IF NOT EXISTS "comics__ix_curated_coverage_state" ON "comics__curated_series_coverage" (availability_state);
CREATE TABLE IF NOT EXISTS "comics__curated_source" (
        id integer primary key autoincrement,
        curated_edition_id integer not null,
        post_id integer not null,
        post_url text not null,
        source_title text not null,
        source_date text,
        source_cover text,
        source_kind text not null,
        source_format text,
        attachment_method text not null,
        confidence_class text not null,
        fan_made integer not null default 0,
        available integer not null default 0,
        parsed_series_text text,
        formats_json text not null default '[]',
        qualifiers_json text not null default '[]',
        parser_version integer not null,
        unique(curated_edition_id, post_id));
CREATE INDEX IF NOT EXISTS "comics__ix_curated_source_conf" ON "comics__curated_source" (confidence_class, available, fan_made);
CREATE INDEX IF NOT EXISTS "comics__ix_curated_source_date" ON "comics__curated_source" (source_date);
CREATE INDEX IF NOT EXISTS "comics__ix_curated_source_post" ON "comics__curated_source" (post_id);
CREATE INDEX IF NOT EXISTS "comics__ix_curated_source_edition" ON "comics__curated_source" (curated_edition_id);
CREATE TABLE IF NOT EXISTS "comics__curated_source_claim" (
        source_id integer not null,
        claim_index integer not null,
        unit text not null,
        start_value text,
        end_value text,
        value_list text not null default '[]',
        claim_kind text not null,
        raw_fragment text,
        primary key(source_id, claim_index));
CREATE INDEX IF NOT EXISTS "comics__ix_curated_claim_source" ON "comics__curated_source_claim" (source_id);
CREATE TABLE IF NOT EXISTS "comics__curated_source_issue" (
        source_id integer not null,
        canonical_issue_id integer not null,
        mapping_evidence text not null,
        verified_at text,
        parser_version integer not null,
        primary key(source_id, canonical_issue_id));
CREATE INDEX IF NOT EXISTS "comics__ix_curated_source_issue_issue" ON "comics__curated_source_issue" (canonical_issue_id);
CREATE TABLE IF NOT EXISTS "comics__download" (
        post_id integer primary key, series_id int, title text, link text,
        date text, kind text, method text, fan_made int,
        year_start int);
CREATE INDEX IF NOT EXISTS "comics__ix_dl_series" ON "comics__download" (series_id);
CREATE TABLE IF NOT EXISTS "comics__download_mirror" (
        post_id integer, url text, host text, label text);
CREATE INDEX IF NOT EXISTS "comics__ix_mirror_post" ON "comics__download_mirror" (post_id);
CREATE TABLE IF NOT EXISTS "comics__series" (
        gcd_id integer primary key, title text, year int, year_ended int,
        issue_count int, publisher text, cover text, synopsis text);
CREATE INDEX IF NOT EXISTS "comics__ix_series_title" ON "comics__series" (title collate nocase);
CREATE TABLE IF NOT EXISTS "comics__series_stats" (
        series_id integer primary key, downloads int, kinds text,
        latest_post text);
CREATE TABLE IF NOT EXISTS "tankoban__series" (
            mal_id INTEGER PRIMARY KEY,
            volume_count INTEGER NOT NULL,
            count_basis TEXT NOT NULL
        );
CREATE TABLE IF NOT EXISTS "tankoban__volumes" (
            mal_id INTEGER NOT NULL,
            number TEXT NOT NULL,
            cover_url TEXT NOT NULL DEFAULT '',
            name TEXT NOT NULL DEFAULT ''
        );
CREATE INDEX IF NOT EXISTS "tankoban__volumes_mal_id_idx" ON "tankoban__volumes" (mal_id);
