package httpserver

import (
	"errors"
	"net/http"
	"strconv"
	"strings"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/account"
)

// syncPushRequest is the decoded SyncPushEnvelope: an optional envelope-level
// attachment_id plus the unchanged mutation array. Ordinary pushes omit the
// attachment field entirely.
type syncPushRequest struct {
	AttachmentID string                      `json:"attachment_id,omitempty"`
	Mutations    []account.SyncMutationInput `json:"mutations"`
}

func (h *Handler) pushSync(w http.ResponseWriter, r *http.Request) {
	var request syncPushRequest
	if err := decodeJSON(w, r, &request); err != nil {
		WriteAPIError(w, http.StatusBadRequest, "invalid_request", "The request body is invalid.")
		return
	}
	if len(request.Mutations) == 0 || len(request.Mutations) > 100 {
		WriteAPIError(w, http.StatusBadRequest, "invalid_request", "A sync push must contain between 1 and 100 mutations.")
		return
	}

	var result account.SyncPushResponse
	var err error
	if attachmentID := strings.TrimSpace(request.AttachmentID); attachmentID != "" {
		result, err = h.accounts.PushSyncWithAttachment(
			r.Context(),
			authenticated(r),
			attachmentID,
			request.Mutations)
	} else {
		result, err = h.accounts.PushSync(
			r.Context(),
			authenticated(r),
			request.Mutations)
	}
	if err != nil {
		if !writeAttachmentAPIError(w, err) {
			h.writeAccountError(w, err)
		}
		return
	}

	writeJSON(w, http.StatusOK, result)
}

func (h *Handler) pullSync(w http.ResponseWriter, r *http.Request) {
	raw := strings.TrimSpace(r.URL.Query().Get("after"))
	if raw == "" {
		raw = "0"
	}

	after, err := strconv.ParseUint(raw, 10, 64)
	if err != nil {
		WriteAPIError(w, http.StatusBadRequest, "invalid_cursor", "The sync cursor is invalid.")
		return
	}

	result, err := h.accounts.PullSync(
		r.Context(),
		authenticated(r),
		after)
	if err != nil {
		h.writeAccountError(w, err)
		return
	}

	writeJSON(w, http.StatusOK, result)
}

// pullSyncSnapshot serves the fixed-cursor canonical snapshot. The first
// page carries no after_key; later pages replay the opaque token returned by
// the previous page.
func (h *Handler) pullSyncSnapshot(w http.ResponseWriter, r *http.Request) {
	pageToken := strings.TrimSpace(r.URL.Query().Get("after_key"))

	result, err := h.accounts.SnapshotSync(
		r.Context(),
		authenticated(r),
		pageToken)
	if err != nil {
		if errors.Is(err, account.ErrInvalidPageToken) {
			WriteAPIError(w, http.StatusBadRequest, "invalid_page_token", "The snapshot page token is invalid.")
			return
		}
		h.writeAccountError(w, err)
		return
	}

	writeJSON(w, http.StatusOK, result)
}
