package account

import (
	"context"
	"encoding/json"
	"strings"
)

type syncFeaturesKey struct{}

// WithSyncFeatures records the HTTP client's advertised capabilities. Internal
// service callers retain the full canonical view unless they select a client
// view explicitly. Every HTTP sync entry point must supply the request header.
func WithSyncFeatures(ctx context.Context, header string) context.Context {
	supportsNuvio := false
	for _, feature := range strings.Split(header, ",") {
		if strings.TrimSpace(feature) == "nuvio" {
			supportsNuvio = true
		}
	}
	return context.WithValue(ctx, syncFeaturesKey{}, supportsNuvio)
}

func legacyNuvioSync(ctx context.Context) bool {
	supportsNuvio, specified := ctx.Value(syncFeaturesKey{}).(bool)
	return specified && !supportsNuvio
}

// Projection changes only the response. Canonical payloads, journal identities,
// sequence values and snapshot pagination remain unchanged in storage.
func projectSyncForClient(ctx context.Context, view SyncMutationView) (SyncMutationView, error) {
	if !legacyNuvioSync(ctx) || view.Operation != "put" ||
		(view.Category != "full_history" && view.Category != "stremio_link") {
		return view, nil
	}
	object, err := decodeSyncObject(view.Payload)
	if err != nil {
		return SyncMutationView{}, err
	}
	if view.Category == "stremio_link" {
		if object["mainSyncProvider"] == "nuvio" {
			// v1.2.0 represents an empty provider by DELETE and rejects a PUT
			// containing either an empty string or an unknown provider.
			view.Operation = "delete"
			view.Payload = nil
			view.DeletedAtMS = ""
		}
		return view, nil
	}
	if object["source"] != "nuvio" {
		return view, nil
	}
	for _, field := range []string{"source", "displayId", "displayTitle", "latestKnownAt"} {
		delete(object, field)
	}
	view.Payload, err = json.Marshal(object)
	return view, err
}

func legacyProviderClear(current syncMergeCurrent, incoming parsedSyncMutation) bool {
	if !incoming.LegacyClient || incoming.Category != "stremio_link" ||
		incoming.RecordKey != "preferences/main-sync-provider" ||
		incoming.Operation != "delete" || current.Operation != "put" {
		return false
	}
	object, err := decodeSyncObject(current.Payload)
	return err == nil && object["mainSyncProvider"] == "nuvio"
}
