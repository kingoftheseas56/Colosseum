package database

import (
	"context"
	"fmt"
	"sort"
	"strings"
)

// The historical seven-file source is Git b9e2eb76409c86bd3f73b9dab06c2ffeac13c683.
// It shares exact Git blobs 0001-0004 with the frozen 11869834 source.
// Names are NOT proof of historical SQL execution: the preflight below also
// requires the physical shape left by precisely that historical lineage.
func legacyBridgeContracts() []schemaTableContract {
	var result []schemaTableContract
	for _, item := range schemaContract() {
		switch item.name {
		case "account_sync_journal":
			for _, col := range []string{
				"materialized_payload_ciphertext", "materialized_hlc_physical_ms",
				"materialized_hlc_counter", "materialized_device_id",
			} {
				delete(item.columns, col)
			}
			item.checks = []string{
				"account_sync_journal_operation_ck", "account_sync_journal_payload_ck",
				"account_sync_journal_schema_ck", "account_sync_journal_counter_ck",
			}
			delete(item.checkExpressions, "account_sync_journal_materialized_hlc_ck")
		case "account_activity_facts":
			item.columns["schema_version"] = schemaColumnContract{dataType: "smallint", notNull: true}
			delete(item.columns, "suppressed")
		case "account_device_attachments":
			for _, col := range []string{
				"source_profile_id", "source_activity_digest", "manifest_digest", "manifest_count",
			} {
				delete(item.columns, col)
			}
			item.checks = []string{
				"account_device_attachments_state_ck",
				"account_device_attachments_baseline_ck",
			}
			for _, name := range []string{
				"account_device_attachments_manifest_count_ck",
				"account_device_attachments_digest_ck",
				"account_device_attachments_manifest_digest_ck",
			} {
				delete(item.checkExpressions, name)
			}
		case "account_sync_current", "account_sync_versions":
			// The frozen schema contract contains only the shared base fields.
		case "service_metadata", "username_reservations", "accounts",
			"devices", "sessions", "device_signin_challenges",
			"trusted_recovery_challenges", "auth_rate_events",
			"account_security_events", "avatar_cleanup_queue":
			// The initial four SQL files are byte-identical across histories.
		default:
			// Later canonical-only relations MUST be absent, not ignored.
			continue
		}
		result = append(result, item)
	}
	return result
}

// Exact public table and index names are derived from the historical seven
// SQL files, including PostgreSQL's generated primary/unique index names.
// A hybrid, future, or manually reshaped schema is not silently admitted.
var legacyTables = []string{
	"account_activity_facts", "account_device_attachments",
	"account_security_events", "account_sync_current", "account_sync_journal",
	"account_sync_versions", "accounts", "auth_rate_events",
	"avatar_cleanup_queue", "device_signin_challenges", "devices",
	"schema_migrations", "service_metadata", "sessions",
	"trusted_recovery_challenges", "username_reservations",
}

var legacyIndexes = map[string][]string{
	"account_activity_facts": {"account_activity_facts_pkey",
		"account_activity_facts_mutation_uk", "account_activity_facts_seq_uk",
		"account_activity_facts_pull_idx", "account_activity_facts_attachment_idx"},
	"account_device_attachments": {"account_device_attachments_pkey", "account_device_attachments_open_idx"},
	"account_security_events":    {"account_security_events_pkey", "account_security_events_account_idx"},
	"account_sync_current":       {"account_sync_current_pkey"},
	"account_sync_journal": {"account_sync_journal_pkey", "account_sync_journal_mutation_uk",
		"account_sync_journal_pull_idx", "account_sync_journal_attachment_idx"},
	"account_sync_versions": {"account_sync_versions_pkey", "account_sync_versions_recovery_idx"},
	"accounts":              {"accounts_pkey", "accounts_canonical_username_key"},
	"auth_rate_events":      {"auth_rate_events_pkey", "auth_rate_events_lookup_idx"},
	"avatar_cleanup_queue": {"avatar_cleanup_queue_pkey", "avatar_cleanup_queue_object_key_key",
		"avatar_cleanup_queue_due_idx"},
	"device_signin_challenges": {"device_signin_challenges_pkey",
		"device_signin_challenges_challenge_token_hash_key", "device_signin_challenges_pending_idx"},
	"devices":           {"devices_pkey", "devices_account_id_install_id_key"},
	"schema_migrations": {"schema_migrations_pkey"},
	"service_metadata":  {"service_metadata_pkey"},
	"sessions": {"sessions_pkey", "sessions_access_token_hash_key",
		"sessions_refresh_token_hash_key", "sessions_one_active_per_device_idx",
		"sessions_account_active_idx", "sessions_previous_refresh_idx"},
	"trusted_recovery_challenges": {"trusted_recovery_challenges_pkey",
		"trusted_recovery_challenges_challenge_token_hash_key",
		"trusted_recovery_challenges_pending_idx"},
	"username_reservations": {"username_reservations_pkey"},
}

