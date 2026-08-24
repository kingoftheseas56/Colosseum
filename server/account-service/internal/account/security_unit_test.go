package account

import (
	"bytes"
	"testing"
)

func TestNormalizeUsernamePreservesDisplayCaseAndCanonicalizesASCII(t *testing.T) {
	display, canonical, err := NormalizeUsername("Hemanth_56")
	if err != nil {
		t.Fatalf("NormalizeUsername() error = %v", err)
	}
	if display != "Hemanth_56" {
		t.Fatalf("display = %q, want Hemanth_56", display)
	}
	if canonical != "hemanth_56" {
		t.Fatalf("canonical = %q, want hemanth_56", canonical)
	}
}

func TestNormalizeUsernameRejectsReservedAndUnicodeNames(t *testing.T) {
	for _, username := range []string{
		"admin",
		"Colosseum",
		"éclair",
		"_startsWrong",
		"endsWrong_",
	} {
		if _, _, err := NormalizeUsername(username); err == nil {
			t.Fatalf("NormalizeUsername(%q) succeeded, want rejection", username)
		}
	}
}

func TestPasswordPolicyDoesNotTrimValidSpaces(t *testing.T) {
	policy := PasswordPolicy{
		Blocklist: NewMemoryPasswordBlocklist(nil),
	}
	password := " leading and trailing spaces are valid "
	normalized, err := policy.Validate(password, "example")
	if err != nil {
		t.Fatalf("Validate() error = %v", err)
	}
	if normalized != password {
		t.Fatalf("normalized password = %q, want exact spaces preserved", normalized)
	}
}

func TestPasswordHasherRoundTripAndWrongPassword(t *testing.T) {
	hasher, err := NewPasswordHasher(DefaultArgon2Params())
	if err != nil {
		t.Fatalf("NewPasswordHasher() error = %v", err)
	}

	encoded, err := hasher.Hash(testPassword)
	if err != nil {
		t.Fatalf("Hash() error = %v", err)
	}
	ok, err := hasher.Verify(encoded, testPassword)
	if err != nil {
		t.Fatalf("Verify(correct) error = %v", err)
	}
	if !ok {
		t.Fatal("Verify(correct) = false")
	}

	ok, err = hasher.Verify(encoded, secondPassword)
	if err != nil {
		t.Fatalf("Verify(wrong) error = %v", err)
	}
	if ok {
		t.Fatal("Verify(wrong) = true")
	}
}

func TestRecoveryVerifierRoundTripAndFormatting(t *testing.T) {
	verifier, err := NewRecoveryKeyVerifier(bytes.Repeat([]byte{0x41}, 32))
	if err != nil {
		t.Fatalf("NewRecoveryKeyVerifier() error = %v", err)
	}
	key, err := GenerateRecoveryKey()
	if err != nil {
		t.Fatalf("GenerateRecoveryKey() error = %v", err)
	}
	sum, err := verifier.Sum(key)
	if err != nil {
		t.Fatalf("Sum() error = %v", err)
	}
	if !verifier.Verify(key, sum) {
		t.Fatal("Verify() rejected generated recovery key")
	}

	compact := ""
	for _, runeValue := range key {
		if runeValue != '-' {
			compact += string(runeValue)
		}
	}
	if !verifier.Verify(compact, sum) {
		t.Fatal("Verify() rejected compact recovery key")
	}
}

func TestSessionCipherRejectsTamper(t *testing.T) {
	cipher, err := NewSessionCipher(bytes.Repeat([]byte{0x52}, 32))
	if err != nil {
		t.Fatalf("NewSessionCipher() error = %v", err)
	}
	sealed, err := cipher.Seal("refresh-token")
	if err != nil {
		t.Fatalf("Seal() error = %v", err)
	}
	sealed[len(sealed)-1] ^= 0x01
	if _, err := cipher.Open(sealed); err == nil {
		t.Fatal("Open() accepted tampered ciphertext")
	}
}
