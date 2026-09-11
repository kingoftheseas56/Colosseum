package account

import (
	"reflect"
	"strings"
	"testing"
)

func splitTopLevelSQLExpressions(value string) []string {
	var expressions []string
	start := 0
	depth := 0
	for index := 0; index < len(value); index++ {
		switch value[index] {
		case '(':
			depth++
		case ')':
			depth--
		case ',':
			if depth == 0 {
				expressions = append(
					expressions,
					normalizeSQLExpression(value[start:index]))
				start = index + 1
			}
		}
	}
	expressions = append(
		expressions,
		normalizeSQLExpression(value[start:]))
	return expressions
}

func normalizeSQLExpression(value string) string {
	return strings.Join(strings.Fields(value), " ")
}

func sqlBetween(
	t *testing.T,
	query string,
	start string,
	end string,
) string {
	t.Helper()
	startIndex := strings.Index(query, start)
	if startIndex < 0 {
		t.Fatalf("SQL contract start %q is missing", start)
	}
	startIndex += len(start)
	endIndex := strings.Index(query[startIndex:], end)
	if endIndex < 0 {
		t.Fatalf("SQL contract end %q is missing", end)
	}
	return query[startIndex : startIndex+endIndex]
}

func TestSyncJournalSQLColumnParity(t *testing.T) {
	insertColumns := splitTopLevelSQLExpressions(sqlBetween(
		t,
		syncJournalInsertQuery,
		"INSERT INTO account_sync_journal(",
		")\n        VALUES("))
	wantInsertColumns := []string{
		"account_id",
		"mutation_id",
		"device_id",
		"category",
		"record_key",
		"schema_version",
		"hlc_physical_ms",
		"hlc_counter",
		"operation",
		"payload_ciphertext",
		"materialized_payload_ciphertext",
		"materialized_hlc_physical_ms",
		"materialized_hlc_counter",
		"materialized_device_id",
		"attachment_id",
		"won",
		"received_at",
	}
	if !reflect.DeepEqual(insertColumns, wantInsertColumns) {
		t.Fatalf("journal INSERT columns = %#v, want %#v", insertColumns, wantInsertColumns)
	}

	insertValues := splitTopLevelSQLExpressions(sqlBetween(
		t,
		syncJournalInsertQuery,
		"VALUES(",
		")\n        ON CONFLICT"))
	if len(insertValues) != len(insertColumns) {
		t.Fatalf("journal INSERT values = %d, columns = %d", len(insertValues), len(insertColumns))
	}
	for _, column := range insertColumns {
		if strings.Contains(strings.ToUpper(column), "COALESCE") {
			t.Fatalf("journal INSERT column list contains expression %q", column)
		}
	}

	branches := strings.Split(syncJournalPullQuery, "UNION ALL")
	if len(branches) != 2 {
		t.Fatalf("journal Pull branches = %d, want 2", len(branches))
	}
	for index, branch := range branches {
		branch = strings.TrimSpace(branch)
		if !strings.HasPrefix(branch, "SELECT") {
			t.Fatalf("journal Pull branch %d does not start with SELECT", index)
		}
		fromIndex := strings.Index(branch, "\n        FROM ")
		if fromIndex < 0 {
			t.Fatalf("journal Pull branch %d has no FROM", index)
		}
		columns := splitTopLevelSQLExpressions(
			strings.TrimPrefix(branch[:fromIndex], "SELECT"))
		if len(columns) != len(insertColumns) {
			t.Fatalf("journal Pull branch %d columns = %d, want %d: %#v",
				index, len(columns), len(insertColumns), columns)
		}
	}
}
