package account

import (
	"bytes"
	"crypto/sha256"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"math"
	"regexp"
	"strings"
	"unicode"
	"unicode/utf8"
)

var syncWindowsDrivePath = regexp.MustCompile(`^[A-Za-z]:[\\/].+`)
var syncRatingsReviewsKey = regexp.MustCompile(`^rr1:[0-9a-f]{64}$`)

var syncAllowedCategories = map[string]int{
	"collection":                      1,
	"continue_progress":               1,
	"full_history":                    1,
	"watch_state":                     1,
	"activity_fact":                   1,
	"explicit_content_preference":     1,
	"stremio_link":                    1,
	"desired_download_intent":         1,
	"ratings_reviews":                 1,
	"ratings_reviews_conversion_maps": 1,
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
	"password": {}, "recoverykey": {}, "accesstoken": {}, "authkey": {},
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
	return validateSyncPayloadForCategory("", raw)
}

func validateSyncPayloadForCategory(category string, raw json.RawMessage) error {
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
	return scanSyncPayload(value, 0, category, "$")
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

func ratingsReviewsRecordKey(world, kind, mediaID string) string {
	preimage := make([]byte, 0, len(world)+len(kind)+len(mediaID)+12)
	for _, value := range []string{world, kind, mediaID} {
		raw := []byte(value)
		var length [4]byte
		binary.BigEndian.PutUint32(length[:], uint32(len(raw)))
		preimage = append(preimage, length[:]...)
		preimage = append(preimage, raw...)
	}
	digest := sha256.Sum256(preimage)
	return fmt.Sprintf("rr1:%x", digest)
}

func validateRatingsReviewsKey(recordKey string) error {
	if !syncRatingsReviewsKey.MatchString(recordKey) {
		return fmt.Errorf("invalid_record_key")
	}
	return nil
}

func validateRatingsReviewsPayload(recordKey string, object map[string]any) error {
	if err := validateRatingsReviewsKey(recordKey); err != nil {
		return err
	}
	allowed := map[string]struct{}{
		"world": {}, "kind": {}, "media_id": {}, "rating": {}, "review": {},
		"spoiler": {}, "created_at_ms": {}, "updated_at_ms": {},
	}
	if len(object) != len(allowed) {
		return fmt.Errorf("payload_field_not_allowed")
	}
	for field := range object {
		if _, ok := allowed[field]; !ok {
			return fmt.Errorf("payload_field_not_allowed")
		}
	}
	world, worldOK := object["world"].(string)
	kind, kindOK := object["kind"].(string)
	mediaID, mediaOK := object["media_id"].(string)
	if !worldOK || world == "" || world != strings.TrimSpace(world) || world != strings.ToLower(world) ||
		!kindOK || kind == "" || kind != strings.TrimSpace(kind) || kind != strings.ToLower(kind) ||
		!mediaOK || mediaID == "" {
		return fmt.Errorf("record_identity_mismatch")
	}
	if ratingsReviewsRecordKey(world, kind, mediaID) != recordKey {
		return fmt.Errorf("record_identity_mismatch")
	}

	ratingPresent := object["rating"] != nil
	if ratingPresent {
		number, ok := object["rating"].(json.Number)
		if !ok {
			return fmt.Errorf("payload_invalid")
		}
		value, err := number.Float64()
		if err != nil || math.IsNaN(value) || math.IsInf(value, 0) ||
			value < 0 || value > 10 || math.Trunc(value*2) != value*2 {
			return fmt.Errorf("payload_invalid")
		}
	}

	reviewPresent := object["review"] != nil
	if reviewPresent {
		review, ok := object["review"].(string)
		if !ok || len([]byte(review)) > 16384 {
			return fmt.Errorf("payload_invalid")
		}
	}
	spoiler, ok := object["spoiler"].(bool)
	if !ok || (!reviewPresent && spoiler) || (!ratingPresent && !reviewPresent) {
		return fmt.Errorf("payload_invalid")
	}
	createdAt, err := syncIntegerField(object, "created_at_ms", true)
	if err != nil {
		return err
	}
	updatedAt, err := syncIntegerField(object, "updated_at_ms", true)
	if err != nil || updatedAt < createdAt {
		return fmt.Errorf("payload_invalid")
	}
	return nil
}

func validateRatingsReviewsConversionKey(recordKey string) (string, error) {
	parts := strings.Split(recordKey, "/")
	if len(parts) != 2 || parts[0] != "conversion" || parts[1] == "" {
		return "", fmt.Errorf("invalid_record_key")
	}
	providerID := parts[1]
	if providerID != strings.TrimSpace(providerID) ||
		providerID != strings.ToLower(providerID) ||
		strings.Contains(providerID, "\\") {
		return "", fmt.Errorf("invalid_record_key")
	}
	allowedProviders := map[string]struct{}{
		"mal": {}, "anilist": {}, "trakt": {}, "simkl": {},
		"imdb": {}, "tmdb": {}, "rotten_tomatoes": {}, "metacritic": {},
	}
	if _, ok := allowedProviders[providerID]; !ok {
		return "", fmt.Errorf("invalid_record_key")
	}
	return providerID, nil
}

func validateRatingsReviewsConversionPayload(recordKey string, object map[string]any) error {
	providerID, err := validateRatingsReviewsConversionKey(recordKey)
	if err != nil {
		return err
	}
	allowed := map[string]struct{}{
		"version": {}, "provider_id": {}, "domain_id": {}, "domain_version": {}, "outputs": {},
	}
	if len(object) != len(allowed) {
		return fmt.Errorf("payload_field_not_allowed")
	}
	for field := range object {
		if _, ok := allowed[field]; !ok {
			return fmt.Errorf("payload_field_not_allowed")
		}
	}
	version, err := syncIntegerField(object, "version", true)
	if err != nil || version != 1 {
		return fmt.Errorf("payload_invalid")
	}
	wireProvider, ok := object["provider_id"].(string)
	if !ok || wireProvider != providerID {
		return fmt.Errorf("record_identity_mismatch")
	}
	domainID, ok := object["domain_id"].(string)
	if !ok || domainID == "" || domainID != strings.TrimSpace(domainID) ||
		utf8.RuneCountInString(domainID) > 128 {
		return fmt.Errorf("payload_invalid")
	}
	if _, err := syncIntegerField(object, "domain_version", true); err != nil {
		return err
	}
	outputs, ok := object["outputs"].([]any)
	if !ok || len(outputs) != 21 {
		return fmt.Errorf("payload_invalid")
	}
	for _, output := range outputs {
		switch output.(type) {
		case nil, string, bool, json.Number:
		default:
			return fmt.Errorf("payload_invalid")
		}
	}
	return nil
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
		"source": {}, "displayId": {}, "displayTitle": {}, "latestKnownAt": {},
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
	hasStremioField := false
	for _, field := range []string{"source", "displayId", "displayTitle", "latestKnownAt"} {
		if _, present := object[field]; present {
			hasStremioField = true
			break
		}
	}
	if !hasStremioField {
		return nil
	}

	source, sourceOK := object["source"].(string)
	displayID, displayIDOK := object["displayId"].(string)
	displayTitle, displayTitleOK := object["displayTitle"].(string)
	if !sourceOK || source != "stremio" ||
		!displayIDOK || displayID == "" || displayID != strings.TrimSpace(displayID) ||
		utf8.RuneCountInString(displayID) > 512 || isSyncFilesystemPath(displayID) ||
		!displayTitleOK || displayTitle == "" || displayTitle != strings.TrimSpace(displayTitle) ||
		utf8.RuneCountInString(displayTitle) > 1024 || isSyncFilesystemPath(displayTitle) {
		return fmt.Errorf("payload_invalid")
	}
	latestKnownAt, latestErr := syncIntegerField(object, "latestKnownAt", true)
	if latestErr != nil || latestKnownAt < first || latestKnownAt > last {
		return fmt.Errorf("payload_invalid")
	}
	return nil
}

func validateActivityResetPayload(object map[string]any) error {
	if len(object) != 2 {
		return fmt.Errorf("payload_field_not_allowed")
	}
	generation, ok := syncIntegerNumber(object["resetGeneration"])
	if !ok || generation <= 0 {
		return fmt.Errorf("payload_invalid")
	}
	resetAt, ok := syncIntegerNumber(object["resetAtMs"])
	if !ok || resetAt <= 0 {
		return fmt.Errorf("payload_invalid")
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

func validateStremioLink(
	key string,
	object map[string]any,
) error {
	if key != "preferences/main-sync-provider" {
		return fmt.Errorf("invalid_record_key")
	}
	if len(object) != 1 {
		return fmt.Errorf("payload_field_not_allowed")
	}
	provider, ok := object["mainSyncProvider"].(string)
	if !ok || provider != "stremio" {
		return fmt.Errorf("payload_invalid")
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
		if category == "full_history" && recordKey == "history/reset" {
			return fmt.Errorf("invalid_operation")
		}
		return validateCategoryRecordKey(category, recordKey)
	}
	if operation != "put" {
		return fmt.Errorf("invalid_operation")
	}
	if err := validateSyncPayloadForCategory(category, payload); err != nil {
		return err
	}

	if category == "activity_fact" {
		object, err := decodeSyncObject(payload)
		if err != nil {
			return err
		}
		if recordKey == "activity/reset" {
			return validateActivityResetPayload(object)
		}
		return validateActivityPayloadObject(object)
	}

	object, err := decodeSyncObject(payload)
	if err != nil {
		return err
	}
	switch category {
	case "collection", "continue_progress", "full_history":
		if category == "full_history" && recordKey == "history/reset" {
			return validateActivityResetPayload(object)
		}
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
	case "watch_state":
		parts := strings.Split(recordKey, "/")
		if len(parts) != 3 || parts[0] != "watch" ||
			(parts[1] != "mark" && parts[1] != "season") {
			return fmt.Errorf("invalid_record_key")
		}
		value, err := decodeCanonicalSyncComponent(parts[2])
		if err != nil || strings.TrimSpace(value) == "" || isSyncFilesystemPath(value) {
			return fmt.Errorf("invalid_record_key")
		}
		if parts[1] == "mark" {
			if err := requiredSyncIdentity(object, "id", value); err != nil {
				return err
			}
			mark, ok := syncIntegerNumber(object["mark"])
			if !ok || (mark != -1 && mark != 1) || (len(object) != 2 && len(object) != 3 && len(object) != 4) {
				return fmt.Errorf("payload_invalid")
			}
			if rawActionAt, present := object["actionAtMs"]; present {
				actionToken, stringValue := rawActionAt.(string)
				if !stringValue {
					return fmt.Errorf("payload_invalid")
				}
				actionAt, valid := parseSyncIntegerToken(actionToken)
				if !valid || actionAt <= 0 {
					return fmt.Errorf("payload_invalid")
				}
			}
			if rawManual, present := object["manual"]; present {
				if _, ok := rawManual.(bool); !ok {
					return fmt.Errorf("payload_invalid")
				}
			}
		} else {
			if err := requiredSyncIdentity(object, "seriesId", value); err != nil {
				return err
			}
			season, ok := syncIntegerNumber(object["season"])
			if !ok || season <= 0 || len(object) != 2 {
				return fmt.Errorf("payload_invalid")
			}
		}
		return nil
	case "desired_download_intent":
		return validateDesiredDownloadIntent(recordKey, object)
	case "ratings_reviews":
		return validateRatingsReviewsPayload(recordKey, object)
	case "ratings_reviews_conversion_maps":
		return validateRatingsReviewsConversionPayload(recordKey, object)
	case "explicit_content_preference":
		return validateExplicitContentPreference(recordKey, object)
	case "stremio_link":
		return validateStremioLink(recordKey, object)
	default:
		return fmt.Errorf("category_not_supported")
	}
}

func validateCategoryRecordKey(category, recordKey string) error {
	switch category {
	case "collection", "continue_progress", "full_history":
		if category == "full_history" && recordKey == "history/reset" {
			return nil
		}
		_, _, err := validateCanonicalSyncKey(category, recordKey)
		return err
	case "ratings_reviews":
		return validateRatingsReviewsKey(recordKey)
	case "ratings_reviews_conversion_maps":
		_, err := validateRatingsReviewsConversionKey(recordKey)
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
	case "stremio_link":
		if recordKey != "preferences/main-sync-provider" {
			return fmt.Errorf("invalid_record_key")
		}
		return nil
	case "activity_fact":
		parts := strings.Split(recordKey, "/")
		if recordKey == "activity/reset" {
			return nil
		}
		if len(parts) != 2 || parts[0] != "activity" || !IsUUID(parts[1]) ||
			parts[1] != strings.ToLower(parts[1]) {
			return fmt.Errorf("invalid_record_key")
		}
		return nil
	case "watch_state":
		parts := strings.Split(recordKey, "/")
		if len(parts) != 3 || parts[0] != "watch" ||
			(parts[1] != "mark" && parts[1] != "season") {
			return fmt.Errorf("invalid_record_key")
		}
		value, err := decodeCanonicalSyncComponent(parts[2])
		if err != nil || strings.TrimSpace(value) == "" || isSyncFilesystemPath(value) {
			return fmt.Errorf("invalid_record_key")
		}
		return nil
	default:
		return fmt.Errorf("category_not_supported")
	}
}

func scanSyncPayload(value any, depth int, category, path string) error {
	if depth > 64 {
		return fmt.Errorf("payload_too_deep")
	}
	switch typed := value.(type) {
	case map[string]any:
		for key, child := range typed {
			if _, forbidden := syncForbiddenFields[normalizeSyncField(key)]; forbidden {
				return fmt.Errorf("forbidden_field")
			}
			childPath := path + "." + key
			if err := scanSyncPayload(child, depth+1, category, childPath); err != nil {
				return err
			}
		}
	case []any:
		for index, child := range typed {
			childPath := fmt.Sprintf("%s[%d]", path, index)
			if err := scanSyncPayload(child, depth+1, category, childPath); err != nil {
				return err
			}
		}
	case string:
		if category == "ratings_reviews" && path == "$.review" {
			return nil
		}
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
