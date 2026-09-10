package httpserver

import (
	"encoding/json"
	"errors"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/account"
)

func TestAPIContract(t *testing.T) {
	if APIPrefix != "/v1" {
		t.Fatalf("APIPrefix = %q, want /v1", APIPrefix)
	}

	response := httptest.NewRecorder()
	WriteAPIError(response,
		http.StatusUnauthorized,
		"invalid_credentials",
		"The credentials were not accepted.")

	if response.Code != http.StatusUnauthorized {
		t.Fatalf("status = %d, want %d", response.Code, http.StatusUnauthorized)
	}

	var body APIError
	if err := json.Unmarshal(response.Body.Bytes(), &body); err != nil {
		t.Fatalf("decode API error: %v", err)
	}
	if body.Error.Code != "invalid_credentials" {
		t.Fatalf("error code = %q, want invalid_credentials", body.Error.Code)
	}
	if body.Error.Message != "The credentials were not accepted." {
		t.Fatalf("error message = %q", body.Error.Message)
	}
}

func TestServiceErrorMappingIsStableAndSanitized(t *testing.T) {
	response := httptest.NewRecorder()
	writeServiceError(response, errors.Join(
		errors.New("password=sentinel host=database.example"),
		account.ErrExportCursorInvalid))

	if response.Code != http.StatusBadRequest {
		t.Fatalf("status = %d, want %d", response.Code, http.StatusBadRequest)
	}
	if response.Header().Get("X-Colosseum-Error-Class") != "export_cursor_invalid" {
		t.Fatalf("error class = %q, want export_cursor_invalid", response.Header().Get("X-Colosseum-Error-Class"))
	}
	body := response.Body.String()
	if strings.Contains(body, "sentinel") || strings.Contains(body, "database.example") {
		t.Fatalf("error body leaked internal detail: %q", body)
	}

	response = httptest.NewRecorder()
	writeServiceError(response, errors.New("dsn password=another-sentinel"))
	if response.Code != http.StatusInternalServerError {
		t.Fatalf("internal status = %d, want %d", response.Code, http.StatusInternalServerError)
	}
	if response.Header().Get("X-Colosseum-Error-Class") != "internal_error" {
		t.Fatalf("internal error class = %q, want internal_error", response.Header().Get("X-Colosseum-Error-Class"))
	}
	if strings.Contains(response.Body.String(), "another-sentinel") {
		t.Fatalf("internal error leaked detail: %q", response.Body.String())
	}
}
