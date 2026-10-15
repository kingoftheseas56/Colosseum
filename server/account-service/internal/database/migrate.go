package database

import (
	"context"
	"crypto/sha256"
	"embed"
	"encoding/hex"
	"fmt"
	"io/fs"
	"sort"
	"strconv"
	"strings"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"
)

// migrationFiles is embedded in both the service and the migration command.
// The service only reads schema_migrations at runtime; only the explicit
// migration command calls RunMigrations.
//
//go:embed migrations/*.sql
var migrationFiles embed.FS

const migrationLockKey int64 = 0x434f4c4f53534555

// RunMigrations applies the embedded forward-only migrations through one
// transaction-scoped advisory lock. It remains available to deterministic
// local tests; production uses RunMigrationsOnConn from the standalone
// migration command so runtime credentials never need DDL privileges.
func RunMigrations(ctx context.Context, pool *pgxpool.Pool) error {
	if pool == nil {
		return fmt.Errorf("migration pool is required")
	}
	return runMigrations(ctx, pool)
}

// RunMigrationsOnConn is the production migration path. A pgx.Conn is a
// single direct connection, so this run cannot silently use the pooled/runtime
// endpoint or consume a pool slot while it holds the DDL lock.
func RunMigrationsOnConn(ctx context.Context, conn *pgx.Conn) error {
	if conn == nil {
		return fmt.Errorf("migration connection is required")
	}
	return runMigrations(ctx, conn)
}

type migrationBeginner interface {
	BeginTx(context.Context, pgx.TxOptions) (pgx.Tx, error)
}

func runMigrations(ctx context.Context, beginner migrationBeginner) error {
	tx, err := beginner.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return fmt.Errorf("begin migration transaction: %w", err)
	}
	defer func() {
		_ = tx.Rollback(ctx)
	}()

	if _, err := tx.Exec(ctx, "SELECT pg_advisory_xact_lock($1)", migrationLockKey); err != nil {
		return fmt.Errorf("acquire migration lock: %w", err)
	}

	migrations, err := embeddedMigrations()
	if err != nil {
		return err
	}
	if err := ensureMigrationMetadata(ctx, tx); err != nil {
		return err
	}

	applied, err := loadAppliedMigrations(ctx, tx)
	if err != nil {
		return err
	}
	if err := validateKnownMigrationRows(applied, migrations); err != nil {
		return err
	}

	for index, current := range migrations {
		row, found := applied[current.name]
		if found {
			if row.checksumSet && row.checksum != current.checksum {
				return migrationChecksumError(current.name)
			}
			continue
		}

		// A later migration row without this one means the database history is
		// incomplete. Refuse to guess which schema objects were applied.
		for _, later := range migrations[index+1:] {
			if _, laterFound := applied[later.name]; laterFound {
				return fmt.Errorf("migration history is missing %s before applied %s",
					current.name, later.name)
			}
		}

		if _, err := tx.Conn().PgConn().Exec(ctx, current.sql).ReadAll(); err != nil {
			return fmt.Errorf("apply migration %s: %w", current.name, err)
		}
		if _, err := tx.Exec(ctx,
			"INSERT INTO schema_migrations(name, checksum) VALUES($1, $2)",
			current.name, current.checksum); err != nil {
			return fmt.Errorf("record migration %s: %w", current.name, err)
		}
		applied[current.name] = appliedMigration{
			name:        current.name,
			checksum:    current.checksum,
			checksumSet: true,
		}
	}

	// An old runner recorded names only. Validate the complete schema before
	// accepting the first canonical checksum baseline; the baseline proves
	// what this binary expects from this point forward, but cannot prove which
	// SQL text an older deployment used in the past.
	if err := checkSchemaQueryer(ctx, tx, migrations, true); err != nil {
		return err
	}
	for _, current := range migrations {
		row := applied[current.name]
		if row.checksumSet {
			continue
		}
		if _, err := tx.Exec(ctx,
			"UPDATE schema_migrations SET checksum = $2 WHERE name = $1 AND checksum IS NULL",
			current.name, current.checksum); err != nil {
			return fmt.Errorf("record legacy checksum baseline for %s: %w", current.name, err)
		}
	}

	if err := tx.Commit(ctx); err != nil {
		return fmt.Errorf("commit migrations: %w", err)
	}
	return nil
}

type migration struct {
	name     string
	sql      string
	checksum string
	version  int
}

