package httpserver

import (
	"errors"
	"net/http"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/account"
)

// Profile attachment lifecycle endpoints (Arc 36 N-12). The wire shapes are
// the frozen Wave 4 contract: begin/get/commit return the attachment id,
// device id, baseline server seq, and state; the fixed-cursor snapshot lives
// under GET /v1/sync/snapshot and carries the opaque after_key token.

func (h *Handler) beginProfileAttachment(w http.ResponseWriter, r *http.Request) {
	var input account.BeginProfileAttachmentInput
	if err := decodeJSON(w, r, &input); err != nil {
		WriteAPIError(w, http.StatusBadRequest, "invalid_request", "The request body is invalid.")
		return
	}

	attachment, err := h.accounts.BeginProfileAttachment(
		r.Context(),
		authenticated(r),
		input)
	if err != nil {
		h.writeProfileAttachmentError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, attachment)
}

func (h *Handler) getProfileAttachment(w http.ResponseWriter, r *http.Request) {
	attachment, err := h.accounts.GetProfileAttachment(
		r.Context(),
		authenticated(r),
		r.PathValue("attachmentID"))
	if err != nil {
		h.writeProfileAttachmentError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, attachment)
}

func (h *Handler) commitProfileAttachment(w http.ResponseWriter, r *http.Request) {
	attachment, err := h.accounts.CommitProfileAttachment(
		r.Context(),
		authenticated(r),
		r.PathValue("attachmentID"))
	if err != nil {
		h.writeProfileAttachmentError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, attachment)
}

// writeProfileAttachmentError maps attachment sentinel errors onto stable
// API codes and falls back to the shared account error mapping.
func (h *Handler) writeProfileAttachmentError(w http.ResponseWriter, err error) {
	if !writeAttachmentAPIError(w, err) {
		h.writeAccountError(w, err)
	}
}

// writeAttachmentAPIError reports whether err was an attachment sentinel
// that has already been written to the response.
func writeAttachmentAPIError(w http.ResponseWriter, err error) bool {
	switch {
	case errors.Is(err, account.ErrAttachmentInvalid):
		WriteAPIError(w,
			http.StatusBadRequest,
			"invalid_attachment",
			"That profile attachment request is invalid.")
	case errors.Is(err, account.ErrAttachmentNotFound):
		WriteAPIError(w,
			http.StatusNotFound,
			"attachment_not_found",
			"That profile attachment was not found.")
	case errors.Is(err, account.ErrAttachmentConflict):
		WriteAPIError(w,
			http.StatusConflict,
			"attachment_conflict",
			"That profile attachment conflicts with an existing attachment.")
	case errors.Is(err, account.ErrAttachmentNotActive):
		WriteAPIError(w,
			http.StatusConflict,
			"attachment_not_active",
			"That profile attachment is no longer accepting changes.")
	default:
		return false
	}
	return true
}
