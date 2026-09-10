package database

import (
	"context"
	"strings"
)

// The migration receipt proves which migration names an operator applied.
// This catalog proves the physical schema those receipts are meant to
// describe before a legacy name-only receipt is baselined or a runtime is
// admitted. Keep correctness-critical columns and constraints here when an
// additive migration lands.
type schemaTableContract struct {
	name                       string
	columns                    map[string]schemaColumnContract
	defaults                   map[string]string
	primaryKey                 []string
	uniqueKeys                 [][]string
	indexes                    []string
	uniqueIndex                []string
	indexDefinitions           map[string]schemaIndexContract
	foreignKeys                []schemaForeignKeyContract
	forbiddenForeignKeyColumns [][]string
	checks                     []string
	checkExpressions           map[string]string
}

type schemaColumnContract struct {
	dataType    string
	notNull     bool
	defaultExpr string
}

type schemaForeignKeyContract struct {
	columns    []string
	referenced string
	refColumns []string
	onDelete   string
	validated  bool
}

type schemaIndexContract struct {
	unique    bool
	valid     bool
	ready     bool
	method    string
	columns   []string
	predicate string
}

type schemaCheckContract struct {
	expression string
	validated  bool
}

const (
	checkAvatarChoice           = "((builtin_avatar_id IS NULL) OR (uploaded_avatar_object_key IS NULL))"
	checkState                  = "(state = ANY (ARRAY['pending'::text, 'approved'::text, 'denied'::text, 'consumed'::text]))"
	checkRetryMaterial          = "(((recovery_retry_ciphertext IS NULL) AND (recovery_retry_expires_at IS NULL)) OR ((recovery_retry_ciphertext IS NOT NULL) AND (recovery_retry_expires_at IS NOT NULL)))"
	checkOperation              = "(operation = ANY (ARRAY['put'::text, 'delete'::text]))"
	checkPayload                = "(((operation = 'put'::text) AND (payload_ciphertext IS NOT NULL)) OR ((operation = 'delete'::text) AND (payload_ciphertext IS NULL)))"
	checkPositiveSchemaVersion  = "(schema_version > 0)"
	checkNonNegativeHLC         = "(hlc_counter >= 0)"
	checkNonNegativePhysicalHLC = "(hlc_physical_ms >= 0)"
	checkActivityCategory       = "(category = 'activity_fact'::text)"
	checkActivityRecordKey      = "(record_key ~~ 'activity/%'::text)"
	checkPutOperation           = "(operation = 'put'::text)"
	checkPayloadHashLength      = "(octet_length(canonical_payload_hash) = 32)"
	checkPositiveServerSequence = "(server_seq > 0)"
	checkNonNegativeHighWater   = "(highwater_server_seq >= 0)"
	checkPositiveFormat         = "(format_version > 0)"
	checkExpiryAfterCreation    = "(expires_at > created_at)"
	checkNonNegativeItemIndex   = "(item_index >= 0)"
	checkExportKind             = "(kind = ANY (ARRAY['account_metadata'::text, 'sync_record'::text, 'activity_fact'::text]))"
	checkPayloadPresent         = "(octet_length(payload_ciphertext) > 0)"
	checkCapabilityHashLength   = "(octet_length(capability_hash) = 32)"
	checkCompletedAfterCreation = "(completed_at >= created_at)"
	checkMaterializedHLC        = "(((materialized_hlc_physical_ms IS NULL) AND (materialized_hlc_counter IS NULL) AND (materialized_device_id IS NULL)) OR ((materialized_hlc_physical_ms IS NOT NULL) AND (materialized_hlc_counter IS NOT NULL) AND (materialized_device_id IS NOT NULL) AND (materialized_hlc_physical_ms >= 0) AND (materialized_hlc_counter >= 0)))"
)

