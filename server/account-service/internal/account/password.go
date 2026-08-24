package account

import (
	"bufio"
	"crypto/rand"
	"crypto/subtle"
	_ "embed"
	"encoding/base64"
	"fmt"
	"io"
	"os"
	"strconv"
	"strings"
	"unicode/utf8"

	"golang.org/x/crypto/argon2"
	"golang.org/x/text/unicode/norm"
)

const (
	DefaultArgonMemoryKiB uint32 = 19 * 1024
	DefaultArgonTime      uint32 = 2
	DefaultArgonThreads   uint8  = 1
	DefaultArgonKeyLength uint32 = 32
	DefaultArgonSaltBytes        = 16
)

//go:embed baseline_passwords.txt
var baselinePasswordBlocklist string

type PasswordBlocklist interface {
	Contains(password string) bool
}

type MemoryPasswordBlocklist struct {
	values map[string]struct{}
}

func NewMemoryPasswordBlocklist(values []string) *MemoryPasswordBlocklist {
	blocklist := &MemoryPasswordBlocklist{values: make(map[string]struct{}, len(values))}
	for _, value := range values {
		normalized := strings.ToLower(norm.NFC.String(strings.TrimSpace(value)))
		if normalized != "" {
			blocklist.values[normalized] = struct{}{}
		}
	}
	return blocklist
}

func LoadPasswordBlocklist(path string) (*MemoryPasswordBlocklist, error) {
	path = strings.TrimSpace(path)
	if path == "" {
		return passwordBlocklistFromReader(
			strings.NewReader(baselinePasswordBlocklist),
			"embedded baseline")
	}

	file, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("open password blocklist: %w", err)
	}
	defer file.Close()

	return passwordBlocklistFromReader(file, path)
}

func passwordBlocklistFromReader(reader io.Reader, source string) (*MemoryPasswordBlocklist, error) {
	values := make([]string, 0, 10000)
	scanner := bufio.NewScanner(reader)
	scanner.Buffer(make([]byte, 64*1024), 1024*1024)
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		values = append(values, line)
	}
	if err := scanner.Err(); err != nil {
		return nil, fmt.Errorf("read password blocklist %s: %w", source, err)
	}
	if len(values) == 0 {
		return nil, fmt.Errorf("password blocklist %s is empty", source)
	}
	return NewMemoryPasswordBlocklist(values), nil
}

func (b *MemoryPasswordBlocklist) Contains(password string) bool {
	if b == nil {
		return false
	}
	_, found := b.values[strings.ToLower(norm.NFC.String(password))]
	return found
}

type PasswordPolicy struct {
	Blocklist PasswordBlocklist
}

func (p PasswordPolicy) Validate(password, canonicalUsername string) (string, error) {
	normalized := norm.NFC.String(password)
	length := utf8.RuneCountInString(normalized)
	if length < 8 || length > 128 {
		return "", ErrInvalidPassword
	}
	if p.Blocklist != nil && p.Blocklist.Contains(normalized) {
		return "", ErrInvalidPassword
	}

	lower := strings.ToLower(normalized)
	contextValues := []string{
		"colosseum",
		"brotherhood",
		strings.ToLower(strings.TrimSpace(canonicalUsername)),
	}
	for _, value := range contextValues {
		if value == "" {
			continue
		}
		if lower == value || lower == value+"123" || lower == value+"1234" {
			return "", ErrInvalidPassword
		}
	}
	return normalized, nil
}

type Argon2Params struct {
	MemoryKiB uint32
	Time      uint32
	Threads   uint8
	KeyLength uint32
	SaltBytes int
}

func DefaultArgon2Params() Argon2Params {
	return Argon2Params{
		MemoryKiB: DefaultArgonMemoryKiB,
		Time:      DefaultArgonTime,
		Threads:   DefaultArgonThreads,
		KeyLength: DefaultArgonKeyLength,
		SaltBytes: DefaultArgonSaltBytes,
	}
}

type PasswordHasher struct {
	params Argon2Params
}

func NewPasswordHasher(params Argon2Params) (*PasswordHasher, error) {
	if params.MemoryKiB < DefaultArgonMemoryKiB ||
		params.Time < DefaultArgonTime ||
		params.Threads < DefaultArgonThreads ||
		params.KeyLength < 32 ||
		params.SaltBytes < 16 {
		return nil, fmt.Errorf("argon2 parameters are below the approved security floor")
	}
	return &PasswordHasher{params: params}, nil
}

func (h *PasswordHasher) Hash(password string) (string, error) {
	salt := make([]byte, h.params.SaltBytes)
	if _, err := rand.Read(salt); err != nil {
		return "", fmt.Errorf("generate password salt: %w", err)
	}

	hash := argon2.IDKey(
		[]byte(password),
		salt,
		h.params.Time,
		h.params.MemoryKiB,
		h.params.Threads,
		h.params.KeyLength)

	return fmt.Sprintf(
		"$argon2id$v=19$m=%d,t=%d,p=%d$%s$%s",
		h.params.MemoryKiB,
		h.params.Time,
		h.params.Threads,
		base64.RawStdEncoding.EncodeToString(salt),
		base64.RawStdEncoding.EncodeToString(hash)), nil
}

func (h *PasswordHasher) Verify(encoded, password string) (bool, error) {
	parts := strings.Split(encoded, "$")
	if len(parts) != 6 || parts[1] != "argon2id" || parts[2] != "v=19" {
		return false, fmt.Errorf("unsupported password hash format")
	}

	var memory uint64
	var iterations uint64
	var threads uint64
	for _, parameter := range strings.Split(parts[3], ",") {
		key, value, found := strings.Cut(parameter, "=")
		if !found {
			return false, fmt.Errorf("malformed password hash parameters")
		}
		parsed, err := strconv.ParseUint(value, 10, 32)
		if err != nil {
			return false, fmt.Errorf("malformed password hash parameters: %w", err)
		}
		switch key {
		case "m":
			memory = parsed
		case "t":
			iterations = parsed
		case "p":
			threads = parsed
		default:
			return false, fmt.Errorf("unknown password hash parameter %q", key)
		}
	}

	if memory < uint64(DefaultArgonMemoryKiB) ||
		iterations < uint64(DefaultArgonTime) ||
		threads < uint64(DefaultArgonThreads) ||
		threads > 255 {
		return false, fmt.Errorf("password hash parameters are outside the supported policy")
	}

	salt, err := base64.RawStdEncoding.DecodeString(parts[4])
	if err != nil || len(salt) < 16 {
		return false, fmt.Errorf("malformed password hash salt")
	}
	expected, err := base64.RawStdEncoding.DecodeString(parts[5])
	if err != nil || len(expected) < 32 {
		return false, fmt.Errorf("malformed password hash value")
	}

	actual := argon2.IDKey(
		[]byte(password),
		salt,
		uint32(iterations),
		uint32(memory),
		uint8(threads),
		uint32(len(expected)))
	return subtle.ConstantTimeCompare(expected, actual) == 1, nil
}
