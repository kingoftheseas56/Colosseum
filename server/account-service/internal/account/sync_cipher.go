package account

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/rand"
	"fmt"
)

type SyncPayloadCipher struct {
	aead cipher.AEAD
}

func NewSyncPayloadCipher(key []byte) (*SyncPayloadCipher, error) {
	if len(key) != 32 {
		return nil, fmt.Errorf("sync data key must contain exactly 32 bytes")
	}
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, fmt.Errorf("create sync payload cipher: %w", err)
	}
	aead, err := cipher.NewGCM(block)
	if err != nil {
		return nil, fmt.Errorf("create sync payload GCM: %w", err)
	}
	return &SyncPayloadCipher{aead: aead}, nil
}

func (c *SyncPayloadCipher) Seal(
	accountID,
	category,
	recordKey string,
	plain []byte,
) ([]byte, error) {
	nonce := make([]byte, c.aead.NonceSize())
	if _, err := rand.Read(nonce); err != nil {
		return nil, fmt.Errorf("generate sync cipher nonce: %w", err)
	}
	sealed := c.aead.Seal(
		nil,
		nonce,
		plain,
		syncPayloadAAD(accountID, category, recordKey))
	return append(nonce, sealed...), nil
}

func (c *SyncPayloadCipher) Open(
	accountID,
	category,
	recordKey string,
	ciphertext []byte,
) ([]byte, error) {
	if len(ciphertext) < c.aead.NonceSize() {
		return nil, fmt.Errorf("sync payload ciphertext is truncated")
	}
	nonce := ciphertext[:c.aead.NonceSize()]
	body := ciphertext[c.aead.NonceSize():]
	plain, err := c.aead.Open(
		nil,
		nonce,
		body,
		syncPayloadAAD(accountID, category, recordKey))
	if err != nil {
		return nil, fmt.Errorf("open sync payload: %w", err)
	}
	return plain, nil
}

func syncPayloadAAD(
	accountID,
	category,
	recordKey string,
) []byte {
	return []byte(
		"colosseum-sync-payload-v1\x1f" +
			accountID + "\x1f" +
			category + "\x1f" +
			recordKey)
}
