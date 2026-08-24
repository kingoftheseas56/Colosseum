package httpserver

import (
	"errors"
	"io"
	"net/http"
	"strings"
	"time"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/account"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/avatar"
)

type createAccountRequest struct {
	Username        string `json:"username"`
	Password        string `json:"password"`
	DeviceInstallID string `json:"device_install_id"`
	DeviceLabel     string `json:"device_label"`
	Platform        string `json:"platform"`
}

type signInRequest struct {
	Username        string `json:"username"`
	Password        string `json:"password"`
	DeviceInstallID string `json:"device_install_id"`
	DeviceLabel     string `json:"device_label"`
	Platform        string `json:"platform"`
}

type tokenRequest struct {
	RefreshToken string `json:"refresh_token"`
}

type challengeRequest struct {
	ChallengeToken string `json:"challenge_token"`
}

type challengeRecoveryRequest struct {
	ChallengeToken string `json:"challenge_token"`
	RecoveryKey    string `json:"recovery_key"`
}

type passwordRecoveryRequest struct {
	Username    string `json:"username"`
	RecoveryKey string `json:"recovery_key"`
	NewPassword string `json:"new_password"`
}

type trustedRecoveryRequest struct {
	Username        string `json:"username"`
	NewPassword     string `json:"new_password"`
	DeviceInstallID string `json:"device_install_id"`
	DeviceLabel     string `json:"device_label"`
	Platform        string `json:"platform"`
}

type passwordChangeRequest struct {
	CurrentPassword string `json:"current_password"`
	NewPassword     string `json:"new_password"`
}

type recoveryKeyReplaceRequest struct {
	CurrentPassword string `json:"current_password"`
}

type usernameRenameRequest struct {
	Username string `json:"username"`
}

type builtinAvatarRequest struct {
	AvatarID string `json:"avatar_id"`
}

type protectionRequest struct {
	Enabled bool `json:"enabled"`
}

type approvalDecisionRequest struct {
	Decision string `json:"decision"`
}

type sessionResponse struct {
	Account         accountResponse `json:"account"`
	Device          deviceResponse  `json:"device"`
	AccessToken     string          `json:"access_token"`
	AccessExpiresAt string          `json:"access_expires_at"`
	RefreshToken    string          `json:"refresh_token"`
}

type accountResponse struct {
	ID                      string `json:"id"`
	Username                string `json:"username"`
	ProtectNewDeviceSignins bool   `json:"protect_new_device_signins"`
	BuiltinAvatarID         string `json:"builtin_avatar_id,omitempty"`
	AvatarURL               string `json:"avatar_url,omitempty"`
}

type deviceResponse struct {
	ID         string `json:"id"`
	InstallID  string `json:"install_id"`
	Label      string `json:"label"`
	Platform   string `json:"platform"`
	Trusted    bool   `json:"trusted"`
	Current    bool   `json:"current,omitempty"`
	Active     bool   `json:"active,omitempty"`
	LastSeenAt string `json:"last_seen_at"`
}

func (h *Handler) createAccount(w http.ResponseWriter, r *http.Request) {
	var request createAccountRequest
	if err := decodeJSON(w, r, &request); err != nil {
		WriteAPIError(w, http.StatusBadRequest, "invalid_request", "The request body is invalid.")
		return
	}

	result, err := h.accounts.CreateAccount(r.Context(), account.CreateAccountInput{
		Username:        request.Username,
		Password:        request.Password,
		DeviceInstallID: request.DeviceInstallID,
		DeviceLabel:     request.DeviceLabel,
		Platform:        request.Platform,
		SourceKey:       clientNetworkKey(r),
	})
	if err != nil {
		h.writeAccountError(w, err)
		return
	}

	writeJSON(w, http.StatusCreated, map[string]any{
		"session":      encodeSession(result.Session),
		"recovery_key": result.RecoveryKey,
	})
}

