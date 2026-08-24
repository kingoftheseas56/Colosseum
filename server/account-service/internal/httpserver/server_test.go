package httpserver

import (
	"context"
	"errors"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

type fakePinger struct {
	err error
}

func (p fakePinger) Ping(context.Context) error {
	return p.err
}

func TestHealthIsIndependentOfDatabaseReadiness(t *testing.T) {
	handler := New(
		fakePinger{err: errors.New("database unavailable")},
		nil,
		time.Second,
		nil)

	request := httptest.NewRequest(http.MethodGet, "/healthz", nil)
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, request)

	if response.Code != http.StatusOK {
		t.Fatalf("status = %d, want %d", response.Code, http.StatusOK)
	}
	if !strings.Contains(response.Body.String(), `"status":"ok"`) {
		t.Fatalf("body = %q, want ok status", response.Body.String())
	}
	if response.Header().Get("Cache-Control") != "no-store" {
		t.Fatalf("Cache-Control = %q, want no-store", response.Header().Get("Cache-Control"))
	}
}

func TestReadyReturnsUnavailableWithoutInternalError(t *testing.T) {
	handler := New(
		fakePinger{err: errors.New("database password sentinel")},
		nil,
		time.Second,
		nil)

	request := httptest.NewRequest(http.MethodGet, "/readyz", nil)
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, request)

	if response.Code != http.StatusServiceUnavailable {
		t.Fatalf("status = %d, want %d", response.Code, http.StatusServiceUnavailable)
	}
	if strings.Contains(response.Body.String(), "password sentinel") {
		t.Fatalf("readiness leaked internal error: %q", response.Body.String())
	}
}

func TestUnknownProtectedRouteRequiresAuthentication(t *testing.T) {
	handler := New(fakePinger{}, nil, time.Second, nil)

	request := httptest.NewRequest(http.MethodGet, "/v1/profile", nil)
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, request)

	if response.Code != http.StatusUnauthorized {
		t.Fatalf("status = %d, want %d", response.Code, http.StatusUnauthorized)
	}
}