func schemaContract() []schemaTableContract {
	return []schemaTableContract{
		{
			name: "service_metadata",
			columns: schemaColumns(
				"key", "text", true,
				"value", "text", true,
				"updated_at", "timestamp with time zone", true),
			primaryKey: []string{"key"},
		},
		{
			name: "username_reservations",
			columns: schemaColumns(
				"canonical_username", "text", true,
				"reserved_account_id", "uuid", false,
				"reserved_at", "timestamp with time zone", true),
			primaryKey: []string{"canonical_username"},
		},
		{
			name: "accounts",
			columns: schemaColumns(
				"id", "uuid", true,
				"canonical_username", "text", true,
				"display_username", "text", true,
				"password_hash", "text", true,
				"recovery_key_verifier", "bytea", true,
				"recovery_key_version", "integer", true,
				"protect_new_device_signins", "boolean", true,
				"builtin_avatar_id", "text", false,
				"uploaded_avatar_object_key", "text", false,
				"username_changed_at", "timestamp with time zone", false,
				"created_at", "timestamp with time zone", true,
				"updated_at", "timestamp with time zone", true),
			primaryKey: []string{"id"},
			uniqueKeys: [][]string{{"canonical_username"}},
			foreignKeys: []schemaForeignKeyContract{{
				columns:    []string{"canonical_username"},
				referenced: "username_reservations",
				refColumns: []string{"canonical_username"},
			}},
			checks: []string{"accounts_avatar_choice_ck"},
			checkExpressions: map[string]string{
				"accounts_avatar_choice_ck": checkAvatarChoice,
			},
		},
		{
			name: "devices",
			columns: schemaColumns(
				"id", "uuid", true,
				"account_id", "uuid", true,
				"install_id", "uuid", true,
				"label", "text", true,
				"platform", "text", true,
				"trusted", "boolean", true,
				"revoked_at", "timestamp with time zone", false,
				"created_at", "timestamp with time zone", true,
				"last_seen_at", "timestamp with time zone", true),
			primaryKey: []string{"id"},
			uniqueKeys: [][]string{{"account_id", "install_id"}},
			foreignKeys: []schemaForeignKeyContract{{
				columns: []string{"account_id"}, referenced: "accounts", refColumns: []string{"id"},
			}},
		},
		{
			name: "sessions",
			columns: schemaColumns(
				"id", "uuid", true,
				"account_id", "uuid", true,
				"device_id", "uuid", true,
				"access_token_hash", "bytea", true,
				"access_expires_at", "timestamp with time zone", true,
				"refresh_token_hash", "bytea", true,
				"previous_refresh_token_hash", "bytea", false,
				"previous_refresh_expires_at", "timestamp with time zone", false,
				"refresh_retry_ciphertext", "bytea", false,
				"revoked_at", "timestamp with time zone", false,
				"created_at", "timestamp with time zone", true,
				"last_refreshed_at", "timestamp with time zone", true),
			primaryKey:  []string{"id"},
			uniqueKeys:  [][]string{{"access_token_hash"}, {"refresh_token_hash"}},
			uniqueIndex: []string{"sessions_one_active_per_device_idx"},
			indexDefinitions: map[string]schemaIndexContract{
				"sessions_one_active_per_device_idx": {
					unique: true, method: "btree", columns: []string{"device_id"},
					predicate: "(revoked_at IS NULL)",
				},
			},
			foreignKeys: []schemaForeignKeyContract{
				{columns: []string{"account_id"}, referenced: "accounts", refColumns: []string{"id"}},
				{columns: []string{"device_id"}, referenced: "devices", refColumns: []string{"id"}},
			},
		},
		{
			name: "device_signin_challenges",
			columns: schemaColumns(
				"id", "uuid", true,
				"account_id", "uuid", true,
				"target_install_id", "uuid", true,
				"target_label", "text", true,
				"target_platform", "text", true,
				"challenge_token_hash", "bytea", true,
				"state", "text", true,
				"expires_at", "timestamp with time zone", true,
				"created_at", "timestamp with time zone", true,
				"decided_at", "timestamp with time zone", false,
				"decided_by_device_id", "uuid", false,
				"consumed_at", "timestamp with time zone", false),
			primaryKey: []string{"id"},
			uniqueKeys: [][]string{{"challenge_token_hash"}},
			foreignKeys: []schemaForeignKeyContract{
				{columns: []string{"account_id"}, referenced: "accounts", refColumns: []string{"id"}},
				{columns: []string{"decided_by_device_id"}, referenced: "devices", refColumns: []string{"id"}},
			},
			checks: []string{"device_signin_challenges_state_ck"},
			checkExpressions: map[string]string{
				"device_signin_challenges_state_ck": checkState,
			},
		},
		{
			name: "trusted_recovery_challenges",
			columns: schemaColumns(
				"id", "uuid", true,
				"account_id", "uuid", true,
				"target_install_id", "uuid", true,
				"target_label", "text", true,
				"target_platform", "text", true,
				"challenge_token_hash", "bytea", true,
				"new_password_hash", "text", true,
				"state", "text", true,
				"expires_at", "timestamp with time zone", true,
				"created_at", "timestamp with time zone", true,
				"decided_at", "timestamp with time zone", false,
				"decided_by_device_id", "uuid", false,
				"consumed_at", "timestamp with time zone", false,
				"recovery_retry_ciphertext", "bytea", false,
				"recovery_retry_expires_at", "timestamp with time zone", false),
			primaryKey: []string{"id"},
			uniqueKeys: [][]string{{"challenge_token_hash"}},
			foreignKeys: []schemaForeignKeyContract{
				{columns: []string{"account_id"}, referenced: "accounts", refColumns: []string{"id"}},
				{columns: []string{"decided_by_device_id"}, referenced: "devices", refColumns: []string{"id"}},
			},
			checks: []string{
				"trusted_recovery_challenges_state_ck",
				"trusted_recovery_retry_material_ck",
			},
			checkExpressions: map[string]string{
				"trusted_recovery_challenges_state_ck": checkState,
				"trusted_recovery_retry_material_ck":   checkRetryMaterial,
			},
		},
		{
			name: "auth_rate_events",
			columns: schemaColumns(
				"id", "bigint", true,
				"event_type", "text", true,
				"key_hash", "bytea", true,
				"occurred_at", "timestamp with time zone", true),
			primaryKey: []string{"id"},
		},
		{
			name: "account_security_events",
			columns: schemaColumns(
				"id", "bigint", true,
				"account_id", "uuid", false,
				"event_type", "text", true,
				"device_id", "uuid", false,
				"occurred_at", "timestamp with time zone", true,
				"metadata", "jsonb", true),
			primaryKey: []string{"id"},
			foreignKeys: []schemaForeignKeyContract{{
				columns: []string{"account_id"}, referenced: "accounts", refColumns: []string{"id"},
			}},
		},
		{
			name: "avatar_cleanup_queue",
			columns: schemaColumns(
				"id", "bigint", true,
				"object_key", "text", true,
				"enqueued_at", "timestamp with time zone", true,
				"attempts", "integer", true,
				"last_error", "text", false,
				"next_attempt_at", "timestamp with time zone", true),
			primaryKey: []string{"id"},
			uniqueKeys: [][]string{{"object_key"}},
		},
		{
			name: "account_sync_journal",
			columns: schemaColumns(
				"server_seq", "bigint", true,
				"account_id", "uuid", true,
				"mutation_id", "uuid", true,
				"device_id", "uuid", true,
				"category", "text", true,
				"record_key", "text", true,
				"schema_version", "integer", true,
				"hlc_physical_ms", "bigint", true,
				"hlc_counter", "bigint", true,
				"operation", "text", true,
				"payload_ciphertext", "bytea", false,
				"materialized_payload_ciphertext", "bytea", false,
				"materialized_hlc_physical_ms", "bigint", false,
				"materialized_hlc_counter", "bigint", false,
				"materialized_device_id", "uuid", false,
				"won", "boolean", true,
				"received_at", "timestamp with time zone", true),
			primaryKey: []string{"server_seq"},
			uniqueKeys: [][]string{{"account_id", "mutation_id"}},
			foreignKeys: []schemaForeignKeyContract{{
				columns: []string{"account_id"}, referenced: "accounts", refColumns: []string{"id"},
			}},
			checks: []string{
				"account_sync_journal_operation_ck",
				"account_sync_journal_payload_ck",
				"account_sync_journal_schema_ck",
				"account_sync_journal_counter_ck",
				"account_sync_journal_materialized_hlc_ck",
			},
			checkExpressions: map[string]string{
				"account_sync_journal_operation_ck":        checkOperation,
				"account_sync_journal_payload_ck":          checkPayload,
				"account_sync_journal_schema_ck":           checkPositiveSchemaVersion,
				"account_sync_journal_counter_ck":          checkNonNegativeHLC,
				"account_sync_journal_materialized_hlc_ck": checkMaterializedHLC,
			},
		},
		{
			name: "account_sync_current",
			columns: schemaColumns(
				"account_id", "uuid", true,
				"category", "text", true,
				"record_key", "text", true,
				"mutation_id", "uuid", true,
				"device_id", "uuid", true,
				"schema_version", "integer", true,
				"hlc_physical_ms", "bigint", true,
				"hlc_counter", "bigint", true,
				"operation", "text", true,
				"payload_ciphertext", "bytea", false,
				"server_seq", "bigint", true,
				"updated_at", "timestamp with time zone", true),
			primaryKey: []string{"account_id", "category", "record_key"},
			foreignKeys: []schemaForeignKeyContract{{
				columns: []string{"account_id"}, referenced: "accounts", refColumns: []string{"id"},
			}},
			checks: []string{
				"account_sync_current_operation_ck",
				"account_sync_current_payload_ck",
			},
			checkExpressions: map[string]string{
				"account_sync_current_operation_ck": checkOperation,
				"account_sync_current_payload_ck":   checkPayload,
			},
		},
		{
			name: "account_sync_versions",
			columns: schemaColumns(
				"id", "bigint", true,
				"account_id", "uuid", true,
				"category", "text", true,
				"record_key", "text", true,
				"mutation_id", "uuid", true,
				"device_id", "uuid", true,
				"schema_version", "integer", true,
				"hlc_physical_ms", "bigint", true,
				"hlc_counter", "bigint", true,
				"operation", "text", true,
				"payload_ciphertext", "bytea", false,
				"server_seq", "bigint", true,
				"replaced_at", "timestamp with time zone", true,
				"replacing_mutation_id", "uuid", true),
			primaryKey: []string{"id"},
			foreignKeys: []schemaForeignKeyContract{{
				columns: []string{"account_id"}, referenced: "accounts", refColumns: []string{"id"},
			}},
		},
		{
			name: "account_activity_facts",
			columns: schemaColumns(
				"account_id", "uuid", true,
				"event_id", "uuid", true,
				"mutation_id", "uuid", true,
				"origin_device_id", "uuid", true,
				"schema_version", "integer", true,
				"event_type", "text", true,
				"payload_ciphertext", "bytea", true,
				"hlc_physical_ms", "bigint", true,
				"hlc_counter", "bigint", true,
				"server_seq", "bigint", true,
				"suppressed", "boolean", true,
				"received_at", "timestamp with time zone", true),
			primaryKey: []string{"account_id", "event_id"},
			uniqueKeys: [][]string{{"account_id", "mutation_id"}, {"server_seq"}},
			foreignKeys: []schemaForeignKeyContract{
				{columns: []string{"account_id"}, referenced: "accounts", refColumns: []string{"id"}},
				{columns: []string{"origin_device_id"}, referenced: "devices", refColumns: []string{"id"}},
			},
			checks: []string{
				"account_activity_facts_schema_ck",
				"account_activity_facts_hlc_physical_ck",
				"account_activity_facts_hlc_counter_ck",
			},
			checkExpressions: map[string]string{
				"account_activity_facts_schema_ck":       checkPositiveSchemaVersion,
				"account_activity_facts_hlc_physical_ck": checkNonNegativePhysicalHLC,
				"account_activity_facts_hlc_counter_ck":  checkNonNegativeHLC,
			},
		},
		{
			name: "account_activity_reset_state",
			columns: schemaColumns(
				"account_id", "uuid", true,
				"reset_generation", "bigint", true,
				"reset_at_ms", "bigint", true,
				"hlc_physical_ms", "bigint", true,
				"hlc_counter", "bigint", true,
				"device_id", "uuid", true),
			primaryKey: []string{"account_id"},
			foreignKeys: []schemaForeignKeyContract{
				{columns: []string{"account_id"}, referenced: "accounts", refColumns: []string{"id"}},
			},
			checks: []string{
				"account_activity_reset_generation_ck",
				"account_activity_reset_at_ck",
				"account_activity_reset_hlc_physical_ck",
				"account_activity_reset_hlc_counter_ck",
			},
			checkExpressions: map[string]string{
				"account_activity_reset_generation_ck":   "reset_generation > 0",
				"account_activity_reset_at_ck":           "reset_at_ms > 0",
				"account_activity_reset_hlc_physical_ck": "hlc_physical_ms >= 0",
				"account_activity_reset_hlc_counter_ck":  "hlc_counter >= 0",
			},
		},
		{
			name: "account_history_reset_state",
			columns: schemaColumns(
				"account_id", "uuid", true,
				"reset_generation", "bigint", true,
				"reset_at_ms", "bigint", true,
				"hlc_physical_ms", "bigint", true,
				"hlc_counter", "bigint", true,
				"device_id", "uuid", true),
			primaryKey: []string{"account_id"},
			foreignKeys: []schemaForeignKeyContract{
				{columns: []string{"account_id"}, referenced: "accounts", refColumns: []string{"id"}},
			},
			checks: []string{
				"account_history_reset_generation_ck",
				"account_history_reset_at_ck",
				"account_history_reset_hlc_physical_ck",
				"account_history_reset_hlc_counter_ck",
			},
			checkExpressions: map[string]string{
				"account_history_reset_generation_ck":   "reset_generation > 0",
				"account_history_reset_at_ck":           "reset_at_ms > 0",
				"account_history_reset_hlc_physical_ck": "hlc_physical_ms >= 0",
				"account_history_reset_hlc_counter_ck":  "hlc_counter >= 0",
			},
		},
		{
			name: "account_sync_mutation_aliases",
			columns: schemaColumns(
				"account_id", "uuid", true,
				"mutation_id", "uuid", true,
				"category", "text", true,
				"record_key", "text", true,
				"device_id", "uuid", true,
				"schema_version", "integer", true,
				"hlc_physical_ms", "bigint", true,
				"hlc_counter", "bigint", true,
				"operation", "text", true,
				"canonical_payload_hash", "bytea", true,
				"server_seq", "bigint", true,
				"won", "boolean", true,
				"activity_event_id", "uuid", true,
				"created_at", "timestamp with time zone", true),
			primaryKey: []string{"account_id", "mutation_id"},
			foreignKeys: []schemaForeignKeyContract{{
				columns: []string{"account_id"}, referenced: "accounts", refColumns: []string{"id"},
			}},
			checks: []string{
				"account_sync_mutation_aliases_category_ck",
				"account_sync_mutation_aliases_record_key_ck",
				"account_sync_mutation_aliases_schema_ck",
				"account_sync_mutation_aliases_hlc_physical_ck",
				"account_sync_mutation_aliases_hlc_counter_ck",
				"account_sync_mutation_aliases_operation_ck",
				"account_sync_mutation_aliases_payload_hash_ck",
				"account_sync_mutation_aliases_server_seq_ck",
			},
			checkExpressions: map[string]string{
				"account_sync_mutation_aliases_category_ck":     checkActivityCategory,
				"account_sync_mutation_aliases_record_key_ck":   checkActivityRecordKey,
				"account_sync_mutation_aliases_schema_ck":       checkPositiveSchemaVersion,
				"account_sync_mutation_aliases_hlc_physical_ck": checkNonNegativePhysicalHLC,
				"account_sync_mutation_aliases_hlc_counter_ck":  checkNonNegativeHLC,
				"account_sync_mutation_aliases_operation_ck":    checkPutOperation,
				"account_sync_mutation_aliases_payload_hash_ck": checkPayloadHashLength,
				"account_sync_mutation_aliases_server_seq_ck":   checkPositiveServerSequence,
			},
		},
		{
			name: "account_export_snapshots",
			columns: schemaColumns(
				"id", "uuid", true,
				"account_id", "uuid", true,
				"highwater_server_seq", "bigint", true,
				"format_version", "integer", true,
				"created_at", "timestamp with time zone", true,
				"expires_at", "timestamp with time zone", true),
			defaults: map[string]string{
				"id": "gen_random_uuid()",
			},
			primaryKey: []string{"id"},
			indexes:    []string{"account_export_snapshots_account_expiry_idx"},
			indexDefinitions: map[string]schemaIndexContract{
				"account_export_snapshots_account_expiry_idx": {
					method: "btree", columns: []string{"account_id", "expires_at"},
				},
			},
			foreignKeys: []schemaForeignKeyContract{{
				columns: []string{"account_id"}, referenced: "accounts", refColumns: []string{"id"},
			}},
			checks: []string{
				"account_export_snapshots_highwater_ck",
				"account_export_snapshots_format_ck",
				"account_export_snapshots_expiry_ck",
			},
			checkExpressions: map[string]string{
				"account_export_snapshots_highwater_ck": checkNonNegativeHighWater,
				"account_export_snapshots_format_ck":    checkPositiveFormat,
				"account_export_snapshots_expiry_ck":    checkExpiryAfterCreation,
			},
		},
		{
			name: "account_export_items",
			columns: schemaColumns(
				"snapshot_id", "uuid", true,
				"item_index", "bigint", true,
				"kind", "text", true,
				"category", "text", true,
				"record_key", "text", true,
				"payload_ciphertext", "bytea", true),
			primaryKey: []string{"snapshot_id", "item_index"},
			uniqueKeys: [][]string{{"snapshot_id", "kind", "category", "record_key"}},
			foreignKeys: []schemaForeignKeyContract{{
				columns: []string{"snapshot_id"}, referenced: "account_export_snapshots", refColumns: []string{"id"},
			}},
			checks: []string{
				"account_export_items_index_ck",
				"account_export_items_kind_ck",
				"account_export_items_payload_ck",
			},
			checkExpressions: map[string]string{
				"account_export_items_index_ck":   checkNonNegativeItemIndex,
				"account_export_items_kind_ck":    checkExportKind,
				"account_export_items_payload_ck": checkPayloadPresent,
			},
		},
		{
			name: "account_deletion_receipts",
			columns: schemaColumns(
				"request_id", "uuid", true,
				"capability_hash", "bytea", true,
				"account_id", "uuid", true,
				"created_at", "timestamp with time zone", true,
				"expires_at", "timestamp with time zone", true,
				"completed_at", "timestamp with time zone", true),
			primaryKey: []string{"request_id"},
			uniqueKeys: [][]string{{"capability_hash"}},
			indexes:    []string{"account_deletion_receipts_expiry_idx"},
			indexDefinitions: map[string]schemaIndexContract{
				"account_deletion_receipts_expiry_idx": {
					method: "btree", columns: []string{"expires_at"},
				},
			},
			// This account identifier intentionally has no FK. The receipt must
			// survive the account cascade long enough to validate a retry.
			forbiddenForeignKeyColumns: [][]string{{"account_id"}},
			checks: []string{
				"account_deletion_receipts_capability_ck",
				"account_deletion_receipts_expiry_ck",
				"account_deletion_receipts_completed_ck",
			},
			checkExpressions: map[string]string{
				"account_deletion_receipts_capability_ck": checkCapabilityHashLength,
				"account_deletion_receipts_expiry_ck":     checkExpiryAfterCreation,
				"account_deletion_receipts_completed_ck":  checkCompletedAfterCreation,
			},
		},
	}
}