func (h *Handler) signIn(w http.ResponseWriter, r *http.Request) {
	var request signInRequest
	if err := decodeJSON(w, r, &request); err != nil {
		WriteAPIError(w, http.StatusBadRequest, "invalid_request", "The request body is invalid.")
		return
	}

	result, err := h.accounts.SignIn(r.Context(), account.SignInInput{
		Username:        request.Username,
		Password:        request.Password,
		DeviceInstallID: request.DeviceInstallID,
		DeviceLabel:     request.DeviceLabel,
		Platform:        request.Platform,
		SourceKey:       clientNetworkKey(r),
	})
	if err != nil {
		h.writeAccountError(w, err)
		return
	}

	if result.Session != nil {
		writeJSON(w, http.StatusOK, map[string]any{
			"status":  result.Status,
			"session": encodeSession(*result.Session),
		})
		return
	}

	writeJSON(w, http.StatusAccepted, map[string]any{
		"status":               result.Status,
		"challenge_token":      result.ChallengeToken,
		"challenge_expires_at": result.ChallengeExpiresAt.UTC().Format(time.RFC3339Nano),
	})
}

func (h *Handler) refreshSession(w http.ResponseWriter, r *http.Request) {
	var request tokenRequest
	if err := decodeJSON(w, r, &request); err != nil {
		WriteAPIError(w, http.StatusBadRequest, "invalid_request", "The request body is invalid.")
		return
	}
	result, err := h.accounts.RefreshSession(r.Context(), request.RefreshToken)
	if err != nil {
		h.writeAccountError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"session": encodeSession(result.Session),
	})
}

func (h *Handler) revokeRefresh(w http.ResponseWriter, r *http.Request) {
	var request tokenRequest
	if err := decodeJSON(w, r, &request); err != nil {
		WriteAPIError(w, http.StatusBadRequest, "invalid_request", "The request body is invalid.")
		return
	}
	if err := h.accounts.RevokeRefreshToken(r.Context(), request.RefreshToken); err != nil {
		h.writeAccountError(w, err)
		return
	}
	w.WriteHeader(http.StatusNoContent)
}

func (h *Handler) recoverPassword(w http.ResponseWriter, r *http.Request) {
	var request passwordRecoveryRequest
	if err := decodeJSON(w, r, &request); err != nil {
		WriteAPIError(w, http.StatusBadRequest, "invalid_request", "The request body is invalid.")
		return
	}
	result, err := h.accounts.RecoverPassword(r.Context(), account.RecoverPasswordInput{
		Username:    request.Username,
		RecoveryKey: request.RecoveryKey,
		NewPassword: request.NewPassword,
		SourceKey:   clientNetworkKey(r),
	})
	if err != nil {
		h.writeAccountError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"recovery_key": result.RecoveryKey,
	})
}

func (h *Handler) startTrustedRecovery(w http.ResponseWriter, r *http.Request) {
	var request trustedRecoveryRequest
	if err := decodeJSON(w, r, &request); err != nil {
		WriteAPIError(w, http.StatusBadRequest, "invalid_request", "The request body is invalid.")
		return
	}
	result, err := h.accounts.StartTrustedRecovery(r.Context(), account.TrustedRecoveryInput{
		Username:        request.Username,
		NewPassword:     request.NewPassword,
		DeviceInstallID: request.DeviceInstallID,
		DeviceLabel:     request.DeviceLabel,
		Platform:        request.Platform,
		SourceKey:       clientNetworkKey(r),
	})
	if err != nil {
		h.writeAccountError(w, err)
		return
	}
	writeJSON(w, http.StatusAccepted, map[string]any{
		"status":               result.Status,
		"challenge_token":      result.ChallengeToken,
		"challenge_expires_at": result.ChallengeExpiresAt.UTC().Format(time.RFC3339Nano),
	})
}

