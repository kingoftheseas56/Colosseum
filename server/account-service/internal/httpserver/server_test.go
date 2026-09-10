package httpserver

import (
	"bytes"
	"context"
	"errors"
	"log/slog"
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

type fakeSchemaChecker struct {
	err error
}

func (c fakeSchemaChecker) CheckSchema(context.Context) error {
	return c.err
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

func TestReadyRejectsIncompatibleSchemaWithoutInternalError(t *testing.T) {
	handler := New(
		fakePinger{},
		nil,
		time.Second,
		nil,
		fakeSchemaChecker{err: errors.New("password sentinel from schema")})

	request := httptest.NewRequest(http.MethodGet, "/readyz", nil)
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, request)

	if response.Code != http.StatusServiceUnavailable {
		t.Fatalf("status = %d, want %d", response.Code, http.StatusServiceUnavailable)
	}
	if strings.Contains(response.Body.String(), "password sentinel") {
		t.Fatalf("readiness leaked internal error: %q", response.Body.String())
	}
	if response.Header().Get("X-Colosseum-Error-Class") != "schema_unready" {
		t.Fatalf("error class = %q, want schema_unready", response.Header().Get("X-Colosseum-Error-Class"))
	}
}

func TestRequestDiagnosticsAreBoundedAndSanitized(t *testing.T) {
	var logs bytes.Buffer
	logger := slog.New(slog.NewJSONHandler(&logs, nil))
	handler := New(fakePinger{}, nil, time.Second, logger)

	request := httptest.NewRequest(http.MethodGet, "/v1/sync/pull", nil)
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, request)

	if response.Code != http.StatusUnauthorized {
		t.Fatalf("status = %d, want %d", response.Code, http.StatusUnauthorized)
	}
	if response.Header().Get("X-Request-ID") == "" {
		t.Fatal("request did not receive an X-Request-ID")
	}
	logText := logs.String()
	for _, expected := range []string{"request_id", "sync_pull", "latency_ms", "session_invalid"} {
		if !strings.Contains(logText, expected) {
			t.Fatalf("diagnostic log = %q, missing %q", logText, expected)
		}
	}
	if strings.Contains(logText, "token") || strings.Contains(logText, "password") {
		t.Fatalf("diagnostic log contains credential-shaped text: %q", logText)
	}
}

func TestRequestOperationUsesFixedLabels(t *testing.T) {
	for _, test := range []struct {
		method string
		path   string
		want   string
	}{
		{method: http.MethodGet, path: "/v1/sync/pull", want: "sync_pull"},
		{method: http.MethodDelete, path: "/v1/devices/secret-device-id", want: "device_revoke"},
		{method: http.MethodPost, path: "/v1/approvals/trusted/secret-challenge-id", want: "approval_decide"},
		{method: "TRACE", path: "/v1/sync/pull", want: "other"},
	} {
		t.Run(test.want, func(t *testing.T) {
			request := httptest.NewRequest(test.method, test.path, nil)
			if got := requestOperation(request); got != test.want {
				t.Fatalf("requestOperation() = %q, want %q", got, test.want)
			}
		})
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

func TestLifecycleRoutesHaveExpectedAuthBoundaries(t *testing.T) {
	var logs bytes.Buffer
	handler := New(fakePinger{}, nil, time.Second, slog.New(slog.NewJSONHandler(&logs, nil)))

	protected := []struct {
		method string
		path   string
		label  string
	}{
		{method: http.MethodGet, path: "/v1/account/export", label: "account_export"},
		{method: http.MethodDelete, path: "/v1/account", label: "account_delete"},
	}
	for _, test := range protected {
		t.Run(test.label, func(t *testing.T) {
			request := httptest.NewRequest(test.method, test.path, nil)
			response := httptest.NewRecorder()
			handler.ServeHTTP(response, request)

			if response.Code != http.StatusUnauthorized {
				t.Fatalf("status = %d, want %d", response.Code, http.StatusUnauthorized)
			}
			if response.Header().Get("X-Colosseum-Error-Class") != "session_invalid" {
				t.Fatalf("error class = %q, want session_invalid", response.Header().Get("X-Colosseum-Error-Class"))
			}
		})
	}

	request := httptest.NewRequest(
		http.MethodPost,
		"/v1/account/deletion/retry",
		strings.NewReader("{"),
	)
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, request)
	if response.Code != http.StatusBadRequest {
		t.Fatalf("public retry status = %d, want %d", response.Code, http.StatusBadRequest)
	}
	if response.Header().Get("X-Colosseum-Error-Class") != "invalid_request" {
		t.Fatalf("public retry error class = %q, want invalid_request", response.Header().Get("X-Colosseum-Error-Class"))
	}

	logText := logs.String()
	for _, label := range []string{"account_export", "account_delete", "account_deletion_retry"} {
		if !strings.Contains(logText, label) {
			t.Fatalf("diagnostic log = %q, missing %q", logText, label)
		}
	}
}