func schemaColumns(values ...any) map[string]schemaColumnContract {
	if len(values)%3 != 0 {
		panic("schema column catalog requires name, type, not-null triples")
	}
	columns := make(map[string]schemaColumnContract, len(values)/3)
	for index := 0; index < len(values); index += 3 {
		name, ok := values[index].(string)
		if !ok {
			panic("schema column catalog name must be a string")
		}
		dataType, ok := values[index+1].(string)
		if !ok {
			panic("schema column catalog type must be a string")
		}
		notNull, ok := values[index+2].(bool)
		if !ok {
			panic("schema column catalog nullability must be a bool")
		}
		columns[name] = schemaColumnContract{dataType: dataType, notNull: notNull}
	}
	return columns
}

func validateSchemaContract(ctx context.Context, queryer migrationQueryer) error {
	// Keep readiness on a small fixed number of catalog round trips. The
	// returned rows are validated in memory so a large remote schema does not
	// turn one readiness check into hundreds of network calls.
	contracts := schemaContract()
	tables := make([]string, 0, len(contracts))
	for _, contract := range contracts {
		tables = append(tables, contract.name)
	}

	columns, err := catalogColumnsBatch(ctx, queryer, tables)
	if err != nil {
		return schemaCompatibilityError("schema_column_contract_failed")
	}
	keys, err := catalogKeysBatch(ctx, queryer, tables)
	if err != nil {
		return schemaCompatibilityError("schema_key_contract_failed")
	}
	foreignKeys, err := catalogForeignKeysBatch(ctx, queryer, tables)
	if err != nil {
		return schemaCompatibilityError("schema_foreign_key_contract_failed")
	}
	checks, err := catalogChecksBatch(ctx, queryer, tables)
	if err != nil {
		return schemaCompatibilityError("schema_check_contract_failed")
	}
	indexes, err := catalogIndexesBatch(ctx, queryer, tables)
	if err != nil {
		return schemaCompatibilityError("schema_unique_index_contract_failed")
	}

	for _, table := range contracts {
		actualColumns, ok := columns[table.name]
		if !ok {
			return schemaCompatibilityError("schema_column_contract_failed")
		}
		for column, expected := range table.columns {
			actual, ok := actualColumns[column]
			if !ok || actual.dataType != expected.dataType || actual.notNull != expected.notNull {
				return schemaCompatibilityError("schema_column_contract_failed")
			}
		}
		for column, expected := range table.defaults {
			actual, ok := actualColumns[column]
			if !ok || normalizeDefaultExpression(actual.defaultExpr) != normalizeDefaultExpression(expected) {
				return schemaCompatibilityError("schema_column_contract_failed")
			}
		}

		actualKeys := keys[table.name]
		if !containsKey(actualKeys["p"], table.primaryKey) {
			return schemaCompatibilityError("schema_primary_key_contract_failed")
		}
		for _, expected := range table.uniqueKeys {
			if !containsKey(actualKeys["u"], expected) {
				return schemaCompatibilityError("schema_unique_contract_failed")
			}
		}
		requiredIndexes := append(append([]string{}, table.indexes...), table.uniqueIndex...)
		for _, expectedName := range requiredIndexes {
			index, ok := indexes[table.name][expectedName]
			expected, expectedOK := table.indexDefinitions[expectedName]
			if !ok || !expectedOK || !index.valid || !index.ready ||
				!matchesIndexContract(index, expected) {
				return schemaCompatibilityError("schema_unique_index_contract_failed")
			}
		}

		for _, expected := range table.foreignKeys {
			if !containsForeignKey(foreignKeys[table.name], expected) {
				return schemaCompatibilityError("schema_foreign_key_contract_failed")
			}
		}
		for _, forbidden := range table.forbiddenForeignKeyColumns {
			if containsForeignKeyColumns(foreignKeys[table.name], forbidden) {
				return schemaCompatibilityError("schema_foreign_key_contract_failed")
			}
		}
		for _, expectedName := range table.checks {
			actual, ok := checks[table.name][expectedName]
			expected, expectedOK := table.checkExpressions[expectedName]
			if !ok || !expectedOK || !actual.validated ||
				normalizeSQLDefinition(actual.expression) != normalizeSQLDefinition(expected) {
				return schemaCompatibilityError("schema_check_contract_failed")
			}
		}
	}
	return nil
}

