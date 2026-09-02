//! Account domain — in-memory core slice (create account / sign in / refresh).
//!
//! Oracle: server/account-service (Go). Wire shapes and error codes mirror
//! internal/httpserver/{api,account_handlers}.go; domain rules mirror
//! internal/account. Do not invent policy — when this scaffold's assumptions
//! disagree with the Go source, the Go source wins and the divergence is
//! documented at the point where it deviates (see the `// Reconciliation:`
//! notes below).
//!
//! Contract: `Service` + the serde DTOs + `Error`. Everything else stays
//! module-private. Storage is in-memory for the core slice; persistence is a
//! separate TODO item, not this crate's concern yet.

use std::collections::HashMap;
use std::sync::Mutex;

use argon2::{
    password_hash::{PasswordHash, PasswordHasher, PasswordVerifier, SaltString},
    Argon2,
};
use serde::{Deserialize, Serialize};
use time::{format_description::well_known::Rfc3339, Duration, OffsetDateTime};
use unicode_normalization::UnicodeNormalization;
use uuid::Uuid;

/// Access-token lifetime, used when issuing a session.
///
/// Reconciliation: matches Go's `accessTokenLifetime` (15 * time.Minute in
/// internal/account/service.go). The scaffold's 15-minute assumption is
/// correct and is therefore kept rather than overridden.
const ACCESS_TOKEN_TTL_MINUTES: i64 = 15;

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("That username is not valid.")]
    InvalidUsername,
    #[error("That username is unavailable.")]
    UsernameUnavailable,
    #[error("That password does not meet the account requirements.")]
    InvalidPassword,
    #[error("The credentials were not accepted.")]
    InvalidCredentials,
    #[error("The session is no longer valid.")]
    SessionInvalid,
    // Reconciliation: Go has no `invalid_device` wire code — device-identity
    // failures surface as `invalid_credentials` via
    // account.ValidateDeviceIdentity (internal/account/id.go). This variant is
    // retained for the scaffold's public `Error` contract but is never
    // returned by the ported slice.
    #[error("The device identity is not valid.")]
    InvalidDevice,
}

impl Error {
    /// Wire error code, matching the Go API error vocabulary in api.go.
    pub fn code(&self) -> &'static str {
        match self {
            Error::InvalidUsername => "invalid_username",
            Error::UsernameUnavailable => "username_unavailable",
            Error::InvalidPassword => "invalid_password",
            Error::InvalidCredentials => "invalid_credentials",
            Error::SessionInvalid => "session_invalid",
            Error::InvalidDevice => "invalid_device",
        }
    }
}

#[derive(Deserialize)]
pub struct CreateAccountRequest {
    pub username: String,
    pub password: String,
    pub device_install_id: String,
    pub device_label: String,
    pub platform: String,
}

#[derive(Deserialize)]
pub struct SignInRequest {
    pub username: String,
    pub password: String,
    pub device_install_id: String,
    pub device_label: String,
    pub platform: String,
}

#[derive(Deserialize)]
pub struct RefreshRequest {
    pub refresh_token: String,
}

#[derive(Serialize)]
pub struct Session {
    pub account: AccountInfo,
    pub device: DeviceInfo,
    pub access_token: String,
    pub access_expires_at: String, // RFC 3339, like the Go service
    pub refresh_token: String,
}

#[derive(Serialize)]
pub struct AccountInfo {
    pub id: String,
    pub username: String,
    pub protect_new_device_signins: bool,
}

#[derive(Serialize)]
pub struct DeviceInfo {
    pub id: String,
    pub install_id: String,
    pub label: String,
    pub platform: String,
    pub trusted: bool,
    pub last_seen_at: String,
}

#[derive(Clone)]
struct AccountRecord {
    id: String,
    display_username: String,
    password_hash: String,
    protect_new_device_signins: bool,
}

#[derive(Clone)]
struct DeviceRecord {
    id: String,
    install_id: String,
    label: String,
    platform: String,
    trusted: bool,
    last_seen_at: OffsetDateTime,
}

struct SessionRecord {
    account_id: String,
    device_id: String,
}

