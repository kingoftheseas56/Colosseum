package account

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"strconv"
)

type syncMergeCurrent struct {
	MutationID    string
	DeviceID      string
	SchemaVersion int
	HLCPhysicalMS int64
	HLCCounter    uint64
	Operation     string
	Payload       json.RawMessage
}

type syncResolution struct {
	Changed             bool
	Operation           string
	Payload             json.RawMessage
	WinnerMutationID    string
	WinnerDeviceID      string
	WinnerSchemaVersion int
	WinnerHLCPhysicalMS int64
	WinnerHLCCounter    uint64
}

type syncHistoryPayload struct {
	object          map[string]any
	firstActivityAt int64
	lastActivityAt  int64
	completedAt     int64
}

func resolveMutableSync(
	current syncMergeCurrent,
	found bool,
	incoming parsedSyncMutation,
) (syncResolution, error) {
	if err := validateSyncMergeOperation(incoming.Operation); err != nil {
		return syncResolution{}, err
	}
	if found {
		if err := validateSyncMergeOperation(current.Operation); err != nil {
			return syncResolution{}, err
		}
	}

	if !found {
		return resolveIncomingWithoutCurrent(incoming)
	}
	if incoming.Category != "full_history" {
		return resolveSyncLWW(current, incoming)
	}
	return resolveHistorySync(current, incoming)
}

func validateSyncMergeOperation(operation string) error {
	switch operation {
	case "put", "delete":
		return nil
	default:
		return fmt.Errorf("unsupported sync merge operation %q", operation)
	}
}

func resolveIncomingWithoutCurrent(
	incoming parsedSyncMutation,
) (syncResolution, error) {
	payload := cloneSyncMergePayload(incoming.Payload)
	if incoming.Operation == "delete" {
		payload = nil
	} else if incoming.Category == "full_history" {
		history, err := decodeSyncHistoryPayload(incoming.Payload)
		if err != nil {
			return syncResolution{}, err
		}
		payload, err = canonicalSyncHistoryPayload(history)
		if err != nil {
			return syncResolution{}, err
		}
	}
	return syncResolutionFromIncoming(incoming, payload, true), nil
}

func resolveSyncLWW(
	current syncMergeCurrent,
	incoming parsedSyncMutation,
) (syncResolution, error) {
	if syncIncomingIsNewer(current, incoming) {
		payload := cloneSyncMergePayload(incoming.Payload)
		if incoming.Operation == "delete" {
			payload = nil
		}
		return syncResolutionFromIncoming(incoming, payload, true), nil
	}
	return syncResolutionFromCurrent(
		current,
		cloneSyncMergePayload(current.Payload),
		false), nil
}

func resolveHistorySync(
	current syncMergeCurrent,
	incoming parsedSyncMutation,
) (syncResolution, error) {
	if current.Operation == "delete" || incoming.Operation == "delete" {
		return resolveHistoryDeleteBarrier(current, incoming)
	}
	return mergeHistorySyncPuts(current, incoming)
}

func resolveHistoryDeleteBarrier(
	current syncMergeCurrent,
	incoming parsedSyncMutation,
) (syncResolution, error) {
	if syncIncomingIsNewer(current, incoming) {
		payload, err := canonicalizeSelectedHistory(
			incoming.Operation, incoming.Payload)
		if err != nil {
			return syncResolution{}, err
		}
		return syncResolutionFromIncoming(incoming, payload, true), nil
	}

	payload, err := canonicalizeSelectedHistory(
		current.Operation, current.Payload)
	if err != nil {
		return syncResolution{}, err
	}
	return syncResolutionFromCurrent(current, payload, false), nil
}

func canonicalizeSelectedHistory(
	operation string,
	payload json.RawMessage,
) (json.RawMessage, error) {
	if operation == "delete" {
		return nil, nil
	}
	history, err := decodeSyncHistoryPayload(payload)
	if err != nil {
		return nil, err
	}
	return canonicalSyncHistoryPayload(history)
}