func catalogColumnsBatch(
	ctx context.Context,
	queryer migrationQueryer,
	tables []string,
) (map[string]map[string]schemaColumnContract, error) {
	rows, err := queryer.Query(ctx, `
		SELECT column_info.table_name,
		       column_info.column_name,
		       column_info.data_type,
		       column_info.is_nullable = 'NO',
		       COALESCE(pg_get_expr(column_default.adbin, column_default.adrelid), '')
		FROM information_schema.columns AS column_info
		JOIN pg_class AS relation
		  ON relation.relname = column_info.table_name
		 AND relation.relnamespace = 'public'::regnamespace
		JOIN pg_attribute AS attribute
		  ON attribute.attrelid = relation.oid
		 AND attribute.attname = column_info.column_name
		LEFT JOIN pg_attrdef AS column_default
		  ON column_default.adrelid = attribute.attrelid
		 AND column_default.adnum = attribute.attnum
		WHERE column_info.table_schema = 'public'
		  AND column_info.table_name = ANY($1::text[])
    `, tables)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	result := make(map[string]map[string]schemaColumnContract, len(tables))
	for rows.Next() {
		var table, column, dataType, defaultExpr string
		var notNull bool
		if err := rows.Scan(&table, &column, &dataType, &notNull, &defaultExpr); err != nil {
			return nil, err
		}
		if result[table] == nil {
			result[table] = make(map[string]schemaColumnContract)
		}
		result[table][column] = schemaColumnContract{
			dataType:    dataType,
			notNull:     notNull,
			defaultExpr: defaultExpr,
		}
	}
	return result, rows.Err()
}

