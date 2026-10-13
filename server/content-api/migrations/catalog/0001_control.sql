CREATE TABLE IF NOT EXISTS _source_tables (
  name TEXT PRIMARY KEY, source_hash TEXT NOT NULL, expected_rows INTEGER NOT NULL,
  imported_rows INTEGER NOT NULL DEFAULT 0, ready INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS _import_budget (day TEXT PRIMARY KEY, reserved_writes INTEGER NOT NULL);
CREATE TRIGGER IF NOT EXISTS _source_limit BEFORE UPDATE OF imported_rows ON _source_tables
WHEN OLD.ready=1 OR NEW.imported_rows>NEW.expected_rows BEGIN
  SELECT RAISE(ABORT, 'TABLE_FULL');
END;
CREATE TABLE IF NOT EXISTS _import_chunks (
  id TEXT PRIMARY KEY, payload_hash TEXT NOT NULL, day TEXT NOT NULL, estimated_writes INTEGER NOT NULL
);
CREATE TRIGGER IF NOT EXISTS _import_quota BEFORE INSERT ON _import_chunks
WHEN COALESCE((SELECT reserved_writes FROM _import_budget WHERE day=NEW.day),0)
    + NEW.estimated_writes > 60000 BEGIN
  SELECT RAISE(ABORT, 'IMPORT_DAILY_BUDGET');
END;
CREATE TRIGGER IF NOT EXISTS _import_charge AFTER INSERT ON _import_chunks BEGIN
  INSERT INTO _import_budget(day,reserved_writes) VALUES(NEW.day,NEW.estimated_writes)
    ON CONFLICT(day) DO UPDATE SET reserved_writes=reserved_writes+NEW.estimated_writes;
END;