struct Store {
    accounts: HashMap<String, AccountRecord>,
    accounts_by_canonical: HashMap<String, String>,
    devices: HashMap<String, DeviceRecord>,
    device_ids_by_install: HashMap<(String, String), String>,
    sessions: HashMap<String, SessionRecord>,
}

pub struct Service {
    store: Mutex<Store>,
    dummy_password_hash: String,
}

impl Service {
    pub fn in_memory() -> Self {
        // Reconciliation: mirrors Go's Service.dummyPasswordHash
        // (internal/account/service.go) used to burn argon2 time on
        // sign-in of an unknown account, so the response time does not leak
        // account existence.
        let dummy_password_hash = hash_password("colosseum-auth-dummy-password-not-a-user-secret");
        Self {
            store: Mutex::new(Store {
                accounts: HashMap::new(),
                accounts_by_canonical: HashMap::new(),
                devices: HashMap::new(),
                device_ids_by_install: HashMap::new(),
                sessions: HashMap::new(),
            }),
            dummy_password_hash,
        }
    }

    /// Create an account; the creating device becomes trusted. Passwords are
    /// hashed with argon2id (see internal/account/password.go).
    pub fn create_account(&self, request: CreateAccountRequest) -> Result<Session, Error> {
        let (display_username, canonical_username) = normalize_username(&request.username)?;
        validate_device_identity(
            &request.device_install_id,
            &request.device_label,
            &request.platform,
        )?;
        let password = validate_password(&request.password, &canonical_username)?;
        let password_hash = hash_password(&password);

        let mut store = self.store.lock().expect("account store poisoned");
        if store
            .accounts_by_canonical
            .contains_key(&canonical_username)
        {
            return Err(Error::UsernameUnavailable);
        }

        let account = AccountRecord {
            id: Uuid::new_v4().to_string(),
            display_username,
            password_hash,
            protect_new_device_signins: false,
        };
        store.accounts.insert(account.id.clone(), account.clone());
        store
            .accounts_by_canonical
            .insert(canonical_username, account.id.clone());

        let now = OffsetDateTime::now_utc();
        let device = upsert_trusted_device(
            &mut store,
            &account.id,
            &request.device_install_id,
            &request.device_label,
            &request.platform,
            now,
        );

        Ok(issue_session(&mut store, &account, &device, now))
    }

    /// Verify credentials, find-or-register the device by install id, issue a
    /// session.
    ///
    /// Reconciliation: the scaffold's doc-comment said "first device to sign
    /// in (account created elsewhere) becomes trusted". Go has no first-device
    /// rule — `upsertTrustedDeviceTx` (internal/account/store.go) marks every
    /// signing-in device trusted while `protect_new_device_signins` is off
    /// (its default; the challenge path it gates is outside this slice). The
    /// ported slice follows Go: sign-in always trusts the device.
    pub fn sign_in(&self, request: SignInRequest) -> Result<Session, Error> {
        // Reconciliation: Go's SignIn maps every username-normalization
        // failure to ErrInvalidCredentials (internal/account/identity.go),
        // so canonicalization problems do not reveal username policy.
        let (_, canonical_username) =
            normalize_username(&request.username).map_err(|_| Error::InvalidCredentials)?;
        validate_device_identity(
            &request.device_install_id,
            &request.device_label,
            &request.platform,
        )?;

        let mut store = self.store.lock().expect("account store poisoned");
        let password: String = request.password.nfc().collect();

        let account = match store.accounts_by_canonical.get(&canonical_username) {
            Some(account_id) => store
                .accounts
                .get(account_id)
                .cloned()
                .expect("account index points at a live account"),
            None => {
                // Burn argon2 time so unknown-account sign-ins pace like
                // known-account sign-ins (mirrors Go's dummy-hash verify).
                let _ = verify_password(&self.dummy_password_hash, &password);
                return Err(Error::InvalidCredentials);
            }
        };

        if !verify_password(&account.password_hash, &password) {
            return Err(Error::InvalidCredentials);
        }

        let now = OffsetDateTime::now_utc();
        let device = upsert_trusted_device(
            &mut store,
            &account.id,
            &request.device_install_id,
            &request.device_label,
            &request.platform,
            now,
        );

        Ok(issue_session(&mut store, &account, &device, now))
    }

