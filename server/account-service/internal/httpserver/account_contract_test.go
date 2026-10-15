package httpserver

import (
	"encoding/json"
	"testing"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/account"
)

func TestAccountResponseUsesCanonicalBuiltinAvatarID(t *testing.T) {
	profile := account.Profile{Account: account.Account{
		ID:              "account-1",
		DisplayUsername: "AvatarOwner",
		BuiltinAvatarID: "laurel",
	}}

	payload, err := json.Marshal(encodeProfile(profile))
	if err != nil {
		t.Fatal(err)
	}

	var got map[string]any
	if err := json.Unmarshal(payload, &got); err != nil {
		t.Fatal(err)
	}
	if got["builtin_avatar_id"] != "laurel" {
		t.Fatalf("builtin_avatar_id = %#v", got["builtin_avatar_id"])
	}
	if _, exists := got["avatar_id"]; exists {
		t.Fatal("stale avatar_id alias leaked into production API")
	}
}