func verifyExactLegacyPhysical(ctx context.Context, q migrationQueryer) error {
	if err := verifyPublicRelations(ctx, q, 'r', legacyTables); err != nil {
		return err
	}
	if err := verifyPublicRelations(ctx, q, 'S', []string{
		"account_change_seq", "account_security_events_id_seq",
		"account_sync_versions_id_seq", "auth_rate_events_id_seq",
		"avatar_cleanup_queue_id_seq",
	}); err != nil {
		return err
	}
	contracts := legacyBridgeContracts()
	names := make([]string, 0, len(contracts))
	for _, c := range contracts {
		names = append(names, c.name)
	}
	columns, err := catalogColumnsBatch(ctx, q, names)
	if err != nil {
		return legacyReject("columns_unavailable")
	}
	keys, err := catalogKeysBatch(ctx, q, names)
	if err != nil {
		return legacyReject("keys_unavailable")
	}
	fks, err := catalogForeignKeysBatch(ctx, q, names)
	if err != nil {
		return legacyReject("foreign_keys_unavailable")
	}
	checks, err := catalogChecksBatch(ctx, q, names)
	if err != nil {
		return legacyReject("checks_unavailable")
	}

	for _, c := range contracts {
		actual := columns[c.name]
		if len(actual) != len(c.columns) {
			return legacyReject("columns_mismatch")
		}
		for name, want := range c.columns {
			got, ok := actual[name]
			if !ok || got.dataType != want.dataType || got.notNull != want.notNull {
				return legacyReject("columns_mismatch")
			}
		}
		for name, want := range c.defaults {
			if normalizeDefaultExpression(actual[name].defaultExpr) != normalizeDefaultExpression(want) {
				return legacyReject("defaults_mismatch")
			}
		}
		if len(keys[c.name]["p"]) != 1 || !containsKey(keys[c.name]["p"], c.primaryKey) ||
			len(keys[c.name]["u"]) != len(c.uniqueKeys) {
			return legacyReject("keys_mismatch")
		}
		for _, want := range c.uniqueKeys {
			if !containsKey(keys[c.name]["u"], want) {
				return legacyReject("keys_mismatch")
			}
		}
		if len(fks[c.name]) != len(c.foreignKeys) {
			return legacyReject("foreign_keys_mismatch")
		}
		for _, want := range c.foreignKeys {
			if !containsForeignKey(fks[c.name], want) {
				return legacyReject("foreign_keys_mismatch")
			}
		}
		if len(checks[c.name]) != len(c.checks) {
			return legacyReject("checks_mismatch")
		}
		for _, name := range c.checks {
			got, ok := checks[c.name][name]
			if !ok || !got.validated ||
				normalizeSQLDefinition(got.expression) != normalizeSQLDefinition(c.checkExpressions[name]) {
				return legacyReject("checks_mismatch")
			}
		}
	}

	// Check the migration metadata as an EXACT two-column table, not just a
	// selectable ledger. No checksum column, shadow receipt, or altered key.
	metaCols, err := catalogColumnsBatch(ctx, q, []string{"schema_migrations"})
	if err != nil {
		return legacyReject("metadata_unavailable")
	}
	m := metaCols["schema_migrations"]
	if len(m) != 2 || m["name"].dataType != "text" || !m["name"].notNull ||
		m["applied_at"].dataType != "timestamp with time zone" || !m["applied_at"].notNull ||
		normalizeDefaultExpression(m["applied_at"].defaultExpr) != "now()" {
		return legacyReject("metadata_mismatch")
	}
	metaKeys, err := catalogKeysBatch(ctx, q, []string{"schema_migrations"})
	if err != nil || len(metaKeys["schema_migrations"]["p"]) != 1 ||
		!containsKey(metaKeys["schema_migrations"]["p"], []string{"name"}) ||
		len(metaKeys["schema_migrations"]["u"]) != 0 {
		return legacyReject("metadata_key_mismatch")
	}

	indexes, err := catalogIndexesBatch(ctx, q, legacyTables)
	if err != nil {
		return legacyReject("indexes_unavailable")
	}
	for _, table := range legacyTables {
		if len(indexes[table]) != len(legacyIndexes[table]) {
			return legacyReject("indexes_mismatch")
		}
		for _, name := range legacyIndexes[table] {
			idx, ok := indexes[table][name]
			if !ok || !idx.valid || !idx.ready || !strings.EqualFold(idx.method, "btree") {
				return legacyReject("indexes_mismatch")
			}
		}
	}
	for _, c := range contracts {
		for name, expected := range c.indexDefinitions {
			if !matchesIndexContract(indexes[c.name][name], expected) {
				return legacyReject("index_definition_mismatch")
			}
		}
	}
	for table, specs := range map[string]map[string]schemaIndexContract{
		"account_sync_journal": {
			"account_sync_journal_pull_idx": {method: "btree", columns: []string{"account_id", "server_seq"}},
			"account_sync_journal_attachment_idx": {
				method: "btree", columns: []string{"account_id", "attachment_id", "server_seq"},
				predicate: "(attachment_id IS NOT NULL)",
			},
		},
		"account_activity_facts": {
			"account_activity_facts_pull_idx": {method: "btree", columns: []string{"account_id", "server_seq"}},
			"account_activity_facts_attachment_idx": {
				method: "btree", columns: []string{"account_id", "attachment_id", "server_seq"},
				predicate: "(attachment_id IS NOT NULL)",
			},
		},
	} {
		for name, expected := range specs {
			if !matchesIndexContract(indexes[table][name], expected) {
				return legacyReject("index_definition_mismatch")
			}
		}
	}
	if err := verifyBridgeSequence(ctx, q, "account_change_seq"); err != nil {
		return err
	}
	return nil
}