func catalogKeysBatch(
	ctx context.Context,
	queryer migrationQueryer,
	tables []string,
) (map[string]map[string][][]string, error) {
	rows, err := queryer.Query(ctx, `
        SELECT relation.relname, constraint_row.contype,
               array_agg(attribute.attname ORDER BY key.ord)
        FROM pg_constraint constraint_row
        JOIN pg_class relation
          ON relation.oid = constraint_row.conrelid
        JOIN LATERAL unnest(constraint_row.conkey) WITH ORDINALITY AS key(attnum, ord)
          ON true
        JOIN pg_attribute attribute
          ON attribute.attrelid = relation.oid
         AND attribute.attnum = key.attnum
        WHERE relation.relnamespace = 'public'::regnamespace
          AND relation.relname = ANY($1::text[])
          AND constraint_row.contype IN ('p', 'u')
        GROUP BY constraint_row.oid, relation.relname, constraint_row.contype
    `, tables)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	result := make(map[string]map[string][][]string, len(tables))
	for rows.Next() {
		var table, kind string
		var columns []string
		if err := rows.Scan(&table, &kind, &columns); err != nil {
			return nil, err
		}
		if result[table] == nil {
			result[table] = make(map[string][][]string)
		}
		result[table][kind] = append(result[table][kind], columns)
	}
	return result, rows.Err()
}