func (h *Handler) pollTrustedRecovery(w http.ResponseWriter, r *http.Request) {
	var request challengeRequest
	if err := decodeJSON(w, r, &request); err != nil {
		WriteAPIError(w, http.StatusBadRequest, "invalid_request", "The request body is invalid.")
		return
	}
	result, err := h.accounts.PollTrustedRecovery(r.Context(), request.ChallengeToken)
	if err != nil {
		h.writeAccountError(w, err)
		return
	}
	body := map[string]any{"status": result.Status}
	if !result.ChallengeExpiresAt.IsZero() {
		body["challenge_expires_at"] = result.ChallengeExpiresAt.UTC().Format(time.RFC3339Nano)
	}
	if result.RecoveryKey != "" {
		body["recovery_key"] = result.RecoveryKey
	}
	writeJSON(w, http.StatusOK, body)
}

func (h *Handler) pollDeviceChallenge(w http.ResponseWriter, r *http.Request) {
	var request challengeRequest
	if err := decodeJSON(w, r, &request); err != nil {
		WriteAPIError(w, http.StatusBadRequest, "invalid_request", "The request body is invalid.")
		return
	}
	result, err := h.accounts.PollDeviceSignInChallenge(r.Context(), request.ChallengeToken)
	if err != nil {
		h.writeAccountError(w, err)
		return
	}
	body := map[string]any{"status": result.Status}
	if result.Session != nil {
		body["session"] = encodeSession(*result.Session)
	}
	if !result.ChallengeExpiresAt.IsZero() {
		body["challenge_expires_at"] = result.ChallengeExpiresAt.UTC().Format(time.RFC3339Nano)
	}
	writeJSON(w, http.StatusOK, body)
}

func (h *Handler) recoverDeviceChallengeWithKey(w http.ResponseWriter, r *http.Request) {
	var request challengeRecoveryRequest
	if err := decodeJSON(w, r, &request); err != nil {
		WriteAPIError(w, http.StatusBadRequest, "invalid_request", "The request body is invalid.")
		return
	}
	result, err := h.accounts.RecoverDeviceSignInWithKey(r.Context(), account.ChallengeRecoveryInput{
		ChallengeToken: request.ChallengeToken,
		RecoveryKey:    request.RecoveryKey,
		SourceKey:      clientNetworkKey(r),
	})
	if err != nil {
		h.writeAccountError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"session":      encodeSession(result.Session),
		"recovery_key": result.RecoveryKey,
	})
}

func (h *Handler) logoutCurrent(w http.ResponseWriter, r *http.Request) {
	if err := h.accounts.LogoutCurrent(r.Context(), authenticated(r)); err != nil {
		h.writeAccountError(w, err)
		return
	}
	w.WriteHeader(http.StatusNoContent)
}

func (h *Handler) logoutEverywhere(w http.ResponseWriter, r *http.Request) {
	if err := h.accounts.LogoutEverywhere(r.Context(), authenticated(r)); err != nil {
		h.writeAccountError(w, err)
		return
	}
	w.WriteHeader(http.StatusNoContent)
}

func (h *Handler) changePassword(w http.ResponseWriter, r *http.Request) {
	var request passwordChangeRequest
	if err := decodeJSON(w, r, &request); err != nil {
		WriteAPIError(w, http.StatusBadRequest, "invalid_request", "The request body is invalid.")
		return
	}
	if err := h.accounts.ChangePassword(r.Context(), authenticated(r), account.ChangePasswordInput{
		CurrentPassword: request.CurrentPassword,
		NewPassword:     request.NewPassword,
	}); err != nil {
		h.writeAccountError(w, err)
		return
	}
	w.WriteHeader(http.StatusNoContent)
}

func (h *Handler) replaceRecoveryKey(w http.ResponseWriter, r *http.Request) {
	var request recoveryKeyReplaceRequest
	if err := decodeJSON(w, r, &request); err != nil {
		WriteAPIError(w, http.StatusBadRequest, "invalid_request", "The request body is invalid.")
		return
	}
	result, err := h.accounts.ReplaceRecoveryKey(
		r.Context(),
		authenticated(r),
		account.ReplaceRecoveryKeyInput{CurrentPassword: request.CurrentPassword})
	if err != nil {
		h.writeAccountError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"recovery_key": result.RecoveryKey,
	})
}

func (h *Handler) getProfile(w http.ResponseWriter, r *http.Request) {
	profile, err := h.accounts.GetProfile(r.Context(), authenticated(r))
	if err != nil {
		h.writeAccountError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, encodeProfile(profile))
}