func verifyPublicRelations(ctx context.Context, q migrationQueryer, kind rune, expected []string) error {
	rows, err := q.Query(ctx, `
        SELECT rel.relname
        FROM pg_class rel JOIN pg_namespace ns ON ns.oid = rel.relnamespace
        WHERE ns.nspname = 'public' AND rel.relkind = $1::"char"
        ORDER BY rel.relname
    `, string(kind))
	if err != nil {
		return legacyReject("relations_unavailable")
	}
	defer rows.Close()
	var names []string
	for rows.Next() {
		var name string
		if err := rows.Scan(&name); err != nil {
			return legacyReject("relations_unavailable")
		}
		names = append(names, name)
	}
	if err := rows.Err(); err != nil {
		return legacyReject("relations_unavailable")
	}
	sorted := append([]string(nil), expected...)
	sort.Strings(sorted)
	if !equalStrings(names, sorted) {
		return legacyReject("relations_mismatch")
	}
	return nil
}

// No last_value/setval/reseed is performed. A rename changes only the
// sequence's identifier; the owned journal default and the activity default
// continue to use the same sequence OID, preserving existing cursors.
func verifyBridgeSequence(ctx context.Context, q migrationQueryer, seq string) error {
	var owned, journalDefault, factsDefault string
	err := q.QueryRow(ctx, `
        SELECT COALESCE(pg_get_serial_sequence('public.account_sync_journal', 'server_seq'), ''),
               COALESCE((SELECT pg_get_expr(d.adbin,d.adrelid)
                 FROM pg_attrdef d JOIN pg_attribute a
                   ON a.attrelid=d.adrelid AND a.attnum=d.adnum
                 WHERE d.adrelid='public.account_sync_journal'::regclass
                   AND a.attname='server_seq'), ''),
               COALESCE((SELECT pg_get_expr(d.adbin,d.adrelid)
                 FROM pg_attrdef d JOIN pg_attribute a
                   ON a.attrelid=d.adrelid AND a.attnum=d.adnum
                 WHERE d.adrelid='public.account_activity_facts'::regclass
                   AND a.attname='server_seq'), '')
    `).Scan(&owned, &journalDefault, &factsDefault)
	if err != nil {
		return legacyReject("sequence_unavailable")
	}
	want := "nextval('" + seq + "'::regclass)"
	if owned != "public."+seq ||
		normalizeDefaultExpression(journalDefault) != normalizeDefaultExpression(want) ||
		normalizeDefaultExpression(factsDefault) != normalizeDefaultExpression(want) {
		return legacyReject("sequence_mismatch")
	}
	return nil
}

