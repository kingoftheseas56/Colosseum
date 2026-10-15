package testdb

import "testing"

func TestSafeDatabaseName(t *testing.T) {
	tests := []struct {
		name string
		want bool
	}{
		{name: "colosseum_account_test", want: true},
		{name: "COLOSSEUM_ACCOUNT_TEST", want: true},
		{name: " colosseum_account_test ", want: true},
		{name: "colosseum_account", want: false},
		{name: "production", want: false},
		{name: "", want: false},
	}

	for _, tt := range tests {
		if got := safeDatabaseName(tt.name); got != tt.want {
			t.Fatalf("safeDatabaseName(%q) = %v, want %v", tt.name, got, tt.want)
		}
	}
}
