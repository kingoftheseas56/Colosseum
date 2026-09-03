package httpserver

import (
	"net/http"
	"strconv"
	"strings"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/account"
)

type syncPushRequest struct {
	Mutations []account.SyncMutationInput `json:"mutations"`
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

	result, err := h.accounts.PushSync(
		r.Context(),
		authenticated(r),
		request.Mutations)
	if err != nil {
		h.writeAccountError(w, err)
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