type appliedMigration struct {
	name        string
	checksum    string
	checksumSet bool
}

func embeddedMigrations() ([]migration, error) {
	entries, err := fs.ReadDir(migrationFiles, "migrations")
	if err != nil {
		return nil, fmt.Errorf("read embedded migrations: %w", err)
	}

	names := make([]string, 0, len(entries))
	for _, entry := range entries {
		if entry.IsDir() || !strings.HasSuffix(entry.Name(), ".sql") {
			continue
		}
		names = append(names, entry.Name())
	}
	sort.Strings(names)

	migrations := make([]migration, 0, len(names))
	seenVersions := make(map[int]struct{}, len(names))
	for _, name := range names {
		contents, err := migrationFiles.ReadFile("migrations/" + name)
		if err != nil {
			return nil, fmt.Errorf("read migration %s: %w", name, err)
		}
		sql := canonicalMigrationSQL(string(contents))
		if sql == "" {
			return nil, fmt.Errorf("migration %s is empty", name)
		}
		version, err := migrationVersion(name)
		if err != nil {
			return nil, err
		}
		if _, duplicate := seenVersions[version]; duplicate {
			return nil, fmt.Errorf("migration version %04d is duplicated", version)
		}
		seenVersions[version] = struct{}{}
		digest := sha256.Sum256([]byte(sql))
		migrations = append(migrations, migration{
			name:     name,
			sql:      sql,
			checksum: hex.EncodeToString(digest[:]),
			version:  version,
		})
	}
	return migrations, nil
}

func canonicalMigrationSQL(sql string) string {
	return strings.TrimSpace(strings.ReplaceAll(sql, "\r\n", "\n"))
}

func migrationVersion(name string) (int, error) {
	if len(name) < 5 || name[4] != '_' {
		return 0, fmt.Errorf("migration %s does not start with a four-digit version", name)
	}
	version, err := strconv.Atoi(name[:4])
	if err != nil || version <= 0 {
		return 0, fmt.Errorf("migration %s has an invalid version", name)
	}
	return version, nil
}

func ensureMigrationMetadata(ctx context.Context, tx pgx.Tx) error {
	if _, err := tx.Exec(ctx, `
        CREATE TABLE IF NOT EXISTS schema_migrations (
            name text PRIMARY KEY,
            checksum text,
            applied_at timestamptz NOT NULL DEFAULT now()
        )
    `); err != nil {
		return fmt.Errorf("ensure schema_migrations: %w", err)
	}
	// Existing databases were created by the name-only runner. This ALTER is
	// deliberately reachable only from the explicit migration transaction.
	if _, err := tx.Exec(ctx,
		"ALTER TABLE schema_migrations ADD COLUMN IF NOT EXISTS checksum text"); err != nil {
		return fmt.Errorf("upgrade schema_migrations metadata: %w", err)
	}
	return nil
}

func loadAppliedMigrations(ctx context.Context, queryer migrationQueryer) (map[string]appliedMigration, error) {
	rows, err := queryer.Query(ctx,
		"SELECT name, checksum FROM schema_migrations ORDER BY name")
	if err != nil {
		return nil, fmt.Errorf("read schema migration metadata: %w", err)
	}
	defer rows.Close()

	result := make(map[string]appliedMigration)
	for rows.Next() {
		var row appliedMigration
		var checksum *string
		if err := rows.Scan(&row.name, &checksum); err != nil {
			return nil, fmt.Errorf("scan schema migration metadata: %w", err)
		}
		if checksum != nil {
			row.checksum = strings.TrimSpace(*checksum)
			row.checksumSet = true
		}
		if _, duplicate := result[row.name]; duplicate {
			return nil, fmt.Errorf("schema migration %s is recorded more than once", row.name)
		}
		result[row.name] = row
	}
	if err := rows.Err(); err != nil {
		return nil, fmt.Errorf("read schema migration metadata: %w", err)
	}
	return result, nil
}

type migrationQueryer interface {
	Query(context.Context, string, ...any) (pgx.Rows, error)
	QueryRow(context.Context, string, ...any) pgx.Row
}

func validateKnownMigrationRows(
	applied map[string]appliedMigration,
	migrations []migration,
) error {
	known := make(map[string]struct{}, len(migrations))
	for _, current := range migrations {
		known[current.name] = struct{}{}
	}
	for name := range applied {
		if _, ok := known[name]; !ok {
			return fmt.Errorf("unsupported future schema migration %s", name)
		}
	}
	return nil
}