func catalogForeignKeysBatch(
	ctx context.Context,
	queryer migrationQueryer,
	tables []string,
) (map[string][]schemaForeignKeyContract, error) {
	rows, err := queryer.Query(ctx, `
        SELECT
            source_relation.relname,
            array_agg(source_attribute.attname ORDER BY source_key.ord),
            referenced_relation.relname,
            array_agg(referenced_attribute.attname ORDER BY source_key.ord),
            constraint_row.confdeltype::text,
            constraint_row.convalidated
        FROM pg_constraint constraint_row
        JOIN pg_class source_relation
          ON source_relation.oid = constraint_row.conrelid
        JOIN pg_class referenced_relation
          ON referenced_relation.oid = constraint_row.confrelid
        JOIN LATERAL unnest(constraint_row.conkey) WITH ORDINALITY AS source_key(attnum, ord)
          ON true
        JOIN pg_attribute source_attribute
          ON source_attribute.attrelid = source_relation.oid
         AND source_attribute.attnum = source_key.attnum
        JOIN LATERAL unnest(constraint_row.confkey) WITH ORDINALITY AS referenced_key(attnum, ord)
          ON referenced_key.ord = source_key.ord
        JOIN pg_attribute referenced_attribute
          ON referenced_attribute.attrelid = referenced_relation.oid
         AND referenced_attribute.attnum = referenced_key.attnum
        WHERE source_relation.relnamespace = 'public'::regnamespace
          AND referenced_relation.relnamespace = 'public'::regnamespace
          AND source_relation.relname = ANY($1::text[])
          AND constraint_row.contype = 'f'
        GROUP BY constraint_row.oid, source_relation.relname,
                 referenced_relation.relname, constraint_row.confdeltype,
                 constraint_row.convalidated
    `, tables)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	result := make(map[string][]schemaForeignKeyContract, len(tables))
	for rows.Next() {
		var table, referenced, onDelete string
		var columns, refColumns []string
		var validated bool
		if err := rows.Scan(&table, &columns, &referenced, &refColumns, &onDelete, &validated); err != nil {
			return nil, err
		}
		result[table] = append(result[table], schemaForeignKeyContract{
			columns: columns, referenced: referenced, refColumns: refColumns,
			onDelete: onDelete, validated: validated,
		})
	}
	return result, rows.Err()
}

