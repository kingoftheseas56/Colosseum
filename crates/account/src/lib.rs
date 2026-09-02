//! Account domain — scaffolding for the ralph loop.
//!
//! Oracle: server/account-service (Go). Wire shapes and error codes mirror
//! internal/httpserver/{api,account_handlers}.go; domain rules mirror
//! internal/account. Do not invent policy — when this scaffold's assumptions
//! disagree with the Go source, the Go source wins and the docs get updated.
//!
//! Contract: `Service` + the serde DTOs + `Error`. Everything else stays
//! module-private. Storage is in-memory for the core slice; persistence is a
//! separate TODO item, not this crate's concern yet.

use serde::{Deserialize, Serialize};

// Consumed by the account-core todo; present now so the contract's TTL
// assumption is visible in one place.
#[allow(dead_code)]
const ACCESS_TOKEN_TTL_MINUTES: i64 = 15; // assumption: reconcile with Go

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

pub struct Service {
    _private: (),
}

impl Service {
    pub fn in_memory() -> Self {
        Self { _private: () }
    }

    /// Create an account; the creating device becomes trusted. Passwords are
    /// hashed with argon2id (see internal/account/password.go).
    pub fn create_account(&self, _request: CreateAccountRequest) -> Result<Session, Error> {
        todo!("TODO.md: account-core")
    }

    /// Verify credentials, find-or-register the device by install id, issue a
    /// session. First device to sign in (account created elsewhere) becomes
    /// trusted; verify against Go semantics before committing.
    pub fn sign_in(&self, _request: SignInRequest) -> Result<Session, Error> {
        todo!("TODO.md: account-core")
    }

    /// Rotate a refresh token: the old one must be consumed on use (replay
    /// returns Err(SessionInvalid)).
    pub fn refresh(&self, _request: RefreshRequest) -> Result<Session, Error> {
        todo!("TODO.md: account-core")
    }
}

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
    #[ignore = "scaffold spec: enable in TODO.md account-core"]
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
    #[ignore = "scaffold spec: enable in TODO.md account-core"]
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
    #[ignore = "scaffold spec: enable in TODO.md account-core"]
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