func (h *Handler) renameUsername(w http.ResponseWriter, r *http.Request) {
	var request usernameRenameRequest
	if err := decodeJSON(w, r, &request); err != nil {
		WriteAPIError(w, http.StatusBadRequest, "invalid_request", "The request body is invalid.")
		return
	}
	updated, err := h.accounts.RenameUsername(
		r.Context(),
		authenticated(r),
		account.RenameUsernameInput{NewUsername: request.Username})
	if err != nil {
		h.writeAccountError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, encodeAccount(updated))
}

func (h *Handler) setBuiltinAvatar(w http.ResponseWriter, r *http.Request) {
	var request builtinAvatarRequest
	if err := decodeJSON(w, r, &request); err != nil {
		WriteAPIError(w, http.StatusBadRequest, "invalid_request", "The request body is invalid.")
		return
	}
	result, err := h.accounts.SetBuiltinAvatar(
		r.Context(),
		authenticated(r),
		request.AvatarID)
	if err != nil {
		h.writeAccountError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, encodeProfile(result.Profile))
}

func (h *Handler) uploadAvatar(w http.ResponseWriter, r *http.Request) {
	r.Body = http.MaxBytesReader(w, r.Body, avatar.MaxBytes)
	data, err := io.ReadAll(r.Body)
	if err != nil {
		WriteAPIError(w, http.StatusBadRequest, "avatar_invalid", "That avatar image is not supported.")
		return
	}
	result, err := h.accounts.UploadAvatar(r.Context(), authenticated(r), data)
	if err != nil {
		h.writeAccountError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, encodeProfile(result.Profile))
}

func (h *Handler) listDevices(w http.ResponseWriter, r *http.Request) {
	devices, err := h.accounts.ListDevices(r.Context(), authenticated(r))
	if err != nil {
		h.writeAccountError(w, err)
		return
	}
	encoded := make([]deviceResponse, 0, len(devices))
	for _, device := range devices {
		encoded = append(encoded, deviceResponse{
			ID:         device.ID,
			InstallID:  device.InstallID,
			Label:      device.Label,
			Platform:   device.Platform,
			Trusted:    device.Trusted,
			Current:    device.Current,
			Active:     device.Active,
			LastSeenAt: device.LastSeenAt.UTC().Format(time.RFC3339Nano),
		})
	}
	writeJSON(w, http.StatusOK, map[string]any{"devices": encoded})
}

func (h *Handler) revokeDevice(w http.ResponseWriter, r *http.Request) {
	if err := h.accounts.RevokeDevice(
		r.Context(),
		authenticated(r),
		r.PathValue("deviceID")); err != nil {
		h.writeAccountError(w, err)
		return
	}
	w.WriteHeader(http.StatusNoContent)
}

func (h *Handler) setNewDeviceProtection(w http.ResponseWriter, r *http.Request) {
	var request protectionRequest
	if err := decodeJSON(w, r, &request); err != nil {
		WriteAPIError(w, http.StatusBadRequest, "invalid_request", "The request body is invalid.")
		return
	}
	updated, err := h.accounts.SetNewDeviceProtection(
		r.Context(),
		authenticated(r),
		request.Enabled)
	if err != nil {
		h.writeAccountError(w, err)
		return
	}
	writeJSON(w, http.StatusOK, encodeAccount(updated))
}

func (h *Handler) listApprovals(w http.ResponseWriter, r *http.Request) {
	wait := waitSeconds(r)
	deadline := time.Now().Add(time.Duration(wait) * time.Second)

	for {
		requests, err := h.accounts.Approvals(r.Context(), authenticated(r))
		if err != nil {
			h.writeAccountError(w, err)
			return
		}
		if len(requests) > 0 || wait == 0 || time.Now().After(deadline) {
			encoded := make([]map[string]any, 0, len(requests))
			for _, request := range requests {
				encoded = append(encoded, map[string]any{
					"id":           request.ID,
					"challenge_id": request.ID,
					"kind":         request.Kind,
					"device_label": request.DeviceLabel,
					"platform":     request.Platform,
					"expires_at":   request.ExpiresAt.UTC().Format(time.RFC3339Nano),
				})
			}
			writeJSON(w, http.StatusOK, map[string]any{"approvals": encoded})
			return
		}

		timer := time.NewTimer(time.Second)
		select {
		case <-r.Context().Done():
			timer.Stop()
			return
		case <-timer.C:
		}
	}
}