func legacyReject(reason string) error {
	return fmt.Errorf("legacy bridge rejected: %s", reason)
}

func verifyCompletedBridgeShape(ctx context.Context, q migrationQueryer) error {
	contracts := schemaContract()
	names := []string{"schema_migrations", "account_legacy_migration_receipts"}
	for _, contract := range contracts {
		names = append(names, contract.name)
	}
	if err := verifyPublicRelations(ctx, q, 'r', names); err != nil {
		return err
	}
	if err := verifyPublicRelations(ctx, q, 'S', []string{
		"account_sync_journal_server_seq_seq",
		"account_security_events_id_seq", "account_sync_versions_id_seq",
		"auth_rate_events_id_seq", "avatar_cleanup_queue_id_seq",
	}); err != nil {
		return err
	}

	// The existing runtime contract intentionally does not require 0010's
	// optional deleted-at columns. This bridge requires their exact presence.
	for i := range contracts {
		switch contracts[i].name {
		case "account_sync_journal", "account_sync_current",
			"account_sync_versions", "account_device_attachment_manifest":
			contracts[i].columns["deleted_at_ms"] = schemaColumnContract{
				dataType: "bigint", notNull: false,
			}
		}
	}
	tables := make([]string, 0, len(contracts)+2)
	for _, c := range contracts {
		tables = append(tables, c.name)
	}
	tables = append(tables, "schema_migrations", "account_legacy_migration_receipts")
	columns, err := catalogColumnsBatch(ctx, q, tables)
	if err != nil {
		return legacyReject("target_columns_unavailable")
	}
	for _, c := range contracts {
		actual := columns[c.name]
		if len(actual) != len(c.columns) {
			return legacyReject("target_columns_mismatch")
		}
		for name, expected := range c.columns {
			got, ok := actual[name]
			if !ok || got.dataType != expected.dataType || got.notNull != expected.notNull {
				return legacyReject("target_columns_mismatch")
			}
		}
	}
	receipts := columns["account_legacy_migration_receipts"]
	if len(receipts) != 2 || receipts["name"].dataType != "text" ||
		!receipts["name"].notNull ||
		receipts["applied_at"].dataType != "timestamp with time zone" ||
		!receipts["applied_at"].notNull {
		return legacyReject("target_receipt_archive_shape")
	}
	checksum := columns["schema_migrations"]
	if len(checksum) != 3 || checksum["name"].dataType != "text" ||
		checksum["checksum"].dataType != "text" ||
		checksum["applied_at"].dataType != "timestamp with time zone" {
		return legacyReject("target_active_receipt_shape")
	}
	return verifyBridgeSequence(ctx, q, "account_sync_journal_server_seq_seq")
}
