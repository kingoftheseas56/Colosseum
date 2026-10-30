package httpserver

import (
	"encoding/json"
	"errors"
	"io"
	"net/http"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/account"
)

func decodeCollectionJSON(w http.ResponseWriter, r *http.Request, dest any) bool {
	r.Body = http.MaxBytesReader(w, r.Body, account.CollectionMaxBodyBytes)
	d := json.NewDecoder(r.Body)
	d.DisallowUnknownFields()
	err := d.Decode(dest)
	if err == nil {
		var extra any
		if next := d.Decode(&extra); !errors.Is(next, io.EOF) {
			err = errors.New("trailing content")
		}
	}
	if err != nil {
		var tooBig *http.MaxBytesError
		if errors.As(err, &tooBig) {
			WriteAPIError(w, http.StatusRequestEntityTooLarge, "publication_too_large", "This publication exceeds the size limit.")
		} else {
			WriteAPIError(w, http.StatusBadRequest, "publication_invalid", "The publication request is invalid.")
		}
		return false
	}
	return true
}
func writeCollectionError(w http.ResponseWriter, err error) {
	switch {
	case errors.Is(err, account.ErrCollectionInvalid):
		WriteAPIError(w, 400, "publication_invalid", "The publication contains unsupported or oversized data.")
	case errors.Is(err, account.ErrCollectionNotFound):
		WriteAPIError(w, 404, "publication_not_found", "This link is unavailable or was revoked.")
	case errors.Is(err, account.ErrCollectionConflict):
		WriteAPIError(w, 409, "revision_conflict", "The publication changed. Refresh before trying again.")
	case errors.Is(err, account.ErrCollectionImmutable):
		WriteAPIError(w, 409, "gift_immutable", "Sent gift editions cannot change. Publish a new gift instead.")
	case errors.Is(err, account.ErrCollectionIdempotency):
		WriteAPIError(w, 409, "idempotency_conflict", "This operation ID was already used with different content.")
	case errors.Is(err, account.ErrCollectionPassphraseRequired):
		WriteAPIError(w, 403, "passphrase_required", "Enter the creator's passphrase to open this link.")
	case errors.Is(err, account.ErrCollectionPassphrase):
		WriteAPIError(w, 403, "passphrase_invalid", "That passphrase was not accepted.")
	case errors.Is(err, account.ErrCollectionLimit):
		WriteAPIError(w, 409, "publication_limit", "The publication or operation limit has been reached.")
	default:
		writeServiceError(w, err)
	}
}
func (h *Handler) publishCollection(w http.ResponseWriter, r *http.Request) {
	var in account.PublishCollectionInput
	if !decodeCollectionJSON(w, r, &in) {
		return
	}
	p, err := h.accounts.PublishCollection(r.Context(), authenticated(r).Account.ID, in)
	if err != nil {
		writeCollectionError(w, err)
		return
	}
	writeJSON(w, 201, p)
}
func (h *Handler) updateCollectionPublication(w http.ResponseWriter, r *http.Request) {
	var in account.UpdateCollectionInput
	if !decodeCollectionJSON(w, r, &in) {
		return
	}
	p, err := h.accounts.UpdateCollectionPublication(r.Context(), authenticated(r).Account.ID, r.PathValue("shareID"), in)
	if err != nil {
		writeCollectionError(w, err)
		return
	}
	writeJSON(w, 200, p)
}
func (h *Handler) revokeCollectionPublication(w http.ResponseWriter, r *http.Request) {
	var in account.RevokeCollectionInput
	if !decodeCollectionJSON(w, r, &in) {
		return
	}
	p, err := h.accounts.RevokeCollectionPublication(r.Context(), authenticated(r).Account.ID, r.PathValue("shareID"), in)
	if err != nil {
		writeCollectionError(w, err)
		return
	}
	writeJSON(w, 200, p)
}
func (h *Handler) listCollectionPublications(w http.ResponseWriter, r *http.Request) {
	p, err := h.accounts.ListCollectionPublications(r.Context(), authenticated(r).Account.ID)
	if err != nil {
		writeCollectionError(w, err)
		return
	}
	writeJSON(w, 200, map[string]any{"publications": p})
}
func (h *Handler) openCollectionPublication(w http.ResponseWriter, r *http.Request) {
	var in struct {
		Passphrase string `json:"passphrase"`
	}
	if r.Method == http.MethodPost && !decodeCollectionJSON(w, r, &in) {
		return
	}
	p, err := h.accounts.OpenCollectionPublication(r.Context(), r.PathValue("shareID"), in.Passphrase, clientNetworkKey(r))
	if err != nil {
		writeCollectionError(w, err)
		return
	}
	writeJSON(w, 200, p)
}