func mergeHistorySyncPuts(
	current syncMergeCurrent,
	incoming parsedSyncMutation,
) (syncResolution, error) {
	currentHistory, err := decodeSyncHistoryPayload(current.Payload)
	if err != nil {
		return syncResolution{}, fmt.Errorf("decode current History payload: %w", err)
	}
	incomingHistory, err := decodeSyncHistoryPayload(incoming.Payload)
	if err != nil {
		return syncResolution{}, fmt.Errorf("decode incoming History payload: %w", err)
	}

	incomingNewer := syncIncomingIsNewer(current, incoming)
	winner := currentHistory
	if incomingNewer {
		winner = incomingHistory
	}
	merged := syncHistoryPayload{
		object:          cloneSyncHistoryObject(winner.object),
		firstActivityAt: minPositiveSyncTimestamp(currentHistory.firstActivityAt, incomingHistory.firstActivityAt),
		lastActivityAt:  maxSyncTimestamp(currentHistory.lastActivityAt, incomingHistory.lastActivityAt),
		completedAt:     minPositiveSyncTimestamp(currentHistory.completedAt, incomingHistory.completedAt),
	}
	mergedPayload, err := canonicalSyncHistoryPayload(merged)
	if err != nil {
		return syncResolution{}, err
	}
	currentCanonical, err := canonicalSyncHistoryPayload(currentHistory)
	if err != nil {
		return syncResolution{}, fmt.Errorf("canonicalize current History payload: %w", err)
	}
	changed := incomingNewer || !bytes.Equal(mergedPayload, currentCanonical)

	if incomingNewer {
		return syncResolutionFromIncoming(
			incoming, mergedPayload, changed), nil
	}
	return syncResolutionFromCurrent(
		current, mergedPayload, changed), nil
}

func syncIncomingIsNewer(
	current syncMergeCurrent,
	incoming parsedSyncMutation,
) bool {
	return compareServerHLC(
		incoming.HLCPhysicalMS,
		incoming.HLCCounter,
		incoming.DeviceID,
		current.HLCPhysicalMS,
		current.HLCCounter,
		current.DeviceID) > 0
}

func syncResolutionFromCurrent(
	current syncMergeCurrent,
	payload json.RawMessage,
	changed bool,
) syncResolution {
	return syncResolution{
		Changed:             changed,
		Operation:           current.Operation,
		Payload:             payload,
		WinnerMutationID:    current.MutationID,
		WinnerDeviceID:      current.DeviceID,
		WinnerSchemaVersion: current.SchemaVersion,
		WinnerHLCPhysicalMS: current.HLCPhysicalMS,
		WinnerHLCCounter:    current.HLCCounter,
	}
}

func syncResolutionFromIncoming(
	incoming parsedSyncMutation,
	payload json.RawMessage,
	changed bool,
) syncResolution {
	return syncResolution{
		Changed:             changed,
		Operation:           incoming.Operation,
		Payload:             payload,
		WinnerMutationID:    incoming.MutationID,
		WinnerDeviceID:      incoming.DeviceID,
		WinnerSchemaVersion: incoming.SchemaVersion,
		WinnerHLCPhysicalMS: incoming.HLCPhysicalMS,
		WinnerHLCCounter:    incoming.HLCCounter,
	}
}

