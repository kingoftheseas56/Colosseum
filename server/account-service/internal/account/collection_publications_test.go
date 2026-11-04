package account

import (
	"context"
	"encoding/json"
	"errors"
	"os"
	"strings"
	"testing"
	"time"
)

func publicationFixture() CollectionSnapshot {
	return CollectionSnapshot{SchemaVersion: 1, Name: "Across worlds", Presentation: CollectionPresentation{Preset: "midnight"}, Members: []CollectionMember{{World: "tankoban", Kind: "manga", Source: "mal", SourceID: "21", Title: "One Piece", Note: "Start here"}, {World: "theatre", Kind: "movie", Source: "theatre", SourceID: "tt0076759", Title: "Star Wars"}}}
}
func TestCollectionProjectionValidation(t *testing.T) {
	s := publicationFixture()
	if err := ValidateCollectionSnapshot(s); err != nil {
		t.Fatal(err)
	}
	cases := []struct {
		name string
		edit func(*CollectionSnapshot)
	}{
		{"unknown schema", func(s *CollectionSnapshot) { s.SchemaVersion = 2 }},
		{"private path", func(s *CollectionSnapshot) { s.Members[0].SourceID = `C:\Users\private` }},
		{"endpoint", func(s *CollectionSnapshot) { s.Members[0].SourceID = "https://provider.invalid/private?token=x" }},
		{"oversize note", func(s *CollectionSnapshot) { s.Members[0].Note = strings.Repeat("x", 501) }},
		{"duplicate", func(s *CollectionSnapshot) { s.Members = append(s.Members, s.Members[0]) }},
		{"svg", func(s *CollectionSnapshot) { s.Presentation.CoverData = "data:image/svg+xml;base64,PHN2Zz4=" }},
		{"unknown preset", func(s *CollectionSnapshot) { s.Presentation.Preset = "html" }},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			s := publicationFixture()
			tc.edit(&s)
			if ValidateCollectionSnapshot(s) == nil {
				t.Fatal("accepted invalid projection")
			}
		})
	}
}
func TestCollectionPublicationLifecycle(t *testing.T) {
	f := newServiceFixture(t)
	owner := createFixtureAccount(t, f, "publisher_one")
	other := createFixtureAccount(t, f, "publisher_two")
	ctx := context.Background()
	in := PublishCollectionInput{OperationID: "11111111-1111-4111-8111-111111111111", Kind: "collection", Snapshot: publicationFixture()}
	pub, err := f.service.PublishCollection(ctx, owner.Session.Account.ID, in)
	if err != nil {
		t.Fatal(err)
	}
	retry, err := f.service.PublishCollection(ctx, owner.Session.Account.ID, in)
	if err != nil || retry.ShareID != pub.ShareID {
		t.Fatalf("retry: %+v %v", retry, err)
	}
	in.Snapshot.Name = "Different"
	if _, err = f.service.PublishCollection(ctx, owner.Session.Account.ID, in); !errors.Is(err, ErrCollectionIdempotency) {
		t.Fatalf("conflicting retry: %v", err)
	}
	update := UpdateCollectionInput{OperationID: "22222222-2222-4222-8222-222222222222", ExpectedRevision: 1, Snapshot: in.Snapshot}
	if _, err = f.service.UpdateCollectionPublication(ctx, other.Session.Account.ID, pub.ShareID, update); !errors.Is(err, ErrCollectionNotFound) {
		t.Fatalf("foreign owner: %v", err)
	}
	before, err := f.service.OpenCollectionPublication(ctx, pub.ShareID, "", "fixture")
	if err != nil || before.Snapshot.Name != "Across worlds" {
		t.Fatalf("private edits leaked: %v", err)
	}
	updated, err := f.service.UpdateCollectionPublication(ctx, owner.Session.Account.ID, pub.ShareID, update)
	if err != nil || updated.Revision != 2 {
		t.Fatalf("update %v", err)
	}
	update.OperationID = "33333333-3333-4333-8333-333333333333"
	if _, err = f.service.UpdateCollectionPublication(ctx, owner.Session.Account.ID, pub.ShareID, update); !errors.Is(err, ErrCollectionConflict) {
		t.Fatalf("stale %v", err)
	}
	giftIn := PublishCollectionInput{OperationID: "44444444-4444-4444-8444-444444444444", Kind: "gift", Snapshot: publicationFixture(), Passphrase: "secret gift phrase"}
	gift, err := f.service.PublishCollection(ctx, owner.Session.Account.ID, giftIn)
	if err != nil {
		t.Fatal(err)
	}
	if _, err = f.service.OpenCollectionPublication(ctx, gift.ShareID, "wrong", "fixture"); !errors.Is(err, ErrCollectionPassphrase) {
		t.Fatalf("wrong passphrase %v", err)
	}
	if _, err = f.service.OpenCollectionPublication(ctx, gift.ShareID, giftIn.Passphrase, "fixture"); err != nil {
		t.Fatal(err)
	}
	update.ExpectedRevision = 1
	if _, err = f.service.UpdateCollectionPublication(ctx, owner.Session.Account.ID, gift.ShareID, update); !errors.Is(err, ErrCollectionImmutable) {
		t.Fatalf("gift mutation %v", err)
	}
	_, err = f.service.RevokeCollectionPublication(ctx, owner.Session.Account.ID, gift.ShareID, RevokeCollectionInput{OperationID: "55555555-5555-4555-8555-555555555555", ExpectedRevision: 1})
	if err != nil {
		t.Fatal(err)
	}
	if _, err = f.service.OpenCollectionPublication(ctx, gift.ShareID, giftIn.Passphrase, "fixture"); !errors.Is(err, ErrCollectionNotFound) {
		t.Fatalf("revoked open %v", err)
	}
}