func catalogChecksBatch(
	ctx context.Context,
	queryer migrationQueryer,
	tables []string,
) (map[string]map[string]schemaCheckContract, error) {
	rows, err := queryer.Query(ctx, `
		SELECT relation.relname,
		       constraint_row.conname,
		       pg_get_expr(constraint_row.conbin, constraint_row.conrelid),
		       constraint_row.convalidated
		FROM pg_constraint constraint_row
		JOIN pg_class relation
		  ON relation.oid = constraint_row.conrelid
		WHERE relation.relnamespace = 'public'::regnamespace
		  AND relation.relname = ANY($1::text[])
		  AND constraint_row.contype = 'c'
	`, tables)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	result := make(map[string]map[string]schemaCheckContract, len(tables))
	for rows.Next() {
		var table, name, expression string
		var validated bool
		if err := rows.Scan(&table, &name, &expression, &validated); err != nil {
			return nil, err
		}
		if result[table] == nil {
			result[table] = make(map[string]schemaCheckContract)
		}
		result[table][name] = schemaCheckContract{
			expression: expression,
			validated:  validated,
		}
	}
	return result, rows.Err()
}

func catalogIndexesBatch(
	ctx context.Context,
	queryer migrationQueryer,
	tables []string,
) (map[string]map[string]schemaIndexContract, error) {
	rows, err := queryer.Query(ctx, `
		SELECT table_relation.relname,
		       index_relation.relname,
		       index_row.indisunique,
		       index_row.indisvalid,
		       index_row.indisready,
		       access_method.amname,
		       array_agg(COALESCE(attribute.attname, '') ORDER BY key.ord),
		       COALESCE(pg_get_expr(index_row.indpred, index_row.indrelid), '')
		FROM pg_index index_row
        JOIN pg_class table_relation
          ON table_relation.oid = index_row.indrelid
        JOIN pg_class index_relation
          ON index_relation.oid = index_row.indexrelid
		JOIN pg_namespace table_namespace
		  ON table_namespace.oid = table_relation.relnamespace
		JOIN pg_am access_method
		  ON access_method.oid = index_relation.relam
		JOIN LATERAL generate_subscripts(index_row.indkey, 1) AS key(ord)
		  ON key.ord <= index_row.indnkeyatts
		LEFT JOIN pg_attribute attribute
		  ON attribute.attrelid = table_relation.oid
		 AND attribute.attnum = index_row.indkey[key.ord]
		WHERE table_namespace.nspname = 'public'
		  AND table_relation.relname = ANY($1::text[])
		GROUP BY table_relation.relname,
		         index_relation.relname,
		         index_row.indisunique,
		         index_row.indisvalid,
		         index_row.indisready,
		         access_method.amname,
		         index_row.indpred,
		         index_row.indrelid
	`, tables)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	result := make(map[string]map[string]schemaIndexContract, len(tables))
	for rows.Next() {
		var table, index, method, predicate string
		var columns []string
		var contract schemaIndexContract
		if err := rows.Scan(
			&table,
			&index,
			&contract.unique,
			&contract.valid,
			&contract.ready,
			&method,
			&columns,
			&predicate); err != nil {
			return nil, err
		}
		contract.method = method
		contract.columns = columns
		contract.predicate = predicate
		if result[table] == nil {
			result[table] = make(map[string]schemaIndexContract)
		}
		result[table][index] = contract
	}
	return result, rows.Err()
}

