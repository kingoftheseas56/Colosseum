package account

import (
	"context"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/json"
	"errors"
	"time"

	"github.com/jackc/pgx/v5"
)

type PublishCollectionInput struct {
	OperationID string             `json:"operation_id"`
	Kind        string             `json:"kind"`
	Snapshot    CollectionSnapshot `json:"snapshot"`
	Passphrase  string             `json:"passphrase"`
}
type UpdateCollectionInput struct {
	OperationID      string             `json:"operation_id"`
	ExpectedRevision int64              `json:"expected_revision"`
	Snapshot         CollectionSnapshot `json:"snapshot"`
	Passphrase       string             `json:"passphrase"`
}
type RevokeCollectionInput struct {
	OperationID      string `json:"operation_id"`
	ExpectedRevision int64  `json:"expected_revision"`
}
type CollectionPublication struct {
	ShareID   string             `json:"share_id"`
	Kind      string             `json:"kind"`
	Revision  int64              `json:"revision"`
	Snapshot  CollectionSnapshot `json:"snapshot"`
	Revoked   bool               `json:"revoked"`
	Protected bool               `json:"protected"`
	CreatedAt time.Time          `json:"created_at"`
	UpdatedAt time.Time          `json:"updated_at"`
}
type CollectionPublicationSummary struct {
	ShareID   string    `json:"share_id"`
	Kind      string    `json:"kind"`
	Revision  int64     `json:"revision"`
	Name      string    `json:"name"`
	Revoked   bool      `json:"revoked"`
	Protected bool      `json:"protected"`
	CreatedAt time.Time `json:"created_at"`
	UpdatedAt time.Time `json:"updated_at"`
}

const publicationColumns = `share_id,kind,revision,snapshot,revoked,(passphrase_hash <> ''),created_at,updated_at`