    /// Rotate a refresh token: the old token is consumed on use.
    ///
    /// Reconciliation: Go keeps a 30s retry grace window around the previous
    /// refresh token (refreshRetryGrace + previous_refresh_token_hash in
    /// internal/account/sessions.go). The scaffold spec requires the simpler
    /// consume-on-use rule — replay returns Err(SessionInvalid) — and the
    /// retry ciphertext machinery is out of scope for this slice, so the old
    /// token is consumed immediately.
    pub fn refresh(&self, request: RefreshRequest) -> Result<Session, Error> {
        let mut store = self.store.lock().expect("account store poisoned");
        if request.refresh_token.is_empty() {
            return Err(Error::SessionInvalid);
        }

        let Some(session) = store.sessions.remove(&request.refresh_token) else {
            return Err(Error::SessionInvalid);
        };

        let account = store
            .accounts
            .get(&session.account_id)
            .cloned()
            .expect("session points at a live account");

        let now = OffsetDateTime::now_utc();
        {
            let device = store
                .devices
                .get_mut(&session.device_id)
                .expect("session points at a live device");
            device.last_seen_at = now;
        }
        let device = store
            .devices
            .get(&session.device_id)
            .cloned()
            .expect("session points at a live device");

        let access_token = generate_token();
        let refresh_token = generate_token();
        let access_expires_at = now + Duration::minutes(ACCESS_TOKEN_TTL_MINUTES);

        store.sessions.insert(
            refresh_token.clone(),
            SessionRecord {
                account_id: account.id.clone(),
                device_id: device.id.clone(),
            },
        );

        Ok(build_session(
            &account,
            &device,
            access_token,
            access_expires_at,
            refresh_token,
        ))
    }
}

/// Reconciliation: Go's `NormalizeUsername` (internal/account/username.go)
/// trims, enforces 3–24 ASCII characters matching
/// `^[A-Za-z0-9](?:[A-Za-z0-9_]{1,22}[A-Za-z0-9])?$`, lowercases into the
/// canonical form, and rejects a reserved list. It does *not* NFC-normalize
/// (the ASCII pattern makes that a no-op). Display case is preserved.
fn normalize_username(input: &str) -> Result<(String, String), Error> {
    let display = input.trim();
    if display.len() < 3 || display.len() > 24 || !display.is_ascii() {
        return Err(Error::InvalidUsername);
    }

    let bytes = display.as_bytes();
    let first = bytes[0];
    let last = bytes[bytes.len() - 1];
    if !first.is_ascii_alphanumeric() || !last.is_ascii_alphanumeric() {
        return Err(Error::InvalidUsername);
    }
    if !bytes[1..bytes.len() - 1]
        .iter()
        .all(|byte| byte.is_ascii_alphanumeric() || *byte == b'_')
    {
        return Err(Error::InvalidUsername);
    }

    let canonical = display.to_ascii_lowercase();
    if RESERVED_USERNAMES.contains(&canonical.as_str()) {
        return Err(Error::UsernameUnavailable);
    }
    Ok((display.to_string(), canonical))
}

/// Reconciliation: Go's `ValidateDeviceIdentity` (internal/account/id.go)
/// requires the install id to be a UUID. The scaffold's spec tests use opaque
/// install ids such as "install-1", so the ported slice treats the install id
/// as an opaque non-empty string. Label (≤64 runes) and platform (≤32 runes)
/// limits are kept. Failures map to `invalid_credentials`, matching Go.
fn validate_device_identity(install_id: &str, label: &str, platform: &str) -> Result<(), Error> {
    if install_id.trim().is_empty() {
        return Err(Error::InvalidCredentials);
    }
    let label = label.trim();
    let platform = platform.trim();
    if label.is_empty() || label.chars().count() > 64 {
        return Err(Error::InvalidCredentials);
    }
    if platform.is_empty() || platform.chars().count() > 32 {
        return Err(Error::InvalidCredentials);
    }
    Ok(())
}