func migrationChecksumError(name string) error {
	return fmt.Errorf("migration checksum drift detected for %s", name)
}

// CheckSchema is the runtime compatibility gate. It performs only SELECTs;
// in particular it never creates or alters schema metadata. Runtime roles
// can therefore be granted application DML without granting public-schema
// CREATE/ALTER/DROP.
func CheckSchema(ctx context.Context, pool *pgxpool.Pool) error {
	if pool == nil {
		return schemaCompatibilityError("database_unavailable")
	}
	migrations, err := embeddedMigrations()
	if err != nil {
		return schemaCompatibilityError("embedded_migrations_unavailable")
	}
	return checkSchemaQueryer(ctx, pool, migrations, false)
}

// CheckSchemaOnConn is useful to migration/operator tests and keeps the
// compatibility query independent of pooling.
func CheckSchemaOnConn(ctx context.Context, conn *pgx.Conn) error {
	if conn == nil {
		return schemaCompatibilityError("database_unavailable")
	}
	migrations, err := embeddedMigrations()
	if err != nil {
		return schemaCompatibilityError("embedded_migrations_unavailable")
	}
	return checkSchemaQueryer(ctx, conn, migrations, false)
}

// PoolSchemaChecker adapts the database gate to the HTTP readiness interface
// without making the HTTP package depend on pgxpool.
type PoolSchemaChecker struct {
	Pool *pgxpool.Pool
}

func (c PoolSchemaChecker) CheckSchema(ctx context.Context) error {
	return CheckSchema(ctx, c.Pool)
}

func checkSchemaQueryer(
	ctx context.Context,
	queryer migrationQueryer,
	migrations []migration,
	allowLegacyChecksums bool,
) error {
	if queryer == nil {
		return schemaCompatibilityError("database_unavailable")
	}

	rows, err := queryer.Query(ctx,
		"SELECT name, checksum FROM schema_migrations ORDER BY name")
	if err != nil {
		return schemaCompatibilityError("migration_metadata_unavailable")
	}
	applied := make(map[string]appliedMigration, len(migrations))
	for rows.Next() {
		var row appliedMigration
		var checksum *string
		if err := rows.Scan(&row.name, &checksum); err != nil {
			rows.Close()
			return schemaCompatibilityError("migration_metadata_unavailable")
		}
		if checksum != nil {
			row.checksum = strings.TrimSpace(*checksum)
			row.checksumSet = true
		}
		if _, duplicate := applied[row.name]; duplicate {
			rows.Close()
			return schemaCompatibilityError("migration_metadata_invalid")
		}
		applied[row.name] = row
	}
	if err := rows.Err(); err != nil {
		rows.Close()
		return schemaCompatibilityError("migration_metadata_unavailable")
	}
	rows.Close()

	if err := validateKnownMigrationRows(applied, migrations); err != nil {
		return schemaCompatibilityError("unsupported_future_schema")
	}
	for _, current := range migrations {
		row, ok := applied[current.name]
		if !ok {
			return schemaCompatibilityError("missing_schema_migration")
		}
		if !row.checksumSet {
			if !allowLegacyChecksums {
				return schemaCompatibilityError("legacy_checksum_baseline_pending")
			}
			continue
		}
		if row.checksum != current.checksum {
			return schemaCompatibilityError("migration_checksum_drift")
		}
	}

	if err := validateSchemaContract(ctx, queryer); err != nil {
		return err
	}
	return nil
}

type schemaCompatibilityError string

func (e schemaCompatibilityError) Error() string {
	return "schema compatibility: " + string(e)
}

// SupportedSchemaVersionRange returns the version range this binary can
// understand. The maximum is derived from embedded migration filenames so a
// future additive migration cannot silently be treated as supported by an
// older runtime.
func SupportedSchemaVersionRange() (minimum int, maximum int, err error) {
	migrations, err := embeddedMigrations()
	if err != nil {
		return 0, 0, err
	}
	if len(migrations) == 0 {
		return 0, 0, fmt.Errorf("no embedded migrations")
	}
	minimum = migrations[0].version
	maximum = migrations[0].version
	for _, current := range migrations[1:] {
		if current.version < minimum {
			minimum = current.version
		}
		if current.version > maximum {
			maximum = current.version
		}
	}
	return minimum, maximum, nil
}
