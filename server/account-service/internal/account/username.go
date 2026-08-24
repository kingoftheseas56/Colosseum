package account

import (
	"regexp"
	"strings"
)

var usernamePattern = regexp.MustCompile(`^[A-Za-z0-9](?:[A-Za-z0-9_]{1,22}[A-Za-z0-9])?$`)

var reservedUsernames = map[string]struct{}{
	"admin":         {},
	"administrator": {},
	"api":           {},
	"auth":          {},
	"colosseum":     {},
	"help":          {},
	"moderator":     {},
	"root":          {},
	"security":      {},
	"support":       {},
	"system":        {},
	"www":           {},
}

func NormalizeUsername(username string) (display string, canonical string, err error) {
	display = strings.TrimSpace(username)
	if len(display) < 3 || len(display) > 24 || !usernamePattern.MatchString(display) {
		return "", "", ErrInvalidUsername
	}

	canonical = strings.ToLower(display)
	if _, reserved := reservedUsernames[canonical]; reserved {
		return "", "", ErrUsernameUnavailable
	}
	return display, canonical, nil
}
