package database

import (
	"crypto/sha256"
	"fmt"
	"strings"
	"testing"
)

// The pin is independent of receipts computed from the current embedded SQL.
// This simulates an edited SQL file that keeps the canonical name and has a
// correctly recomputed checksum. It MUST NOT acquire approval by self-hashing.
func TestLegacyBridgeCanonicalContentPin(t *testing.T) {
	canonical, err := embeddedMigrations()
	if err != nil {
		t.Fatal(err)
	}
	if err := verifyReviewedCanonicalSource(canonical); err != nil {
		t.Fatalf("reviewed canonical SQL rejected: %v", err)
	}
	for i, original := range canonical {
		t.Run(original.name, func(t *testing.T) {
			changed := append([]migration(nil), canonical...)
			changed[i].sql += "\n-- synthetic drift with the SAME filename"
			digest := sha256.Sum256([]byte(changed[i].sql))
			changed[i].checksum = fmt.Sprintf("%x", digest)
			if err := verifyReviewedCanonicalSource(changed); err == nil ||
				!strings.Contains(err.Error(), "unreviewed_canonical_sql") {
				t.Fatalf("same-name SQL and matching changed checksum admitted: %v", err)
			}
		})
	}
}

// Uses the existing testdb helper's dedicated *_test database reset guard.
// No DDL should execute if 0005 or 0009 changes under the reviewed filename.
func TestLegacyBridgeSameNameSQLDriftRejectsBeforeDDL(t *testing.T) {
	for _, name := range []string{
		"0005_activity_fact_sync.sql",
		"0009_profile_attachments.sql",
	} {
		t.Run(name, func(t *testing.T) {
			pool, ctx := createLegacyBridgeFixture(t)
			seedLegacySentinels(t, ctx, pool)
			before := snapshotSentinels(t, ctx, pool)
			receipts := snapshotReceipt(t, ctx, pool, "schema_migrations")

			canonical, err := embeddedMigrations()
			if err != nil {
				t.Fatal(err)
			}
			for i := range canonical {
				if canonical[i].name == name {
					canonical[i].sql += "\n-- synthetic same-name substituted SQL"
					digest := sha256.Sum256([]byte(canonical[i].sql))
					canonical[i].checksum = fmt.Sprintf("%x", digest)
				}
			}
			err = runLegacyBridgeWithSource(ctx, pool, func() ([]migration, error) {
				return canonical, nil
			})
			if err == nil || !strings.Contains(err.Error(), "unreviewed_canonical_sql") {
				t.Fatalf("canonical drift was not stopped by source pin: %v", err)
			}
			assertNoBridgeSideEffects(t, ctx, pool, before, receipts, false)
			var originalSequence, createdAlias bool
			if err := pool.QueryRow(ctx, `
                SELECT to_regclass('public.account_change_seq') IS NOT NULL,
                       to_regclass('public.account_sync_mutation_aliases') IS NOT NULL
            `).Scan(&originalSequence, &createdAlias); err != nil {
				t.Fatal(err)
			}
			if !originalSequence || createdAlias {
				t.Fatal("same-name SQL drift caused bridge DDL despite early rejection")
			}
		})
	}
}