func TestCollectionSharedFixture(t *testing.T) {
	raw, err := os.ReadFile("../../../../tests/collections_sharing/fixtures/mixed-gift-v1.json")
	if err != nil {
		t.Fatal(err)
	}
	var snapshot CollectionSnapshot
	if err = json.Unmarshal(raw, &snapshot); err != nil {
		t.Fatal(err)
	}
	if err = ValidateCollectionSnapshot(snapshot); err != nil {
		t.Fatal(err)
	}
	sanitized, err := sanitizeCollectionSnapshot(snapshot)
	if err != nil {
		t.Fatal(err)
	}
	if err = ValidateCollectionSnapshot(sanitized); err != nil {
		t.Fatal(err)
	}
}

func TestCollectionConcurrentRetryAndRollback(t *testing.T) {
	f := newServiceFixture(t)
	owner := createFixtureAccount(t, f, "publisher_race")
	ctx := context.Background()
	in := PublishCollectionInput{OperationID: "11111111-1111-4111-8111-111111111111", Kind: "gift", Snapshot: publicationFixture()}
	type result struct {
		p   CollectionPublication
		err error
	}
	results := make(chan result, 8)
	for i := 0; i < 8; i++ {
		go func() {
			p, err := f.service.PublishCollection(ctx, owner.Session.Account.ID, in)
			results <- result{p, err}
		}()
	}
	id := ""
	for i := 0; i < 8; i++ {
		r := <-results
		if r.err != nil {
			t.Fatal(r.err)
		}
		if id != "" && id != r.p.ShareID {
			t.Fatal("duplicate publication")
		}
		id = r.p.ShareID
	}
	var count int
	if err := f.pool.QueryRow(ctx, `SELECT count(*) FROM collection_publications`).Scan(&count); err != nil || count != 1 {
		t.Fatalf("count=%d %v", count, err)
	}
	if _, err := f.pool.Exec(ctx, `CREATE FUNCTION reject_publication() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN RAISE EXCEPTION 'synthetic interrupted save'; END $$; CREATE TRIGGER reject_publication BEFORE INSERT ON collection_publications FOR EACH ROW EXECUTE FUNCTION reject_publication()`); err != nil {
		t.Fatal(err)
	}
	in.OperationID = "22222222-2222-4222-8222-222222222222"
	if _, err := f.service.PublishCollection(ctx, owner.Session.Account.ID, in); err == nil {
		t.Fatal("interrupted write succeeded")
	}
	if _, err := f.pool.Exec(ctx, `DROP TRIGGER reject_publication ON collection_publications; DROP FUNCTION reject_publication()`); err != nil {
		t.Fatal(err)
	}
	if _, err := f.service.PublishCollection(ctx, owner.Session.Account.ID, in); err != nil {
		t.Fatalf("retry after rollback: %v", err)
	}
	if err := f.pool.QueryRow(ctx, `SELECT count(*) FROM collection_publication_operations`).Scan(&count); err != nil || count != 2 {
		t.Fatalf("receipt count=%d %v", count, err)
	}
}
func TestCollectionPassphraseRateLimit(t *testing.T) {
	f := newServiceFixture(t)
	owner := createFixtureAccount(t, f, "publisher_lock")
	ctx := context.Background()
	pub, err := f.service.PublishCollection(ctx, owner.Session.Account.ID, PublishCollectionInput{OperationID: "11111111-1111-4111-8111-111111111111", Kind: "gift", Snapshot: publicationFixture(), Passphrase: "a bounded passphrase"})
	if err != nil {
		t.Fatal(err)
	}
	for i := 0; i < 10; i++ {
		if _, err = f.service.OpenCollectionPublication(ctx, pub.ShareID, "incorrect", "attacker"); !errors.Is(err, ErrCollectionPassphrase) {
			t.Fatalf("attempt %d: %v", i, err)
		}
	}
	_, err = f.service.OpenCollectionPublication(ctx, pub.ShareID, "a bounded passphrase", "attacker")
	var rate *RateLimitError
	if !errors.As(err, &rate) {
		t.Fatalf("not throttled: %v", err)
	}
	if _, err = f.service.OpenCollectionPublication(ctx, pub.ShareID, "a bounded passphrase", "recipient"); err != nil {
		t.Fatalf("another recipient locked out: %v", err)
	}
	// The server stores only a verifier, never a passphrase or whole native row.
	var hash string
	if err = f.pool.QueryRow(ctx, `SELECT passphrase_hash FROM collection_publications WHERE share_id=$1`, pub.ShareID).Scan(&hash); err != nil {
		t.Fatal(err)
	}
	if !strings.HasPrefix(hash, "$argon2id$") || strings.Contains(hash, "bounded") {
		t.Fatal("unsafe passphrase storage")
	}
}

