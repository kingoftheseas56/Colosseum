package account

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"regexp"
	"strings"
	"unicode"
	"unicode/utf8"
)

var syncWindowsDrivePath = regexp.MustCompile(`^[A-Za-z]:[\\/].+`)

var syncAllowedCategories = map[string]int{
	"collection":                  1,
	"continue_progress":           1,
	"full_history":                1,
	"activity_fact":               1,
	"explicit_content_preference": 1,
	"desired_download_intent":     1,
}

// These categories are present in older server feeds, but have no confirmed
// native owner/export seam in this client generation. Keep them explicit so a
// legacy record is rejected with an actionable capability error instead of
// being treated as an ordinary, silently ignored mutation.
var syncUnsupportedCategories = map[string]struct{}{
	"theatre_track_preferences": {},
	"theatre_row_customization": {},
	"extension_roster":          {},
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
		if _, unsupported := syncUnsupportedCategories[category]; unsupported {
			return fmt.Errorf("category_not_supported")
		}
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
	var extra any
	if err := decoder.Decode(&extra); err != io.EOF {
		return fmt.Errorf("payload_invalid")
	}
	return scanSyncPayload(value, 0)
}

func decodeCanonicalSyncComponent(encoded string) (string, error) {
	if encoded == "" {
		return "", fmt.Errorf("invalid_record_key")
	}
	decoded, err := base64.RawURLEncoding.DecodeString(encoded)
	if err != nil || len(decoded) == 0 || !utf8.Valid(decoded) ||
		base64.RawURLEncoding.EncodeToString(decoded) != encoded {
		return "", fmt.Errorf("invalid_record_key")
	}
	return string(decoded), nil
}

func validateCanonicalSyncKey(
	category, recordKey string,
) (string, string, error) {
	parts := strings.Split(recordKey, "/")
	if len(parts) != 3 {
		return "", "", fmt.Errorf("invalid_record_key")
	}
	prefix := map[string]string{
		"collection":        "collection",
		"continue_progress": "progress",
		"full_history":      "history",
	}[category]
	if prefix == "" || parts[0] != prefix {
		return "", "", fmt.Errorf("invalid_record_key")
	}
	left, err := decodeCanonicalSyncComponent(parts[1])
	if err != nil {
		return "", "", err
	}
	right, err := decodeCanonicalSyncComponent(parts[2])
	if err != nil {
		return "", "", err
	}
	return left, right, nil
}

func decodeSyncObject(raw json.RawMessage) (map[string]any, error) {
	var object map[string]any
	decoder := json.NewDecoder(bytes.NewReader(raw))
	decoder.UseNumber()
	if err := decoder.Decode(&object); err != nil || object == nil {
		return nil, fmt.Errorf("payload_invalid")
	}
	var extra any
	if err := decoder.Decode(&extra); err != io.EOF {
		return nil, fmt.Errorf("payload_invalid")
	}
	return object, nil
}

func requiredSyncIdentity(
	object map[string]any,
	field, expected string,
) error {
	value, ok := object[field].(string)
	if !ok || strings.TrimSpace(value) == "" || value != expected {
		return fmt.Errorf("record_identity_mismatch")
	}
	return nil
}

func validateDesiredDownloadIntent(
	key string,
	object map[string]any,
) error {
	parts := strings.Split(key, "/")
	if len(parts) != 2 || parts[0] == "" || parts[1] == "" {
		return fmt.Errorf("invalid_record_key")
	}
	if strings.ContainsAny(parts[0], `\\`) || strings.ContainsAny(parts[1], `\\`) {
		return fmt.Errorf("invalid_record_key")
	}
	if err := requiredSyncIdentity(object, "world", parts[0]); err != nil {
		return err
	}
	if err := requiredSyncIdentity(object, "id", parts[1]); err != nil {
		return err
	}
	kind, ok := object["kind"].(string)
	if !ok || strings.TrimSpace(kind) == "" || strings.ContainsAny(kind, `/\\`) {
		return fmt.Errorf("record_identity_mismatch")
	}

	allowed := map[string]struct{}{
		"id": {}, "world": {}, "kind": {}, "title": {}, "subtitle": {},
		"seriesTitle": {}, "season": {}, "episode": {}, "seriesId": {},
		"label": {}, "author": {},
	}
	for field := range object {
		if _, ok := allowed[field]; !ok {
			return fmt.Errorf("payload_field_not_allowed")
		}
	}
	return nil
}

func syncIntegerField(
	object map[string]any,
	field string,
	positive bool,
) (int64, error) {
	parsed, ok := syncIntegerNumber(object[field])
	if !ok || (positive && parsed <= 0) {
		return 0, fmt.Errorf("payload_invalid")
	}
	return parsed, nil
}

