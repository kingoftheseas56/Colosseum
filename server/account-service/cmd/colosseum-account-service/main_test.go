package main

import (
	"bytes"
	"context"
	"errors"
	"log/slog"
	"strings"
	"testing"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/account"
)

type fakeMaintenanceRunner struct {
	calls         int
	options       []account.MaintenanceOptions
	hasDeadline   bool
	returnedError error
}

func (f *fakeMaintenanceRunner) RunOnce(ctx context.Context, options account.MaintenanceOptions) error {
	f.calls++
	f.options = append(f.options, options)
	_, f.hasDeadline = ctx.Deadline()
	return f.returnedError
}

func TestRunMaintenancePassUsesBoundedMaintenanceRunner(t *testing.T) {
	var logs bytes.Buffer
	runner := &fakeMaintenanceRunner{
		returnedError: errors.New("database password sentinel"),
	}

	runMaintenancePass(context.Background(), slog.New(slog.NewJSONHandler(&logs, nil)), runner)

	if runner.calls != 1 {
		t.Fatalf("RunOnce calls = %d, want 1", runner.calls)
	}
	if !runner.hasDeadline {
		t.Fatal("maintenance pass did not set a deadline")
	}
	if len(runner.options) != 1 || runner.options[0] != (account.MaintenanceOptions{}) {
		t.Fatalf("RunOnce options = %#v, want zero options", runner.options)
	}
	if strings.Contains(logs.String(), "password sentinel") {
		t.Fatalf("maintenance diagnostic leaked internal error: %q", logs.String())
	}
}