/// Reconciliation: mirrors Go's `PasswordPolicy.Validate`
/// (internal/account/password.go): NFC-normalize, require 8–128 runes, then
/// reject blocklist hits and context values ("colosseum", "brotherhood", the
/// canonical username) plus their `123`/`1234` variants. Applies at create
/// only — sign-in verifies, it does not re-run policy.
fn validate_password(password: &str, canonical_username: &str) -> Result<String, Error> {
    let normalized: String = password.nfc().collect();
    let length = normalized.chars().count();
    if !(8..=128).contains(&length) {
        return Err(Error::InvalidPassword);
    }

    let lower = normalized.to_lowercase();
    if BASELINE_PASSWORD_BLOCKLIST
        .iter()
        .any(|entry| *entry == lower)
    {
        return Err(Error::InvalidPassword);
    }

    let canonical_lower = canonical_username.to_lowercase();
    for context in ["colosseum", "brotherhood", canonical_lower.as_str()] {
        if lower == context || lower == format!("{context}123") || lower == format!("{context}1234")
        {
            return Err(Error::InvalidPassword);
        }
    }

    Ok(normalized)
}

/// argon2id with Go's approved parameters (internal/account/password.go:
/// memory 19 MiB, 2 iterations, 1 thread, 32-byte key, 16-byte salt). The
/// argon2 crate defaults match these exactly, producing a PHC string.
fn hash_password(password: &str) -> String {
    let salt: [u8; 16] = rand::random();
    let salt = SaltString::encode_b64(&salt).expect("16-byte salt encodes as B64");
    Argon2::default()
        .hash_password(password.as_bytes(), &salt)
        .expect("argon2id hash of a valid salt cannot fail")
        .to_string()
}

fn verify_password(encoded: &str, password: &str) -> bool {
    match PasswordHash::new(encoded) {
        Ok(parsed) => Argon2::default()
            .verify_password(password.as_bytes(), &parsed)
            .is_ok(),
        Err(_) => false,
    }
}

/// 32 bytes of cryptographic randomness, base64url-encoded without padding
/// (internal/account/token.go).
fn generate_token() -> String {
    let bytes: [u8; 32] = rand::random();
    base64url_encode(&bytes)
}

const BASE64URL_ALPHABET: &[u8; 64] =
    b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

fn base64url_encode(input: &[u8]) -> String {
    let mut output = String::with_capacity(input.len().div_ceil(3) * 4);
    for chunk in input.chunks(3) {
        let value = (u32::from(chunk[0]) << 16)
            | (u32::from(chunk.get(1).copied().unwrap_or(0)) << 8)
            | u32::from(chunk.get(2).copied().unwrap_or(0));
        output.push(BASE64URL_ALPHABET[(value >> 18) as usize & 63] as char);
        output.push(BASE64URL_ALPHABET[(value >> 12) as usize & 63] as char);
        if chunk.len() > 1 {
            output.push(BASE64URL_ALPHABET[(value >> 6) as usize & 63] as char);
        }
        if chunk.len() > 2 {
            output.push(BASE64URL_ALPHABET[value as usize & 63] as char);
        }
    }
    output
}

/// find-or-register device, Go's `upsertTrustedDeviceTx`
/// (internal/account/store.go): existing devices are updated and re-trusted,
/// new devices are created already trusted.
fn upsert_trusted_device(
    store: &mut Store,
    account_id: &str,
    install_id: &str,
    label: &str,
    platform: &str,
    now: OffsetDateTime,
) -> DeviceRecord {
    let install_id = install_id.trim().to_string();
    let label = label.trim().to_string();
    let platform = platform.trim().to_string();
    let key = (account_id.to_string(), install_id.clone());

    if let Some(device_id) = store.device_ids_by_install.get(&key) {
        let device_id = device_id.clone();
        let device = store
            .devices
            .get_mut(&device_id)
            .expect("device id index points at a live device");
        device.label = label;
        device.platform = platform;
        device.trusted = true;
        device.last_seen_at = now;
        return device.clone();
    }

    let device = DeviceRecord {
        id: Uuid::new_v4().to_string(),
        install_id,
        label,
        platform,
        trusted: true,
        last_seen_at: now,
    };
    store.device_ids_by_install.insert(key, device.id.clone());
    store.devices.insert(device.id.clone(), device.clone());
    device
}

