package account

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/rand"
	"fmt"
)

var (
	refreshRetryAAD         = []byte("colosseum-refresh-retry-v1")
	trustedRecoveryRetryAAD = []byte("colosseum-trusted-recovery-retry-v1")
)

type SessionCipher struct {
	aead cipher.AEAD
}

func NewSessionCipher(key []byte) (*SessionCipher, error) {
	if len(key) != 32 {
		return nil, fmt.Errorf("session wrap key must contain exactly 32 bytes")
	}
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, fmt.Errorf("create session cipher: %w", err)
	}
	aead, err := cipher.NewGCM(block)
	if err != nil {
		return nil, fmt.Errorf("create session GCM: %w", err)
	}
	return &SessionCipher{aead: aead}, nil
}

func (c *SessionCipher) Seal(value string) ([]byte, error) {
	return c.seal(value, refreshRetryAAD)
}

func (c *SessionCipher) Open(ciphertext []byte) (string, error) {
	return c.open(ciphertext, refreshRetryAAD)
}

func (c *SessionCipher) SealTrustedRecoveryRetry(value string) ([]byte, error) {
	return c.seal(value, trustedRecoveryRetryAAD)
}

func (c *SessionCipher) OpenTrustedRecoveryRetry(ciphertext []byte) (string, error) {
	return c.open(ciphertext, trustedRecoveryRetryAAD)
}

func (c *SessionCipher) seal(value string, aad []byte) ([]byte, error) {
	nonce := make([]byte, c.aead.NonceSize())
	if _, err := rand.Read(nonce); err != nil {
		return nil, fmt.Errorf("generate session cipher nonce: %w", err)
	}
	sealed := c.aead.Seal(nil, nonce, []byte(value), aad)
	return append(nonce, sealed...), nil
}

func (c *SessionCipher) open(ciphertext, aad []byte) (string, error) {
	if len(ciphertext) < c.aead.NonceSize() {
		return "", ErrSessionInvalid
	}
	nonce := ciphertext[:c.aead.NonceSize()]
	body := ciphertext[c.aead.NonceSize():]
	plain, err := c.aead.Open(nil, nonce, body, aad)
	if err != nil {
		return "", ErrSessionInvalid
	}
	return string(plain), nil
}