func decodeSyncHistoryPayload(
	raw json.RawMessage,
) (syncHistoryPayload, error) {
	decoder := json.NewDecoder(bytes.NewReader(raw))
	decoder.UseNumber()

	var value any
	if err := decoder.Decode(&value); err != nil {
		return syncHistoryPayload{}, fmt.Errorf("History payload is invalid JSON: %w", err)
	}
	if err := ensureSyncJSONEOF(decoder); err != nil {
		return syncHistoryPayload{}, err
	}
	object, ok := value.(map[string]any)
	if !ok || object == nil {
		return syncHistoryPayload{}, fmt.Errorf("History payload must be a JSON object")
	}

	first, err := syncHistoryTimestamp(object, "firstActivityAt", true)
	if err != nil {
		return syncHistoryPayload{}, err
	}
	last, err := syncHistoryTimestamp(object, "lastActivityAt", true)
	if err != nil {
		return syncHistoryPayload{}, err
	}
	completed, err := syncHistoryTimestamp(object, "completedAt", false)
	if err != nil {
		return syncHistoryPayload{}, err
	}
	if last < first {
		return syncHistoryPayload{}, fmt.Errorf("History payload lastActivityAt precedes firstActivityAt")
	}
	if completed > 0 && (completed < first || completed > last) {
		return syncHistoryPayload{}, fmt.Errorf("History payload completedAt falls outside the activity interval")
	}
	return syncHistoryPayload{
		object:          object,
		firstActivityAt: first,
		lastActivityAt:  last,
		completedAt:     completed,
	}, nil
}

func ensureSyncJSONEOF(decoder *json.Decoder) error {
	var extra any
	if err := decoder.Decode(&extra); err != io.EOF {
		if err == nil {
			return fmt.Errorf("History payload contains trailing JSON")
		}
		return fmt.Errorf("History payload has invalid trailing data: %w", err)
	}
	return nil
}

func syncHistoryTimestamp(
	object map[string]any,
	field string,
	required bool,
) (int64, error) {
	value, exists := object[field]
	if !exists {
		if required {
			return 0, fmt.Errorf("History payload requires numeric %s", field)
		}
		return 0, nil
	}
	if value == nil {
		return 0, fmt.Errorf("History payload %s must be a positive integer", field)
	}
	number, ok := value.(json.Number)
	if !ok {
		return 0, fmt.Errorf("History payload %s must be an integer", field)
	}
	parsed, err := number.Int64()
	if err != nil {
		return 0, fmt.Errorf("History payload %s must be an int64: %w", field, err)
	}
	if parsed <= 0 {
		return 0, fmt.Errorf("History payload %s must be positive", field)
	}
	return parsed, nil
}

func canonicalSyncHistoryPayload(
	history syncHistoryPayload,
) (json.RawMessage, error) {
	if history.firstActivityAt <= 0 {
		return nil, fmt.Errorf("History payload has no positive firstActivityAt")
	}
	if history.lastActivityAt <= 0 {
		return nil, fmt.Errorf("History payload has no positive lastActivityAt")
	}
	object := cloneSyncHistoryObject(history.object)
	object["firstActivityAt"] = json.Number(strconv.FormatInt(history.firstActivityAt, 10))
	object["lastActivityAt"] = json.Number(strconv.FormatInt(history.lastActivityAt, 10))
	if history.completedAt > 0 {
		object["completedAt"] = json.Number(strconv.FormatInt(history.completedAt, 10))
	} else {
		delete(object, "completedAt")
	}

	encoded, err := json.Marshal(object)
	if err != nil {
		return nil, fmt.Errorf("encode canonical History payload: %w", err)
	}
	return json.RawMessage(encoded), nil
}

func minPositiveSyncTimestamp(left, right int64) int64 {
	if left <= 0 {
		return right
	}
	if right <= 0 {
		return left
	}
	if left < right {
		return left
	}
	return right
}

func maxSyncTimestamp(left, right int64) int64 {
	if left > right {
		return left
	}
	return right
}

func cloneSyncHistoryObject(source map[string]any) map[string]any {
	clone := make(map[string]any, len(source))
	for key, value := range source {
		clone[key] = value
	}
	return clone
}

func cloneSyncMergePayload(payload json.RawMessage) json.RawMessage {
	if len(payload) == 0 {
		return nil
	}
	clone := make([]byte, len(payload))
	copy(clone, payload)
	return json.RawMessage(clone)
}