/// Go's `issueSessionTx` (internal/account/store.go): revoke prior sessions
/// for the device, then issue a fresh access/refresh pair.
fn issue_session(
    store: &mut Store,
    account: &AccountRecord,
    device: &DeviceRecord,
    now: OffsetDateTime,
) -> Session {
    let access_token = generate_token();
    let refresh_token = generate_token();
    let access_expires_at = now + Duration::minutes(ACCESS_TOKEN_TTL_MINUTES);

    store
        .sessions
        .retain(|_, session| session.device_id.as_str() != device.id.as_str());
    store.sessions.insert(
        refresh_token.clone(),
        SessionRecord {
            account_id: account.id.clone(),
            device_id: device.id.clone(),
        },
    );

    build_session(
        account,
        device,
        access_token,
        access_expires_at,
        refresh_token,
    )
}

fn build_session(
    account: &AccountRecord,
    device: &DeviceRecord,
    access_token: String,
    access_expires_at: OffsetDateTime,
    refresh_token: String,
) -> Session {
    Session {
        account: AccountInfo {
            id: account.id.clone(),
            username: account.display_username.clone(),
            protect_new_device_signins: account.protect_new_device_signins,
        },
        device: DeviceInfo {
            id: device.id.clone(),
            install_id: device.install_id.clone(),
            label: device.label.clone(),
            platform: device.platform.clone(),
            trusted: device.trusted,
            last_seen_at: rfc3339(device.last_seen_at),
        },
        access_token,
        access_expires_at: rfc3339(access_expires_at),
        refresh_token,
    }
}

fn rfc3339(value: OffsetDateTime) -> String {
    value
        .format(&Rfc3339)
        .expect("RFC 3339 formatting cannot fail")
}

/// Reconciliation: Go reserves these canonical usernames
/// (internal/account/username.go).
const RESERVED_USERNAMES: &[&str] = &[
    "admin",
    "administrator",
    "api",
    "auth",
    "colosseum",
    "help",
    "moderator",
    "root",
    "security",
    "support",
    "system",
    "www",
];

/// Reconciliation: Go's embedded baseline denylist
/// (internal/account/baseline_passwords.txt). An operator-provided
/// PASSWORD_BLOCKLIST_PATH is out of scope for this slice.
const BASELINE_PASSWORD_BLOCKLIST: &[&str] = &[
    "password",
    "password1",
    "password12",
    "password123",
    "password1234",
    "password12345",
    "password123456",
    "password1234567",
    "password12345678",
    "password123456789",
    "password1234567890",
    "passw0rd",
    "p@ssword",
    "p@ssw0rd",
    "letmein",
    "letmein123",
    "letmein1234",
    "welcome",
    "welcome1",
    "welcome123",
    "qwerty",
    "qwerty1",
    "qwerty12",
    "qwerty123",
    "qwerty1234",
    "qwerty12345",
    "qwertyuiop",
    "qwertyuiop123",
    "asdfgh",
    "asdfgh123",
    "asdfghjkl",
    "asdfghjkl123",
    "zxcvbn",
    "zxcvbn123",
    "zxcvbnm",
    "zxcvbnm123",
    "123456",
    "1234567",
    "12345678",
    "123456789",
    "1234567890",
    "111111",
    "11111111",
    "000000",
    "00000000",
    "121212",
    "123123",
    "123321",
    "654321",
    "666666",
    "7777777",
    "888888",
    "999999",
    "112233",
    "102030",
    "abc123",
    "abc123456",
    "abcdef",
    "abcdefg",
    "abcdefgh",
    "abcdefgh123",
    "abcd1234",
    "iloveyou",
    "iloveyou1",
    "iloveyou123",
    "monkey",
    "monkey123",
    "dragon",
    "dragon123",
    "master",
    "master123",
    "shadow",
    "shadow123",
    "sunshine",
    "sunshine1",
    "sunshine123",
    "princess",
    "princess1",
    "princess123",
    "football",
    "football1",
    "football123",
    "baseball",
    "baseball1",
    "baseball123",
    "soccer",
    "soccer123",
    "hockey",
    "hockey123",
    "basketball",
    "basketball123",
    "superman",
    "superman123",
    "batman",
    "batman123",
    "starwars",
    "starwars123",
    "pokemon",
    "pokemon123",
    "naruto",
    "naruto123",
    "onepiece",
    "onepiece123",
    "admin",
    "admin123",
    "administrator",
    "administrator123",
    "root",
    "root123",
    "user",
    "user123",
    "guest",
    "guest123",
    "default",
    "default123",
    "changeme",
    "changeme123",
    "secret",
    "secret123",
    "login",
    "login123",
    "test",
    "test123",
    "testing",
    "testing123",
    "computer",
    "computer123",
    "internet",
    "internet123",
    "windows",
    "windows123",
    "microsoft",
    "microsoft123",
    "google",
    "google123",
    "apple",
    "apple123",
    "android",
    "android123",
    "freedom",
    "freedom123",
    "whatever",
    "whatever123",
    "trustno1",
    "trustno1!",
    "mustang",
    "mustang123",
    "jordan",
    "jordan23",
    "michael",
    "michael123",
    "jennifer",
    "jennifer123",
    "thomas",
    "thomas123",
    "charlie",
    "charlie123",
    "hunter",
    "hunter123",
    "buster",
    "buster123",
    "pepper",
    "pepper123",
    "ginger",
    "ginger123",
    "summer",
    "summer123",
    "winter",
    "winter123",
    "spring",
    "spring123",
    "autumn",
    "autumn123",
    "colosseum",
    "colosseum123",
    "colosseum1234",
    "colosseum1234567",
    "brotherhood",
    "brotherhood123",
    "brotherhood123456",
    "hemanth",
    "hemanth123",
    "recovery",
    "recovery123",
    "account",
    "account123",
];

