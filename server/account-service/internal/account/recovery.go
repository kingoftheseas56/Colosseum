package account

import (
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base32"
	"fmt"
	"strings"
)

const recoveryKeyBytes = 16

type RecoveryKeyVerifier struct {
	key []byte
}

func NewRecoveryKeyVerifier(key []byte) (*RecoveryKeyVerifier, error) {
	if len(key) < 32 {
		return nil, fmt.Errorf("recovery HMAC key must contain at least 32 bytes")
	}
	copyOfKey := append([]byte(nil), key...)
	return &RecoveryKeyVerifier{key: copyOfKey}, nil
}

func GenerateRecoveryKey() (string, error) {
	random := make([]byte, recoveryKeyBytes)
	if _, err := rand.Read(random); err != nil {
		return "", fmt.Errorf("generate recovery key: %w", err)
	}

	encoded := base32.StdEncoding.WithPadding(base32.NoPadding).EncodeToString(random)
	groups := []int{5, 5, 5, 5, 6}
	parts := make([]string, 0, len(groups))
	offset := 0
	for _, size := range groups {
		parts = append(parts, encoded[offset:offset+size])
		offset += size
	}
	return strings.Join(parts, "-"), nil
}

func NormalizeRecoveryKey(value string) (string, error) {
	normalized := strings.ToUpper(strings.NewReplacer(
		"-", "",
		" ", "",
		"\t", "",
		"\r", "",
		"\n", "",
	).Replace(strings.TrimSpace(value)))
	if len(normalized) != 26 {
		return "", ErrRecoveryKeyInvalid
	}
	if _, err := base32.StdEncoding.WithPadding(base32.NoPadding).DecodeString(normalized); err != nil {
		return "", ErrRecoveryKeyInvalid
	}
	return normalized, nil
}

func (v *RecoveryKeyVerifier) Sum(key string) ([]byte, error) {
	normalized, err := NormalizeRecoveryKey(key)
	if err != nil {
		return nil, err
	}
	mac := hmac.New(sha256.New, v.key)
	_, _ = mac.Write([]byte(normalized))
	return mac.Sum(nil), nil
}

func (v *RecoveryKeyVerifier) Verify(key string, expected []byte) bool {
	actual, err := v.Sum(key)
	if err != nil {
		return false
	}
	return hmac.Equal(actual, expected)
}
