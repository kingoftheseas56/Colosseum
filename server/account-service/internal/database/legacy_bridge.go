package database

import (
	"context"
	"crypto/sha256"
	"embed"
	"fmt"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"
)

// This migration is never invoked by service startup or RunMigrations.
// It is an explicit, fail-closed operator transition for a single historical
// shape. The old three divergent receipts are archived WITHOUT renaming or
// claiming they executed the new SQL. Canonical target receipts are issued
// only after the forward DDL and full target physical check both succeed.
//
//go:embed legacy_bridge_005.sql legacy_bridge_009.sql
var legacyBridgeSQL embed.FS

var verifiedLegacyNames = []string{
	"0001_bootstrap.sql",
	"0002_identity_security.sql",
	"0003_sync_core.sql",
	"0004_trusted_recovery_retry.sql",
	"0005_profile_portable_sync.sql",
	"0006_activity_fact_hlc.sql",
	"0007_activity_attachment_id.sql",
}

var verifiedCanonicalNames = []string{
	"0001_bootstrap.sql",
	"0002_identity_security.sql",
	"0003_sync_core.sql",
	"0004_trusted_recovery_retry.sql",
	"0005_activity_fact_sync.sql",
	"0006_sync_mutation_aliases.sql",
	"0007_account_lifecycle.sql",
	"0008_domain_sync_semantics.sql",
	"0009_profile_attachments.sql",
	"0010_ratings_reviews_sync.sql",
	"0011_collection_publications.sql",
}

// Independent, reviewed SHA-256 of canonicalMigrationSQL for the exact
// historical target sources plus explicitly pinned additive migrations.
// In particular, 0005 and 0009 are substituted by
// forward bridge DDL; a same-name edited canonical file is NOT equivalent.
// Each digest is indexed by the corresponding verifiedCanonicalNames entry.
var verifiedCanonicalSHA256 = [...]string{
	"00cb4e49790795b2f89383fa0cf46ca4000e6fcf2cf708761c966a8c6ac91482", // 0001_bootstrap.sql
	"79c7da98a677410cf62e8bea50455fd6bfa9f99eead3618f6fe5a4b99cbc6bc7", // 0002_identity_security.sql
	"e63cfc2d7d64a5f632720cb84b8384f57ce4c04a3ca9c02b019339764cafb510", // 0003_sync_core.sql
	"d6127bee25a9f078ddff5ec8c1bdff15f674d692a66461983fa8fb2672c8d6ae", // 0004_trusted_recovery_retry.sql
	"9c682592c0cbfc7bebde3be459408728f395f68e46eaf5533027ab399544d2a8", // 0005_activity_fact_sync.sql
	"3c6d7c905aea5bf124b88876c5c4d50252090264e1ff3e1443a010cf310c474e", // 0006_sync_mutation_aliases.sql
	"2c1ed6731c6541e9caff045a56d5a51fe83bdb80ac14ca20eb472884ac99e4df", // 0007_account_lifecycle.sql
	"b2f97d1f90b475a8c91ec44f0f9644857ba3d9b4999539f48d736ac60ded1bd7", // 0008_domain_sync_semantics.sql
	"3f4a2b59e0c4ae7d676571e0c17b89e5371d1f359a299fcbb40764c50f613b6c", // 0009_profile_attachments.sql
	"b4cd7041a534459fe49c2c36c3cc1cac5fca9795849f2f9e5c21e7a5c0b3e8fe", // 0010_ratings_reviews_sync.sql
	"f4276dd8012496a877b6ce65793f0c6c51c34a03ceddd575498e6567a8cf11eb", // 0011_collection_publications.sql
}

// RunLegacyBridge is a fixture/test entrypoint using the same transaction and
// advisory lock as the production-only direct-connection variant.
func RunLegacyBridge(ctx context.Context, pool *pgxpool.Pool) error {
	if pool == nil {
		return fmt.Errorf("legacy bridge pool is required")
	}
	return runLegacyBridge(ctx, pool)
}

func RunLegacyBridgeOnConn(ctx context.Context, conn *pgx.Conn) error {
	if conn == nil {
		return fmt.Errorf("legacy bridge direct connection is required")
	}
	return runLegacyBridge(ctx, conn)
}

func runLegacyBridge(ctx context.Context, beginner migrationBeginner) error {
	return runLegacyBridgeWithSource(ctx, beginner, embeddedMigrations)
}