func matchesIndexContract(actual, expected schemaIndexContract) bool {
	return actual.unique == expected.unique &&
		strings.EqualFold(actual.method, expected.method) &&
		equalStrings(actual.columns, expected.columns) &&
		normalizeSQLDefinition(actual.predicate) == normalizeSQLDefinition(expected.predicate)
}

func containsKey(keys [][]string, expected []string) bool {
	for _, key := range keys {
		if equalStrings(key, expected) {
			return true
		}
	}
	return false
}

func containsForeignKey(keys []schemaForeignKeyContract, expected schemaForeignKeyContract) bool {
	expectedDeleteAction := expected.onDelete
	if expectedDeleteAction == "" {
		expectedDeleteAction = defaultForeignKeyDeleteAction(expected)
	}
	for _, key := range keys {
		if key.validated && key.referenced == expected.referenced &&
			equalStrings(key.columns, expected.columns) &&
			equalStrings(key.refColumns, expected.refColumns) &&
			key.onDelete == expectedDeleteAction {
			return true
		}
	}
	return false
}

func containsForeignKeyColumns(keys []schemaForeignKeyContract, expected []string) bool {
	for _, key := range keys {
		if equalStrings(key.columns, expected) {
			return true
		}
	}
	return false
}

func defaultForeignKeyDeleteAction(expected schemaForeignKeyContract) string {
	// PostgreSQL's "a" means NO ACTION, the default used by the username
	// reservation relation. All account-owned rows use CASCADE; the two
	// optional approver-device references intentionally use SET NULL.
	if strings.EqualFold(expected.referenced, "devices") &&
		len(expected.columns) == 1 && expected.columns[0] == "decided_by_device_id" {
		return "n"
	}
	if strings.EqualFold(expected.referenced, "username_reservations") {
		return "a"
	}
	return "c"
}

func equalStrings(left, right []string) bool {
	if len(left) != len(right) {
		return false
	}
	for index := range left {
		if left[index] != right[index] {
			return false
		}
	}
	return true
}

func normalizeDefaultExpression(value string) string {
	return normalizeSQLDefinition(value)
}

func normalizeSQLDefinition(value string) string {
	return strings.Join(strings.Fields(strings.ToLower(strings.TrimSpace(value))), "")
}
