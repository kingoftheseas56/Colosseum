package account

import (
	"encoding/hex"
	"strings"
)

func IsUUID(value string) bool {
	value = strings.TrimSpace(strings.ToLower(value))
	if len(value) != 36 ||
		value[8] != '-' ||
		value[13] != '-' ||
		value[18] != '-' ||
		value[23] != '-' {
		return false
	}
	raw := strings.ReplaceAll(value, "-", "")
	if len(raw) != 32 {
		return false
	}
	_, err := hex.DecodeString(raw)
	return err == nil
}

func ValidateDeviceIdentity(installID, label, platform string) error {
	if !IsUUID(installID) {
		return ErrInvalidCredentials
	}
	label = strings.TrimSpace(label)
	platform = strings.TrimSpace(platform)
	if label == "" || len([]rune(label)) > 64 {
		return ErrInvalidCredentials
	}
	if platform == "" || len([]rune(platform)) > 32 {
		return ErrInvalidCredentials
	}
	return nil
}
