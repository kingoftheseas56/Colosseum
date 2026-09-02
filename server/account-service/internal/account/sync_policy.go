package account

import (
	"bytes"
	"encoding/json"
	"fmt"
	"regexp"
	"strings"
	"unicode"
)

var syncWindowsDrivePath = regexp.MustCompile(`^[A-Za-z]:[\\/].+`)

var syncAllowedCategories = map[string]int{
	"collection":                  1,
	"continue_progress":           1,
	"full_history":                1,
	"explicit_content_preference": 1,
	"theatre_track_preferences":   1,
	"theatre_row_customization":   1,
	"extension_roster":            1,
}

var syncForbiddenFields = map[string]struct{}{
	"path": {}, "filepath": {}, "localpath": {}, "absolutepath": {},
	"outputpath": {}, "partpath": {}, "defaultdownloaddir": {},
	"vaultdir": {}, "oldpath": {}, "newpath": {}, "rootpath": {},
	"mediapath": {}, "downloadpath": {}, "sourcepath": {},
	"mediablob": {}, "fileblob": {}, "blob": {}, "filebytes": {},
	"rawbytes": {}, "contentbytes": {},
	"searchhistory": {}, "savedstate": {}, "sessionstate": {},
	"windowstate": {}, "windowgeometry": {}, "pipstate": {},
	"caststate": {}, "roomstate": {},
	"password": {}, "recoverykey": {}, "accesstoken": {},
	"refreshtoken": {}, "authorization": {}, "cookie": {},
	"cookies": {}, "apikey": {}, "clientsecret": {}, "secret": {},
	"credential": {}, "credentials": {},
	"transporturl": {}, "downloadurl": {}, "streamurl": {}, "feedurl": {},
}

func validateSyncCategory(category string, schemaVersion int) error {
	if category == "" || category != strings.ToLower(strings.TrimSpace(category)) {
		return fmt.Errorf("noncanonical_category")
	}
	allowedVersion, ok := syncAllowedCategories[category]
	if !ok {
		return fmt.Errorf("category_not_allowed")
	}
	if schemaVersion != allowedVersion {
		return fmt.Errorf("unsupported_schema_version")
	}
	return nil
}

func validateSyncRecordKey(recordKey string) error {
	if recordKey == "" || len(recordKey) > 512 || recordKey != strings.TrimSpace(recordKey) ||
		strings.HasPrefix(recordKey, "/") || strings.HasPrefix(recordKey, `\`) {
		return fmt.Errorf("invalid_record_key")
	}
	for _, segment := range strings.Split(recordKey, "/") {
		if segment == "" || segment == "." || segment == ".." {
			return fmt.Errorf("invalid_record_key")
		}
		for _, r := range segment {
			if r < 0x20 || r == 0x7f || r == '\\' {
				return fmt.Errorf("invalid_record_key")
			}
		}
	}
	return nil
}

func validateSyncPayload(raw json.RawMessage) error {
	if len(raw) == 0 {
		return fmt.Errorf("payload_required")
	}
	var value any
	decoder := json.NewDecoder(bytes.NewReader(raw))
	decoder.UseNumber()
	if err := decoder.Decode(&value); err != nil {
		return fmt.Errorf("payload_invalid")
	}
	return scanSyncPayload(value, 0)
}

func scanSyncPayload(value any, depth int) error {
	if depth > 64 {
		return fmt.Errorf("payload_too_deep")
	}
	switch typed := value.(type) {
	case map[string]any:
		for key, child := range typed {
			if _, forbidden := syncForbiddenFields[normalizeSyncField(key)]; forbidden {
				return fmt.Errorf("forbidden_field")
			}
			if err := scanSyncPayload(child, depth+1); err != nil {
				return err
			}
		}
	case []any:
		for _, child := range typed {
			if err := scanSyncPayload(child, depth+1); err != nil {
				return err
			}
		}
	case string:
		if isSyncFilesystemPath(typed) {
			return fmt.Errorf("filesystem_path_value")
		}
	}
	return nil
}

func normalizeSyncField(value string) string {
	var builder strings.Builder
	for _, r := range value {
		if unicode.IsLetter(r) || unicode.IsDigit(r) {
			builder.WriteRune(unicode.ToLower(r))
		}
	}
	return builder.String()
}

func isSyncFilesystemPath(value string) bool {
	trimmed := strings.TrimSpace(value)
	if trimmed == "" {
		return false
	}
	lower := strings.ToLower(trimmed)
	return strings.HasPrefix(lower, "file:/") ||
		strings.HasPrefix(lower, "qrc:/") ||
		strings.HasPrefix(trimmed, `\\`) ||
		strings.HasPrefix(trimmed, "../") ||
		strings.HasPrefix(trimmed, "./") ||
		strings.HasPrefix(trimmed, `..\`) ||
		strings.HasPrefix(trimmed, `.\`) ||
		strings.HasPrefix(trimmed, "/") ||
		syncWindowsDrivePath.MatchString(trimmed)
}