func scanPublication(row pgx.Row) (CollectionPublication, error) {
	var p CollectionPublication
	var raw []byte
	if err := row.Scan(&p.ShareID, &p.Kind, &p.Revision, &raw, &p.Revoked, &p.Protected, &p.CreatedAt, &p.UpdatedAt); err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return p, ErrCollectionNotFound
		}
		return p, err
	}
	if err := json.Unmarshal(raw, &p.Snapshot); err != nil {
		return p, err
	}
	return p, nil
}
func (s *Service) collectionOperation(ctx context.Context, accountID, operationID, action, target string, input any, apply func(pgx.Tx) (CollectionPublication, error)) (CollectionPublication, error) {
	var zero CollectionPublication
	if !IsUUID(accountID) || !IsUUID(operationID) {
		return zero, ErrCollectionInvalid
	}
	raw, err := json.Marshal(input)
	if err != nil || len(raw) > int(CollectionMaxBodyBytes) {
		return zero, ErrCollectionInvalid
	}
	// Keyed receipt digest does not expose a dictionary-testable passphrase.
	mac := hmac.New(sha256.New, s.rateLimiter.key)
	mac.Write([]byte(action + "\x00" + target + "\x00"))
	mac.Write(raw)
	digest := mac.Sum(nil)
	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return zero, err
	}
	defer func() { _ = tx.Rollback(ctx) }()
	// One account lock serializes mutation receipts, quota checks, concurrent
	// updates and account deletion. No response can precede a committed receipt.
	var owner string
	if err = tx.QueryRow(ctx, `SELECT id::text FROM accounts WHERE id=$1::uuid FOR UPDATE`, accountID).Scan(&owner); err != nil {
		return zero, ErrSessionInvalid
	}
	var oldDigest []byte
	var oldShare string
	err = tx.QueryRow(ctx, `SELECT request_hash,share_id FROM collection_publication_operations WHERE account_id=$1::uuid AND operation_id=$2::uuid`, accountID, operationID).Scan(&oldDigest, &oldShare)
	if err == nil {
		if !hmac.Equal(oldDigest, digest) {
			return zero, ErrCollectionIdempotency
		}
		result, err := scanPublication(tx.QueryRow(ctx, `SELECT `+publicationColumns+` FROM collection_publications WHERE account_id=$1::uuid AND share_id=$2`, accountID, oldShare))
		return result, err
	}
	if !errors.Is(err, pgx.ErrNoRows) {
		return zero, err
	}
	var operationCount int
	if err = tx.QueryRow(ctx, `SELECT count(*) FROM collection_publication_operations WHERE account_id=$1::uuid`, accountID).Scan(&operationCount); err != nil {
		return zero, err
	}
	if action == "revoke" {
		current, readErr := scanPublication(tx.QueryRow(ctx, `SELECT `+publicationColumns+` FROM collection_publications WHERE account_id=$1::uuid AND share_id=$2`, accountID, target))
		if readErr != nil {
			return zero, readErr
		}
		if current.Revoked {
			return current, nil
		}
	}
	if action != "revoke" && operationCount >= 5000 {
		return zero, ErrCollectionLimit
	}
	if action != "revoke" {
		if _, err = s.rateLimiter.ReserveTx(ctx, tx, "collection_publish", []string{accountID}, time.Hour, 100); err != nil {
			return zero, err
		}
	}
	p, err := apply(tx)
	if err != nil {
		return zero, err
	}
	if _, err = tx.Exec(ctx, `INSERT INTO collection_publication_operations(account_id,operation_id,request_hash,share_id) VALUES($1::uuid,$2::uuid,$3,$4)`, accountID, operationID, digest, p.ShareID); err != nil {
		return zero, err
	}
	if err = tx.Commit(ctx); err != nil {
		return zero, err
	}
	return p, nil
}
func validCollectionPassphrase(passphrase string) bool {
	return collectionText(passphrase, 128) && (passphrase == "" || len([]rune(passphrase)) >= 8)
}
func (s *Service) PublishCollection(ctx context.Context, accountID string, input PublishCollectionInput) (CollectionPublication, error) {
	if (input.Kind != "collection" && input.Kind != "gift") || ValidateCollectionSnapshot(input.Snapshot) != nil || !validCollectionPassphrase(input.Passphrase) {
		return CollectionPublication{}, ErrCollectionInvalid
	}
	return s.collectionOperation(ctx, accountID, input.OperationID, "create", "", input, func(tx pgx.Tx) (CollectionPublication, error) {
		var count int
		if err := tx.QueryRow(ctx, `SELECT count(*) FROM collection_publications WHERE account_id=$1::uuid`, accountID).Scan(&count); err != nil {
			return CollectionPublication{}, err
		}
		if count >= 200 {
			return CollectionPublication{}, ErrCollectionLimit
		}
		shareID, err := GenerateToken()
		if err != nil {
			return CollectionPublication{}, err
		}
		hash := ""
		if input.Passphrase != "" {
			hash, err = s.passwordHasher.Hash(input.Passphrase)
			if err != nil {
				return CollectionPublication{}, err
			}
		}
		snapshot, err := sanitizeCollectionSnapshot(input.Snapshot)
		if err != nil {
			return CollectionPublication{}, err
		}
		raw, _ := json.Marshal(snapshot)
		now := s.clock.Now()
		return scanPublication(tx.QueryRow(ctx, `INSERT INTO collection_publications(share_id,account_id,kind,revision,snapshot,passphrase_hash,created_at,updated_at) VALUES($1,$2::uuid,$3,1,$4::jsonb,$5,$6,$6) RETURNING `+publicationColumns, shareID, accountID, input.Kind, raw, hash, now))
	})
}
func (s *Service) UpdateCollectionPublication(ctx context.Context, accountID, shareID string, input UpdateCollectionInput) (CollectionPublication, error) {
	if !collectionShareID.MatchString(shareID) || input.ExpectedRevision < 1 || ValidateCollectionSnapshot(input.Snapshot) != nil || !validCollectionPassphrase(input.Passphrase) {
		return CollectionPublication{}, ErrCollectionInvalid
	}
	return s.collectionOperation(ctx, accountID, input.OperationID, "update", shareID, input, func(tx pgx.Tx) (CollectionPublication, error) {
		old, err := scanPublication(tx.QueryRow(ctx, `SELECT `+publicationColumns+` FROM collection_publications WHERE account_id=$1::uuid AND share_id=$2`, accountID, shareID))
		if err != nil {
			return old, err
		}
		if old.Revoked {
			return old, ErrCollectionNotFound
		}
		if old.Kind == "gift" {
			return old, ErrCollectionImmutable
		}
		if old.Revision != input.ExpectedRevision {
			return old, ErrCollectionConflict
		}
		hash := ""
		if input.Passphrase != "" {
			hash, err = s.passwordHasher.Hash(input.Passphrase)
			if err != nil {
				return old, err
			}
		}
		snapshot, err := sanitizeCollectionSnapshot(input.Snapshot)
		if err != nil {
			return old, err
		}
		raw, _ := json.Marshal(snapshot)
		return scanPublication(tx.QueryRow(ctx, `UPDATE collection_publications SET revision=revision+1,snapshot=$3::jsonb,passphrase_hash=$4,updated_at=$5 WHERE account_id=$1::uuid AND share_id=$2 RETURNING `+publicationColumns, accountID, shareID, raw, hash, s.clock.Now()))
	})
}
func (s *Service) RevokeCollectionPublication(ctx context.Context, accountID, shareID string, input RevokeCollectionInput) (CollectionPublication, error) {
	if !collectionShareID.MatchString(shareID) || input.ExpectedRevision < 1 {
		return CollectionPublication{}, ErrCollectionInvalid
	}
	return s.collectionOperation(ctx, accountID, input.OperationID, "revoke", shareID, input, func(tx pgx.Tx) (CollectionPublication, error) {
		old, err := scanPublication(tx.QueryRow(ctx, `SELECT `+publicationColumns+` FROM collection_publications WHERE account_id=$1::uuid AND share_id=$2`, accountID, shareID))
		if err != nil {
			return old, err
		}
		if old.Revision != input.ExpectedRevision {
			return old, ErrCollectionConflict
		}
		return scanPublication(tx.QueryRow(ctx, `UPDATE collection_publications SET revoked=true,updated_at=$3 WHERE account_id=$1::uuid AND share_id=$2 RETURNING `+publicationColumns, accountID, shareID, s.clock.Now()))
	})
}
func (s *Service) ListCollectionPublications(ctx context.Context, accountID string) ([]CollectionPublicationSummary, error) {
	rows, err := s.pool.Query(ctx, `SELECT share_id,kind,revision,snapshot->>'name',revoked,(passphrase_hash<>''),created_at,updated_at FROM collection_publications WHERE account_id=$1::uuid ORDER BY created_at DESC,share_id LIMIT 200`, accountID)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	result := []CollectionPublicationSummary{}
	for rows.Next() {
		var p CollectionPublicationSummary
		if err = rows.Scan(&p.ShareID, &p.Kind, &p.Revision, &p.Name, &p.Revoked, &p.Protected, &p.CreatedAt, &p.UpdatedAt); err != nil {
			return nil, err
		}
		result = append(result, p)
	}
	return result, rows.Err()
}
func (s *Service) OpenCollectionPublication(ctx context.Context, shareID, passphrase, networkKey string) (CollectionPublication, error) {
	var zero CollectionPublication
	if !collectionShareID.MatchString(shareID) {
		return zero, ErrCollectionNotFound
	}
	if !collectionText(passphrase, 128) {
		return zero, ErrCollectionPassphrase
	}
	// A source-wide bound covers random-ID probing; a share/source bound protects
	// the expensive verifier without letting one stranger lock out every reader.
	if err := s.rateLimiter.Allow(ctx, "collection_open", []string{networkKey}, time.Minute, 120); err != nil {
		return zero, err
	}
	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return zero, err
	}
	defer func() { _ = tx.Rollback(ctx) }()
	var hash string
	if err = tx.QueryRow(ctx, `SELECT passphrase_hash FROM collection_publications WHERE share_id=$1 AND NOT revoked FOR SHARE`, shareID).Scan(&hash); err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return zero, ErrCollectionNotFound
		}
		return zero, err
	}
	if hash != "" {
		if passphrase == "" {
			return zero, ErrCollectionPassphraseRequired
		}
		// Use a transaction-local rate reservation to avoid a second pool lease
		// while holding the publication read lock.
		if _, err = s.rateLimiter.ReserveTx(ctx, tx, "collection_passphrase", []string{shareID, networkKey}, 15*time.Minute, 10); err != nil {
			return zero, err
		}
		ok, verifyErr := s.passwordVerify(hash, passphrase)
		if verifyErr != nil || !ok {
			if err = tx.Commit(ctx); err != nil {
				return zero, err
			}
			return zero, ErrCollectionPassphrase
		}
	}
	p, err := scanPublication(tx.QueryRow(ctx, `SELECT `+publicationColumns+` FROM collection_publications WHERE share_id=$1 AND NOT revoked`, shareID))
	if err != nil {
		return zero, err
	}
	if err = tx.Commit(ctx); err != nil {
		return zero, err
	}
	return p, nil
}
