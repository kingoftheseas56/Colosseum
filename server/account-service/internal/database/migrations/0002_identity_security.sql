CREATE TABLE username_reservations (
    canonical_username text PRIMARY KEY,
    reserved_account_id uuid,
    reserved_at timestamptz NOT NULL
);

CREATE TABLE accounts (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    canonical_username text NOT NULL UNIQUE,
    display_username text NOT NULL,
    password_hash text NOT NULL,
    recovery_key_verifier bytea NOT NULL,
    recovery_key_version integer NOT NULL DEFAULT 1,
    protect_new_device_signins boolean NOT NULL DEFAULT false,
    builtin_avatar_id text,
    uploaded_avatar_object_key text,
    username_changed_at timestamptz,
    created_at timestamptz NOT NULL,
    updated_at timestamptz NOT NULL,
    CONSTRAINT accounts_username_reservation_fk
        FOREIGN KEY (canonical_username)
        REFERENCES username_reservations(canonical_username),
    CONSTRAINT accounts_avatar_choice_ck
        CHECK (
            (builtin_avatar_id IS NULL) OR
            (uploaded_avatar_object_key IS NULL)
        )
);

CREATE TABLE devices (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    install_id uuid NOT NULL,
    label text NOT NULL,
    platform text NOT NULL,
    trusted boolean NOT NULL DEFAULT true,
    revoked_at timestamptz,
    created_at timestamptz NOT NULL,
    last_seen_at timestamptz NOT NULL,
    UNIQUE (account_id, install_id)
);

CREATE TABLE sessions (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    device_id uuid NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    access_token_hash bytea NOT NULL UNIQUE,
    access_expires_at timestamptz NOT NULL,
    refresh_token_hash bytea NOT NULL UNIQUE,
    previous_refresh_token_hash bytea,
    previous_refresh_expires_at timestamptz,
    refresh_retry_ciphertext bytea,
    revoked_at timestamptz,
    created_at timestamptz NOT NULL,
    last_refreshed_at timestamptz NOT NULL
);

CREATE UNIQUE INDEX sessions_one_active_per_device_idx
    ON sessions(device_id)
    WHERE revoked_at IS NULL;

CREATE INDEX sessions_account_active_idx
    ON sessions(account_id, revoked_at);

CREATE INDEX sessions_previous_refresh_idx
    ON sessions(previous_refresh_token_hash)
    WHERE previous_refresh_token_hash IS NOT NULL;

CREATE TABLE device_signin_challenges (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    target_install_id uuid NOT NULL,
    target_label text NOT NULL,
    target_platform text NOT NULL,
    challenge_token_hash bytea NOT NULL UNIQUE,
    state text NOT NULL,
    expires_at timestamptz NOT NULL,
    created_at timestamptz NOT NULL,
    decided_at timestamptz,
    decided_by_device_id uuid REFERENCES devices(id) ON DELETE SET NULL,
    consumed_at timestamptz,
    CONSTRAINT device_signin_challenges_state_ck
        CHECK (state IN ('pending', 'approved', 'denied', 'consumed'))
);

CREATE INDEX device_signin_challenges_pending_idx
    ON device_signin_challenges(account_id, state, expires_at);

CREATE TABLE trusted_recovery_challenges (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    target_install_id uuid NOT NULL,
    target_label text NOT NULL,
    target_platform text NOT NULL,
    challenge_token_hash bytea NOT NULL UNIQUE,
    new_password_hash text NOT NULL,
    state text NOT NULL,
    expires_at timestamptz NOT NULL,
    created_at timestamptz NOT NULL,
    decided_at timestamptz,
    decided_by_device_id uuid REFERENCES devices(id) ON DELETE SET NULL,
    consumed_at timestamptz,
    CONSTRAINT trusted_recovery_challenges_state_ck
        CHECK (state IN ('pending', 'approved', 'denied', 'consumed'))
);

CREATE INDEX trusted_recovery_challenges_pending_idx
    ON trusted_recovery_challenges(account_id, state, expires_at);

CREATE TABLE auth_rate_events (
    id bigserial PRIMARY KEY,
    event_type text NOT NULL,
    key_hash bytea NOT NULL,
    occurred_at timestamptz NOT NULL
);

CREATE INDEX auth_rate_events_lookup_idx
    ON auth_rate_events(event_type, key_hash, occurred_at);

CREATE TABLE account_security_events (
    id bigserial PRIMARY KEY,
    account_id uuid REFERENCES accounts(id) ON DELETE CASCADE,
    event_type text NOT NULL,
    device_id uuid,
    occurred_at timestamptz NOT NULL,
    metadata jsonb NOT NULL DEFAULT '{}'::jsonb
);

CREATE INDEX account_security_events_account_idx
    ON account_security_events(account_id, occurred_at DESC);


CREATE TABLE avatar_cleanup_queue (
    id bigserial PRIMARY KEY,
    object_key text NOT NULL UNIQUE,
    enqueued_at timestamptz NOT NULL,
    attempts integer NOT NULL DEFAULT 0,
    last_error text,
    next_attempt_at timestamptz NOT NULL
);

CREATE INDEX avatar_cleanup_queue_due_idx
    ON avatar_cleanup_queue(next_attempt_at, id);
