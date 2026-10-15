package httpserver

import (
	"context"
	"net/http"
	"strings"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/account"
)

type authContextKey struct{}

func (h *Handler) requireAuth(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		header := strings.TrimSpace(r.Header.Get("Authorization"))
		scheme, token, found := strings.Cut(header, " ")
		if !found || !strings.EqualFold(scheme, "Bearer") || strings.TrimSpace(token) == "" {
			writeServiceError(w, account.ErrSessionInvalid)
			return
		}

		auth, err := h.accounts.AuthenticateAccessToken(r.Context(), strings.TrimSpace(token))
		if err != nil {
			writeServiceError(w, err)
			return
		}
		ctx := context.WithValue(r.Context(), authContextKey{}, auth)
		next.ServeHTTP(w, r.WithContext(ctx))
	})
}

func authenticated(r *http.Request) account.AuthenticatedSession {
	value, _ := r.Context().Value(authContextKey{}).(account.AuthenticatedSession)
	return value
}
