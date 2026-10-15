package database

import (
	"context"
	"encoding/json"
	"fmt"
	"reflect"
	"sort"
	"testing"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"
)

// Snapshot every application schema, not just the seeded sentinel relations.
// Stable catalog projections exclude physical storage IDs and transaction IDs.
// Full row JSON additionally detects changes that preserve row counts.
type bridgeDatabaseSnapshot struct {
	Catalog   map[string][]string `json:"catalog"`
	Counts    map[string]int64    `json:"counts"`
	Rows      map[string]string   `json:"rows"`
	Sequences map[string]string   `json:"sequences"`
}

func snapshotBridgeDatabase(t *testing.T, ctx context.Context, pool *pgxpool.Pool) bridgeDatabaseSnapshot {
	t.Helper()
	out := bridgeDatabaseSnapshot{map[string][]string{}, map[string]int64{}, map[string]string{}, map[string]string{}}
	queries := map[string]string{
		"schemas":      `SELECT jsonb_build_array(nspname,nspacl)::text FROM pg_namespace WHERE nspname !~ '^pg_' AND nspname <> 'information_schema' ORDER BY 1`,
		"relations":    `SELECT jsonb_build_array(n.nspname,c.relname,c.relkind,c.relpersistence,c.relrowsecurity,c.relforcerowsecurity,c.reloptions,c.relacl)::text FROM pg_class c JOIN pg_namespace n ON n.oid=c.relnamespace WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema' ORDER BY 1`,
		"columns":      `SELECT jsonb_build_array(n.nspname,c.relname,a.attnum,a.attname,format_type(a.atttypid,a.atttypmod),a.attnotnull,a.attidentity,a.attgenerated,pg_get_expr(d.adbin,d.adrelid))::text FROM pg_attribute a JOIN pg_class c ON c.oid=a.attrelid JOIN pg_namespace n ON n.oid=c.relnamespace LEFT JOIN pg_attrdef d ON d.adrelid=a.attrelid AND d.adnum=a.attnum WHERE a.attnum>0 AND NOT a.attisdropped AND n.nspname !~ '^pg_' AND n.nspname <> 'information_schema' ORDER BY 1`,
		"constraints":  `SELECT jsonb_build_array(n.nspname,c.conrelid::regclass::text,c.conname,c.contype,c.convalidated,pg_get_constraintdef(c.oid,true))::text FROM pg_constraint c JOIN pg_namespace n ON n.oid=c.connamespace WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema' ORDER BY 1`,
		"indexes":      `SELECT jsonb_build_array(schemaname,tablename,indexname,indexdef)::text FROM pg_indexes WHERE schemaname !~ '^pg_' AND schemaname <> 'information_schema' ORDER BY 1`,
		"triggers":     `SELECT jsonb_build_array(n.nspname,c.relname,t.tgname,t.tgenabled,pg_get_triggerdef(t.oid,true))::text FROM pg_trigger t JOIN pg_class c ON c.oid=t.tgrelid JOIN pg_namespace n ON n.oid=c.relnamespace WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema' ORDER BY 1`,
		"functions":    `SELECT jsonb_build_array(n.nspname,p.proname,pg_get_function_identity_arguments(p.oid),pg_get_functiondef(p.oid))::text FROM pg_proc p JOIN pg_namespace n ON n.oid=p.pronamespace WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema' AND p.prokind <> 'a' ORDER BY 1`,
		"sequences":    `SELECT jsonb_build_array(n.nspname,c.relname,s.seqtypid::regtype::text,s.seqstart,s.seqincrement,s.seqmax,s.seqmin,s.seqcache,s.seqcycle)::text FROM pg_sequence s JOIN pg_class c ON c.oid=s.seqrelid JOIN pg_namespace n ON n.oid=c.relnamespace WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema' ORDER BY 1`,
		"dependencies": `SELECT jsonb_build_array(sn.nspname,s.relname,tn.nspname,t.relname,a.attname,d.deptype)::text FROM pg_depend d JOIN pg_class s ON s.oid=d.objid AND s.relkind='S' JOIN pg_namespace sn ON sn.oid=s.relnamespace JOIN pg_class t ON t.oid=d.refobjid JOIN pg_namespace tn ON tn.oid=t.relnamespace LEFT JOIN pg_attribute a ON a.attrelid=t.oid AND a.attnum=d.refobjsubid WHERE d.classid='pg_class'::regclass AND d.refclassid='pg_class'::regclass AND sn.nspname !~ '^pg_' ORDER BY 1`,
	}
	for key, query := range queries {
		rows, err := pool.Query(ctx, query)
		if err != nil {
			t.Fatal(err)
		}
		for rows.Next() {
			var value string
			if err := rows.Scan(&value); err != nil {
				t.Fatal(err)
			}
			out.Catalog[key] = append(out.Catalog[key], value)
		}
		if err := rows.Err(); err != nil {
			t.Fatal(err)
		}
		rows.Close()
	}
	rows, err := pool.Query(ctx, `SELECT n.nspname,c.relname,c.relkind::text FROM pg_class c JOIN pg_namespace n ON n.oid=c.relnamespace WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema' AND c.relkind IN ('r','p','m','S') ORDER BY 1,2`)
	if err != nil {
		t.Fatal(err)
	}
	type relation struct{ schema, name, kind string }
	var relations []relation
	for rows.Next() {
		var r relation
		if err := rows.Scan(&r.schema, &r.name, &r.kind); err != nil {
			t.Fatal(err)
		}
		relations = append(relations, r)
	}
	if err := rows.Err(); err != nil {
		t.Fatal(err)
	}
	rows.Close()
	for _, r := range relations {
		name := pgx.Identifier{r.schema, r.name}.Sanitize()
		if r.kind == "S" {
			var value string
			if err := pool.QueryRow(ctx, "SELECT jsonb_build_array(last_value,is_called)::text FROM "+name).Scan(&value); err != nil {
				t.Fatal(err)
			}
			out.Sequences[name] = value
			continue
		}
		var count int64
		var data string
		if err := pool.QueryRow(ctx, "SELECT count(*),COALESCE(jsonb_agg(to_jsonb(t) ORDER BY to_jsonb(t)::text)::text,'[]') FROM "+name+" t").Scan(&count, &data); err != nil {
			t.Fatal(err)
		}
		out.Counts[name] = count
		out.Rows[name] = data
	}
	return out
}