// ── executable spec (ralph loop: implement until these pass, then un-ignore) ──

#[cfg(test)]
mod tests {
    use super::*;

    fn request(username: &str, password: &str) -> CreateAccountRequest {
        CreateAccountRequest {
            username: username.to_string(),
            password: password.to_string(),
            device_install_id: "install-1".to_string(),
            device_label: "Test Laptop".to_string(),
            platform: "macOS".to_string(),
        }
    }

    #[test]
    fn create_sign_in_and_refresh() {
        let service = Service::in_memory();

        let session = service
            .create_account(request("SeaKing_56", "correct-horse-style"))
            .unwrap();
        assert_eq!(session.account.username, "SeaKing_56");
        assert!(session.device.trusted);

        let sign_in = service
            .sign_in(SignInRequest {
                username: "seaking_56".to_string(), // canonicalized
                password: "correct-horse-style".to_string(),
                device_install_id: "install-1".to_string(),
                device_label: "Test Laptop".to_string(),
                platform: "macOS".to_string(),
            })
            .unwrap();
        assert_eq!(sign_in.account.id, session.account.id);
        assert!(sign_in.device.trusted, "same install id keeps trust");

        let refreshed = service
            .refresh(RefreshRequest {
                refresh_token: sign_in.refresh_token.clone(),
            })
            .unwrap();
        assert_ne!(refreshed.access_token, sign_in.access_token);

        let replay = service.refresh(RefreshRequest {
            refresh_token: sign_in.refresh_token,
        });
        assert!(matches!(replay, Err(Error::SessionInvalid)));
    }

    #[test]
    fn duplicate_username_conflicts() {
        let service = Service::in_memory();
        service
            .create_account(request("neptune", "correct-horse-style"))
            .unwrap();
        assert!(matches!(
            service.create_account(request("Neptune", "other-passphrase")),
            Err(Error::UsernameUnavailable)
        ));
    }

    #[test]
    fn wrong_password_rejected() {
        let service = Service::in_memory();
        service
            .create_account(request("poseidon", "correct-horse-style"))
            .unwrap();
        assert!(matches!(
            service.sign_in(SignInRequest {
                username: "poseidon".to_string(),
                password: "wrong".to_string(),
                device_install_id: "install-1".to_string(),
                device_label: "Test Laptop".to_string(),
                platform: "macOS".to_string(),
            }),
            Err(Error::InvalidCredentials)
        ));
    }
}