func validateFullHistory(
	object map[string]any,
	left, right string,
) error {
	if err := requiredSyncIdentity(object, "kind", left); err != nil {
		return err
	}
	if err := requiredSyncIdentity(object, "id", right); err != nil {
		return err
	}
	allowed := map[string]struct{}{
		"kind": {}, "id": {}, "firstActivityAt": {}, "lastActivityAt": {}, "completedAt": {},
	}
	for field := range object {
		if _, ok := allowed[field]; !ok {
			return fmt.Errorf("payload_field_not_allowed")
		}
	}
	first, err := syncIntegerField(object, "firstActivityAt", true)
	if err != nil {
		return err
	}
	last, err := syncIntegerField(object, "lastActivityAt", true)
	if err != nil || last < first {
		return fmt.Errorf("payload_invalid")
	}
	if completedValue, present := object["completedAt"]; present {
		completedObject := map[string]any{"completedAt": completedValue}
		completed, completedErr := syncIntegerField(completedObject, "completedAt", true)
		if completedErr != nil || completed < first || completed > last {
			return fmt.Errorf("payload_invalid")
		}
	}
	return nil
}

func validateExplicitContentPreference(
	key string,
	object map[string]any,
) error {
	if err := validateExplicitContentPreferenceKey(key); err != nil {
		return err
	}
	if len(object) != 1 {
		return fmt.Errorf("payload_field_not_allowed")
	}
	value, ok := object["showExplicit"].(bool)
	if !ok {
		return fmt.Errorf("payload_invalid")
	}
	_ = value
	return nil
}

func validateExplicitContentPreferenceKey(key string) error {
	if key != "preferences/explicit-content" {
		return fmt.Errorf("invalid_record_key")
	}
	return nil
}

// validateSyncRecordShape is the server admission seam shared by mutation
// validation and focused contract tests. It mirrors the key and materialized
// payload contracts of the shipping native adapters while retaining the
// generic firewall for portable fields.
func validateSyncRecordShape(
	category string,
	schemaVersion int,
	recordKey string,
	operation string,
	payload json.RawMessage,
) error {
	if err := validateSyncCategory(category, schemaVersion); err != nil {
		return err
	}
	if err := validateSyncRecordKey(recordKey); err != nil {
		return err
	}
	operation = strings.ToLower(strings.TrimSpace(operation))
	if operation == "delete" {
		return validateCategoryRecordKey(category, recordKey)
	}
	if operation != "put" {
		return fmt.Errorf("invalid_operation")
	}
	if err := validateSyncPayload(payload); err != nil {
		return err
	}

	if category == "activity_fact" {
		object, err := decodeSyncObject(payload)
		if err != nil {
			return err
		}
		return validateActivityPayloadObject(object)
	}

	object, err := decodeSyncObject(payload)
	if err != nil {
		return err
	}
	switch category {
	case "collection", "continue_progress", "full_history":
		left, right, err := validateCanonicalSyncKey(category, recordKey)
		if err != nil {
			return err
		}
		leftField, rightField := "world", "id"
		if category != "collection" {
			leftField = "kind"
		}
		if err := requiredSyncIdentity(object, leftField, left); err != nil {
			return err
		}
		if err := requiredSyncIdentity(object, rightField, right); err != nil {
			return err
		}
		if category == "full_history" {
			return validateFullHistory(object, left, right)
		}
		return nil
	case "desired_download_intent":
		return validateDesiredDownloadIntent(recordKey, object)
	case "explicit_content_preference":
		return validateExplicitContentPreference(recordKey, object)
	default:
		return fmt.Errorf("category_not_supported")
	}
}

func validateCategoryRecordKey(category, recordKey string) error {
	switch category {
	case "collection", "continue_progress", "full_history":
		_, _, err := validateCanonicalSyncKey(category, recordKey)
		return err
	case "desired_download_intent":
		parts := strings.Split(recordKey, "/")
		if len(parts) != 2 || parts[0] == "" || parts[1] == "" ||
			strings.ContainsAny(parts[0], `\\`) || strings.ContainsAny(parts[1], `\\`) {
			return fmt.Errorf("invalid_record_key")
		}
		return nil
	case "explicit_content_preference":
		return validateExplicitContentPreferenceKey(recordKey)
	case "activity_fact":
		parts := strings.Split(recordKey, "/")
		if len(parts) != 2 || parts[0] != "activity" || !IsUUID(parts[1]) ||
			parts[1] != strings.ToLower(parts[1]) {
			return fmt.Errorf("invalid_record_key")
		}
		return nil
	default:
		return fmt.Errorf("category_not_supported")
	}
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
