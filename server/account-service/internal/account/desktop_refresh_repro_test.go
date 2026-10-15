package account

import (
	"context"
	"encoding/json"
	"errors"
	"net/http"
	"net/http/httptest"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sync/atomic"
	"testing"
	"time"
)

// Opt-in integration reproduction: a real PostgreSQL-backed Service, real Qt
// HTTP transport and timers, and a tagged Windows vault shared by fresh processes.
// Lost responses must recover without erasing the remembered session.
func TestDesktopRefreshResponseLoss(t *testing.T) {
	probe := os.Getenv("COLOSSEUM_REFRESH_REPRO_EXE")
	if runtime.GOOS != "windows" || probe == "" {
		t.Skip("set COLOSSEUM_REFRESH_REPRO_EXE to a freshly built tst_account_identity.exe on Windows")
	}
	for _, scenario := range []string{"normal_restart", "lost_response_automatic_retry", "lost_response_quick_restart", "lost_response_late_restart"} {
		t.Run(scenario, func(t *testing.T) {
			fixture := newServiceFixture(t)
			fixture.service.clock = SystemClock{}
			created := createFixtureAccount(t, fixture, "RefreshReproOwner")
			tag := "account-refresh-repro-" + created.Session.Account.ID
			stateDir := t.TempDir()
			seed, err := json.Marshal(map[string]string{
				"account_id":    created.Session.Account.ID,
				"device_id":     created.Session.Device.ID,
				"refresh_token": created.Session.RefreshToken,
			})
			if err != nil {
				t.Fatal("encode probe seed")
			}
			var dropResponse atomic.Bool
			dropResponse.Store(scenario != "normal_restart")
			var calls atomic.Int32
			var rejected atomic.Int32
			lostAt := make(chan time.Time, 1)
			server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				if r.URL.Path != "/v1/sessions/refresh" || r.Method != http.MethodPost {
					http.NotFound(w, r)
					return
				}
				calls.Add(1)
				var input struct {
					RefreshToken string `json:"refresh_token"`
				}
				if json.NewDecoder(r.Body).Decode(&input) != nil {
					http.Error(w, "invalid test request", http.StatusBadRequest)
					return
				}
				result, err := fixture.service.RefreshSession(r.Context(), input.RefreshToken)
				w.Header().Set("Content-Type", "application/json")
				if err != nil {
					rejected.Add(1)
					code := "service_error"
					status := http.StatusInternalServerError
					if errors.Is(err, ErrSessionRevoked) || errors.Is(err, ErrSessionInvalid) {
						code, status = "session_invalid", http.StatusUnauthorized
						if errors.Is(err, ErrSessionRevoked) {
							code = "session_revoked"
						}
					}
					w.WriteHeader(status)
					_ = json.NewEncoder(w).Encode(map[string]any{"error": map[string]string{"code": code, "message": "Reproduction session rejected."}})
					return
				}
				if dropResponse.Swap(false) {
					// Commit succeeded, but no response bytes reach Qt. Let its real
					// 15-second deadline fire; don't close early and invite HTTP retries.
					conn, _, err := w.(http.Hijacker).Hijack()
					if err != nil {
						t.Error("hijack response-loss connection")
						return
					}
					lostAt <- time.Now()
					go func() { time.Sleep(16 * time.Second); _ = conn.Close() }()
					return
				}
				s := result.Session
				_ = json.NewEncoder(w).Encode(map[string]any{"session": map[string]any{
					"account":           map[string]any{"id": s.Account.ID, "username": s.Account.DisplayUsername},
					"device":            map[string]any{"id": s.Device.ID},
					"access_token":      s.AccessToken,
					"access_expires_at": s.AccessExpiresAt.Format(time.RFC3339Nano),
					"refresh_token":     s.RefreshToken,
				}})
			}))
			defer server.Close()
			runProbe := func(expected string, seedJSON []byte, cleanup bool) {
				t.Helper()
				ctx, cancel := context.WithTimeout(context.Background(), 65*time.Second)
				defer cancel()
				logPath := filepath.Join(stateDir, "probe.txt")
				cmd := exec.CommandContext(ctx, probe, "refreshResponseLossProcessProbe", "-o", logPath+",txt")
				cmd.Env = append(os.Environ(),
					"COLOSSEUM_APPDATA_TAG="+tag,
					"COLOSSEUM_REFRESH_REPRO_URL="+server.URL,
					"COLOSSEUM_REFRESH_REPRO_STATE_DIR="+stateDir,
					"COLOSSEUM_REFRESH_REPRO_EXPECTED="+expected,
					"COLOSSEUM_REFRESH_REPRO_SEED="+string(seedJSON))
				if cleanup {
					cmd.Env = append(cmd.Env, "COLOSSEUM_REFRESH_REPRO_CLEANUP=1")
				}
				output, err := cmd.CombinedOutput()
				if log, readErr := os.ReadFile(logPath); readErr == nil {
					output = append(output, log...)
				} else {
					t.Errorf("read desktop probe log: %v", readErr)
				}
				t.Logf("probe expected=%s cleanup=%v\n%s", expected, cleanup, output)
				if err != nil {
					t.Fatalf("desktop probe failed: %v", err)
				}
			}
			defer runProbe("", nil, true)
			switch scenario {
			case "normal_restart":
				runProbe("signedIn", seed, false)
				runProbe("signedIn", nil, false)
			case "lost_response_automatic_retry":
				runProbe("signedIn", seed, false)
			case "lost_response_quick_restart":
				runProbe("offline", seed, false)
				runProbe("signedIn", nil, false)
			case "lost_response_late_restart":
				runProbe("offline", seed, false)
				committedAt := <-lostAt
				// Reproduce a restart beyond the original 30-second retry window.
				time.Sleep(time.Until(committedAt.Add(31 * time.Second)))
				runProbe("signedIn", nil, false)
			}
			if calls.Load() != 2 {
				t.Fatalf("refresh calls = %d, want 2", calls.Load())
			}
			if rejected.Load() != 0 {
				t.Fatalf("rejected refreshes = %d, want 0", rejected.Load())
			}
		})
	}
}