func TestCollectionRevocationRemainsAvailableAtQuota(t *testing.T) {
	f := newServiceFixture(t)
	owner := createFixtureAccount(t, f, "publisher_quota")
	ctx := context.Background()
	p, err := f.service.PublishCollection(ctx, owner.Session.Account.ID, PublishCollectionInput{OperationID: "11111111-1111-4111-8111-111111111111", Kind: "collection", Snapshot: publicationFixture()})
	if err != nil {
		t.Fatal(err)
	}
	for i := 0; i < 99; i++ {
		if err = f.service.rateLimiter.Allow(ctx, "collection_publish", []string{owner.Session.Account.ID}, time.Hour, 100); err != nil {
			t.Fatal(err)
		}
	}
	_, err = f.service.UpdateCollectionPublication(ctx, owner.Session.Account.ID, p.ShareID, UpdateCollectionInput{OperationID: "22222222-2222-4222-8222-222222222222", ExpectedRevision: 1, Snapshot: publicationFixture()})
	var limit *RateLimitError
	if !errors.As(err, &limit) {
		t.Fatalf("update not bounded: %v", err)
	}
	revoked, err := f.service.RevokeCollectionPublication(ctx, owner.Session.Account.ID, p.ShareID, RevokeCollectionInput{OperationID: "33333333-3333-4333-8333-333333333333", ExpectedRevision: 1})
	if err != nil || !revoked.Revoked {
		t.Fatalf("privacy control blocked by quota: %v", err)
	}
	_, err = f.service.RevokeCollectionPublication(ctx, owner.Session.Account.ID, p.ShareID, RevokeCollectionInput{OperationID: "44444444-4444-4444-8444-444444444444", ExpectedRevision: 1})
	if err != nil {
		t.Fatal(err)
	}
	var count int
	if err = f.pool.QueryRow(ctx, `SELECT count(*) FROM collection_publication_operations`).Scan(&count); err != nil || count != 2 {
		t.Fatalf("repeat revoke allocated receipt: %d %v", count, err)
	}
}
