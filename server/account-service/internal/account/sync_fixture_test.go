package account

import (
	"encoding/json"
	"os"
	"path/filepath"
	"runtime"
	"testing"
)

type sharedActivitySyncFixture struct {
	Category      string          `json:"category"`
	SchemaVersion int             `json:"schema_version"`
	RecordKey     string          `json:"record_key"`
	Payload       json.RawMessage `json:"payload"`
}

func loadSharedActivitySyncFixture(t *testing.T) sharedActivitySyncFixture {
	t.Helper()
	_, sourceFile, _, ok := runtime.Caller(0)
	if !ok {
		t.Fatal("runtime.Caller failed while locating the shared sync fixture")
	}
	path := filepath.Join(
		filepath.Dir(sourceFile),
		"..", "..", "..", "..",
		"tests", "fixtures", "sync", "activity-playback.json")
	bytes, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read shared sync fixture %q: %v", path, err)
	}
	var fixture sharedActivitySyncFixture
	if err := json.Unmarshal(bytes, &fixture); err != nil {
		t.Fatalf("decode shared sync fixture %q: %v", path, err)
	}
	return fixture
}

func TestSharedActivityFixtureMatchesServerAdmission(t *testing.T) {
	fixture := loadSharedActivitySyncFixture(t)
	if err := validateSyncRecordShape(
		fixture.Category,
		fixture.SchemaVersion,
		fixture.RecordKey,
		"put",
		fixture.Payload,
	); err != nil {
		t.Fatalf("shared Qt Activity fixture rejected by Go admission: %v", err)
	}
}