func (h *Handler) decideApproval(w http.ResponseWriter, r *http.Request) {
	var request approvalDecisionRequest
	if err := decodeJSON(w, r, &request); err != nil {
		WriteAPIError(w, http.StatusBadRequest, "invalid_request", "The request body is invalid.")
		return
	}
	decision := strings.ToLower(strings.TrimSpace(request.Decision))
	if decision != "approve" && decision != "deny" {
		WriteAPIError(w, http.StatusBadRequest, "invalid_request", "The approval decision is invalid.")
		return
	}
	if err := h.accounts.DecideApproval(
		r.Context(),
		authenticated(r),
		r.PathValue("kind"),
		r.PathValue("challengeID"),
		decision == "approve"); err != nil {
		h.writeAccountError(w, err)
		return
	}
	w.WriteHeader(http.StatusNoContent)
}

func (h *Handler) writeAccountError(w http.ResponseWriter, err error) {
	var rateLimit *account.RateLimitError
	if errors.As(err, &rateLimit) && rateLimit.Scope == "create_success_global" {
		h.logger.Warn("registration circuit breaker reached")
	} else if !isExpectedAccountError(err) {
		h.logger.Error("account request failed")
	}
	writeServiceError(w, err)
}

func isExpectedAccountError(err error) bool {
	var rateLimit *account.RateLimitError
	if errors.As(err, &rateLimit) {
		return true
	}
	return errors.Is(err, account.ErrInvalidUsername) ||
		errors.Is(err, account.ErrUsernameUnavailable) ||
		errors.Is(err, account.ErrInvalidPassword) ||
		errors.Is(err, account.ErrInvalidCredentials) ||
		errors.Is(err, account.ErrSessionInvalid) ||
		errors.Is(err, account.ErrSessionRevoked) ||
		errors.Is(err, account.ErrRecoveryKeyInvalid) ||
		errors.Is(err, account.ErrChallengeInvalid) ||
		errors.Is(err, account.ErrChallengeExpired) ||
		errors.Is(err, account.ErrChallengeDenied) ||
		errors.Is(err, account.ErrRenameCooldown) ||
		errors.Is(err, account.ErrDeviceNotFound) ||
		errors.Is(err, account.ErrAvatarInvalid) ||
		errors.Is(err, account.ErrAvatarStorageDisabled) ||
		errors.Is(err, account.ErrTrustedRecoveryNeeded)
}

func encodeSession(session account.IssuedSession) sessionResponse {
	return sessionResponse{
		Account:         encodeAccount(session.Account),
		Device:          encodeDevice(session.Device),
		AccessToken:     session.AccessToken,
		AccessExpiresAt: session.AccessExpiresAt.UTC().Format(time.RFC3339Nano),
		RefreshToken:    session.RefreshToken,
	}
}

func encodeAccount(value account.Account) accountResponse {
	return accountResponse{
		ID:                      value.ID,
		Username:                value.DisplayUsername,
		ProtectNewDeviceSignins: value.ProtectNewDeviceSignins,
		BuiltinAvatarID:         value.BuiltinAvatarID,
	}
}

func encodeDevice(value account.Device) deviceResponse {
	return deviceResponse{
		ID:         value.ID,
		InstallID:  value.InstallID,
		Label:      value.Label,
		Platform:   value.Platform,
		Trusted:    value.Trusted,
		LastSeenAt: value.LastSeenAt.UTC().Format(time.RFC3339Nano),
	}
}

func encodeProfile(value account.Profile) accountResponse {
	response := encodeAccount(value.Account)
	response.AvatarURL = value.AvatarURL
	return response
}