func assertBridgeDatabaseEqual(t *testing.T, before, after bridgeDatabaseSnapshot) {
	t.Helper()
	b, _ := json.Marshal(before)
	a, _ := json.Marshal(after)
	t.Logf("whole-database snapshot BEFORE: %s", b)
	t.Logf("whole-database snapshot AFTER: %s", a)
	if !reflect.DeepEqual(before, after) {
		t.Fatalf("whole-database changed: before=%s after=%s", b, a)
	}
	t.Logf("whole-database equality verified: %d tables, %d sequences, %d catalog sections", len(before.Counts), len(before.Sequences), len(before.Catalog))
}

func assertBridgeCountsPreserved(t *testing.T, before, after bridgeDatabaseSnapshot) {
	t.Helper()
	for name, count := range before.Counts {
		if name == `"public"."schema_migrations"` {
			continue
		}
		if afterCount, exists := after.Counts[name]; !exists || afterCount != count {
			t.Errorf("table %s count changed: %d -> %d", name, count, after.Counts[name])
		}
	}
	// Compare every original column of every original row; new canonical
	// columns are deliberately projected out, and row order is immaterial.
	for name, raw := range before.Rows {
		if name == `"public"."schema_migrations"` {
			continue
		}
		var oldRows, newRows []map[string]json.RawMessage
		if err := json.Unmarshal([]byte(raw), &oldRows); err != nil {
			t.Fatal(err)
		}
		if err := json.Unmarshal([]byte(after.Rows[name]), &newRows); err != nil {
			t.Fatal(err)
		}
		if len(oldRows) == 0 {
			continue
		}
		keys := map[string]bool{}
		for key := range oldRows[0] {
			keys[key] = true
		}
		encode := func(rows []map[string]json.RawMessage) []string {
			result := []string{}
			for _, row := range rows {
				projected := map[string]json.RawMessage{}
				for key := range keys {
					projected[key] = row[key]
				}
				b, err := json.Marshal(projected)
				if err != nil {
					t.Fatal(err)
				}
				result = append(result, string(b))
			}
			sort.Strings(result)
			return result
		}
		if !reflect.DeepEqual(encode(oldRows), encode(newRows)) {
			t.Errorf("original row values changed in %s", name)
		}
	}
	for name, value := range before.Sequences {
		target := name
		if name == `"public"."account_change_seq"` {
			target = `"public"."account_sync_journal_server_seq_seq"`
		}
		if after.Sequences[target] != value {
			t.Errorf("sequence %s state changed: %s -> %s", name, value, after.Sequences[target])
		}
	}
	b, _ := json.Marshal(before)
	a, _ := json.Marshal(after)
	t.Logf("preservation snapshot BEFORE: %s", b)
	t.Logf("preservation snapshot AFTER: %s", a)
	t.Log(fmt.Sprintf("preserved counts for %d original tables and state for %d original sequences", len(before.Counts)-1, len(before.Sequences)))
}

func TestLegacyBridgeChecksumLedgerDriftIsAtomic(t *testing.T) {
	for _, name := range []string{"0005_activity_fact_sync.sql", "0009_profile_attachments.sql"} {
		t.Run(name, func(t *testing.T) {
			pool, ctx := createLegacyBridgeFixture(t)
			seedLegacySentinels(t, ctx, pool)
			if err := RunLegacyBridge(ctx, pool); err != nil {
				t.Fatal(err)
			}
			mustFixtureExec(t, ctx, pool, "UPDATE schema_migrations SET checksum='synthetic-drift' WHERE name=$1", name)
			before := snapshotBridgeDatabase(t, ctx, pool)
			if err := RunLegacyBridge(ctx, pool); err == nil {
				t.Fatal("corrupted canonical checksum accepted")
			} else {
				t.Logf("expected checksum rejection: %v", err)
			}
			assertBridgeDatabaseEqual(t, before, snapshotBridgeDatabase(t, ctx, pool))
		})
	}
}
