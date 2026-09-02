package httpserver

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
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