// The loader is an internal regression-test seam; production always uses
// embeddedMigrations. Neither entrypoint skips the reviewed source pin.
func runLegacyBridgeWithSource(ctx context.Context, beginner migrationBeginner,
	load func() ([]migration, error)) error {
	tx, err := beginner.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return fmt.Errorf("begin legacy bridge: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()
	if _, err := tx.Exec(ctx, "SELECT pg_advisory_xact_lock($1)", migrationLockKey); err != nil {
		return fmt.Errorf("lock legacy bridge: %w", err)
	}

	canonical, err := load()
	if err != nil {
		return err
	}
	if err := verifyReviewedCanonicalSource(canonical); err != nil {
		return err
	}

	var alreadyBridged bool
	if err := tx.QueryRow(ctx, `
        SELECT to_regclass('public.account_legacy_migration_receipts') IS NOT NULL
    `).Scan(&alreadyBridged); err != nil {
		return legacyReject("archive_catalog_unavailable")
	}
	if alreadyBridged {
		// Idempotency is NOT a bypass: a drifted/mixed canonical ledger, an
		// altered archive, or damaged physical schema still fails closed.
		if err := checkSchemaQueryer(ctx, tx, canonical, false); err != nil {
			return err
		}
		if err := verifyLegacyArchive(ctx, tx); err != nil {
			return err
		}
		if err := verifyCompletedBridgeShape(ctx, tx); err != nil {
			return err
		}
		return tx.Commit(ctx)
	}

	if err := verifyExactLegacyPhysical(ctx, tx); err != nil {
		return err
	}
	if err := verifyLegacyLedger(ctx, tx); err != nil {
		return err
	}
	if err := verifyLegacyAttachmentData(ctx, tx); err != nil {
		return err
	}

	if err := executeLegacySQL(ctx, tx, "legacy_bridge_005.sql"); err != nil {
		return err
	}
	// Versions 0006-0008 and 0010-0011 are genuinely executed from their
	// untouched canonical files. 0005 and 0009 have equivalent forward DDL
	// because their CREATE TABLEs would collide with historical objects.
	for _, name := range []string{
		"0006_sync_mutation_aliases.sql",
		"0007_account_lifecycle.sql",
		"0008_domain_sync_semantics.sql",
	} {
		if err := executeCanonicalBridgeMigration(ctx, tx, canonical, name); err != nil {
			return err
		}
	}
	if err := executeLegacySQL(ctx, tx, "legacy_bridge_009.sql"); err != nil {
		return err
	}
	if err := executeCanonicalBridgeMigration(ctx, tx, canonical,
		"0010_ratings_reviews_sync.sql"); err != nil {
		return err
	}
	if err := executeCanonicalBridgeMigration(ctx, tx, canonical, "0011_collection_publications.sql"); err != nil {
		return err
	}

	// Validate BEFORE issuing any canonical receipt. No old account,
	// device, session, sync journal, history, activity fact, attachment, or
	// sequence is deleted, copied into a substitute, or reseeded.
	if err := validateSchemaContract(ctx, tx); err != nil {
		return err
	}
	if err := verifyBridgeSequence(ctx, tx,
		"account_sync_journal_server_seq_seq"); err != nil {
		return err
	}

	if _, err := tx.Exec(ctx, `
        CREATE TABLE account_legacy_migration_receipts (
            name text PRIMARY KEY,
            applied_at timestamptz NOT NULL
        )
    `); err != nil {
		return fmt.Errorf("archive legacy receipts: %w", err)
	}
	if tag, err := tx.Exec(ctx, `
        INSERT INTO account_legacy_migration_receipts(name, applied_at)
        SELECT name, applied_at FROM schema_migrations
    `); err != nil || tag.RowsAffected() != 7 {
		return legacyReject("legacy_receipt_archive_failed")
	}

	// Only here do old divergent name-only receipts leave the active ledger.
	// Their names and timestamps remain intact in the immutable-by-procedure
	// archive above. Never convert those old names into canonical names.
	if _, err := tx.Exec(ctx, `
        ALTER TABLE schema_migrations ADD COLUMN checksum text
    `); err != nil {
		return fmt.Errorf("add canonical checksum ledger: %w", err)
	}
	if tag, err := tx.Exec(ctx, `
        DELETE FROM schema_migrations
        WHERE name IN ('0005_profile_portable_sync.sql',
                       '0006_activity_fact_hlc.sql',
                       '0007_activity_attachment_id.sql')
    `); err != nil || tag.RowsAffected() != 3 {
		return legacyReject("legacy_active_receipt_removal_failed")
	}

	// The first four SQL blobs are identical across historical and canonical
	// sources. A target checksum on the remaining six certifies the verified
	// target SCHEMA, not historical execution of the corresponding SQL text.
	for i, m := range canonical {
		if i < 4 {
			tag, err := tx.Exec(ctx, `
                UPDATE schema_migrations SET checksum=$2
                WHERE name=$1 AND checksum IS NULL
            `, m.name, m.checksum)
			if err != nil || tag.RowsAffected() != 1 {
				return legacyReject("shared_receipt_baseline_failed")
			}
			continue
		}
		if _, err := tx.Exec(ctx, `
            INSERT INTO schema_migrations(name, checksum, applied_at)
            VALUES ($1,$2,$3)
        `, m.name, m.checksum, time.Now().UTC()); err != nil {
			return fmt.Errorf("record verified target migration %s: %w", m.name, err)
		}
	}
	if err := checkSchemaQueryer(ctx, tx, canonical, false); err != nil {
		return err
	}
	if err := verifyLegacyArchive(ctx, tx); err != nil {
		return err
	}
	if err := verifyCompletedBridgeShape(ctx, tx); err != nil {
		return err
	}
	if err := tx.Commit(ctx); err != nil {
		return fmt.Errorf("commit legacy bridge: %w", err)
	}
	return nil
}

// Reject same-name SQL changes BEFORE bridge DDL or canonical receipt writes.
// The migrations' self-calculated checksums cannot authorize new content:
// require BOTH the digest of the SQL that would execute and its reported
// checksum to agree with independently reviewed, frozen target digests.
func verifyReviewedCanonicalSource(canonical []migration) error {
	if len(canonical) != len(verifiedCanonicalNames) ||
		len(canonical) != len(verifiedCanonicalSHA256) {
		return legacyReject("unreviewed_canonical_migrations")
	}
	for i, current := range canonical {
		if current.name != verifiedCanonicalNames[i] {
			return legacyReject("unreviewed_canonical_migrations")
		}
		digest := sha256.Sum256([]byte(current.sql))
		if fmt.Sprintf("%x", digest) != verifiedCanonicalSHA256[i] ||
			current.checksum != verifiedCanonicalSHA256[i] {
			return legacyReject("unreviewed_canonical_sql")
		}
	}
	return nil
}

func verifyLegacyLedger(ctx context.Context, q migrationQueryer) error {
	// The physical metadata verifier already required exactly (name,applied_at)
	// and its primary key. Do NOT upgrade metadata before this check.
	rows, err := q.Query(ctx, "SELECT name, applied_at FROM schema_migrations ORDER BY name")
	if err != nil {
		return legacyReject("history_unavailable")
	}
	defer rows.Close()
	var names []string
	for rows.Next() {
		var name string
		var appliedAt time.Time
		if err := rows.Scan(&name, &appliedAt); err != nil {
			return legacyReject("history_unavailable")
		}
		if appliedAt.IsZero() {
			return legacyReject("history_invalid")
		}
		names = append(names, name)
	}
	if err := rows.Err(); err != nil {
		return legacyReject("history_unavailable")
	}
	if !equalStrings(names, verifiedLegacyNames) {
		return legacyReject("unknown_or_hybrid_history")
	}
	return nil
}

func verifyLegacyArchive(ctx context.Context, q migrationQueryer) error {
	rows, err := q.Query(ctx, `
        SELECT name, applied_at FROM account_legacy_migration_receipts ORDER BY name
    `)
	if err != nil {
		return legacyReject("archive_invalid")
	}
	defer rows.Close()
	var names []string
	for rows.Next() {
		var name string
		var appliedAt time.Time
		if err := rows.Scan(&name, &appliedAt); err != nil || appliedAt.IsZero() {
			return legacyReject("archive_invalid")
		}
		names = append(names, name)
	}
	if err := rows.Err(); err != nil || !equalStrings(names, verifiedLegacyNames) {
		return legacyReject("archive_invalid")
	}
	return nil
}

func verifyLegacyAttachmentData(ctx context.Context, q migrationQueryer) error {
	// Only a boolean is read. Invalid old digests cannot be silently coerced
	// to the new length constraint. The DDL will preserve every old digest.
	var invalid bool
	if err := q.QueryRow(ctx, `
        SELECT EXISTS (
            SELECT 1 FROM account_device_attachments
            WHERE length(source_semantic_digest) = 0
               OR length(source_semantic_digest) > 256
        )
    `).Scan(&invalid); err != nil {
		return legacyReject("attachment_check_unavailable")
	}
	if invalid {
		return legacyReject("legacy_attachment_digest_incompatible")
	}
	return nil
}

func executeLegacySQL(ctx context.Context, tx pgx.Tx, path string) error {
	sql, err := legacyBridgeSQL.ReadFile(path)
	if err != nil {
		return fmt.Errorf("read bridge SQL: %w", err)
	}
	if _, err := tx.Conn().PgConn().Exec(ctx, string(sql)).ReadAll(); err != nil {
		return fmt.Errorf("apply explicit legacy bridge %s: %w", path, err)
	}
	return nil
}

func executeCanonicalBridgeMigration(ctx context.Context, tx pgx.Tx,
	migrations []migration, name string) error {
	for _, current := range migrations {
		if current.name == name {
			if _, err := tx.Conn().PgConn().Exec(ctx, current.sql).ReadAll(); err != nil {
				return fmt.Errorf("apply canonical transition %s: %w", name, err)
			}
			return nil
		}
	}
	return legacyReject("missing_canonical_transition")
}
