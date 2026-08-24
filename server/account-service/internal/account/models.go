package account

import "time"

type Account struct {
	ID                      string
	CanonicalUsername       string
	DisplayUsername         string
	ProtectNewDeviceSignins bool
	BuiltinAvatarID         string
	UploadedAvatarObjectKey string
	UsernameChangedAt       *time.Time
	CreatedAt               time.Time
	UpdatedAt               time.Time
}

type Device struct {
	ID         string
	AccountID  string
	InstallID  string
	Label      string
	Platform   string
	Trusted    bool
	RevokedAt  *time.Time
	CreatedAt  time.Time
	LastSeenAt time.Time
}

type Session struct {
	ID                       string
	AccountID                string
	DeviceID                 string
	AccessTokenHash          []byte
	AccessExpiresAt          time.Time
	RefreshTokenHash         []byte
	PreviousRefreshTokenHash []byte
	PreviousRefreshExpiresAt *time.Time
	RefreshRetryCiphertext   []byte
	RevokedAt                *time.Time
	CreatedAt                time.Time
	LastRefreshedAt          time.Time
}

type IssuedSession struct {
	Account         Account
	Device          Device
	AccessToken     string
	AccessExpiresAt time.Time
	RefreshToken    string
}

type SignInResult struct {
	Status             string
	Session            *IssuedSession
	ChallengeToken     string
	ChallengeExpiresAt time.Time
}

type DeviceApprovalRequest struct {
	ID          string
	Kind        string
	DeviceLabel string
	Platform    string
	ExpiresAt   time.Time
}

type Profile struct {
	Account
	AvatarURL string
}

type RecoveryResult struct {
	RecoveryKey string
}

type TrustedRecoveryResult struct {
	Status             string
	ChallengeToken     string
	ChallengeExpiresAt time.Time
	RecoveryKey        string
}

type CreateAccountInput struct {
	Username        string
	Password        string
	DeviceInstallID string
	DeviceLabel     string
	Platform        string
	SourceKey       string
}

type CreateAccountResult struct {
	Session     IssuedSession
	RecoveryKey string
}

type SignInInput struct {
	Username        string
	Password        string
	DeviceInstallID string
	DeviceLabel     string
	Platform        string
	SourceKey       string
}

type ChangePasswordInput struct {
	CurrentPassword string
	NewPassword     string
}

type RenameUsernameInput struct {
	NewUsername string
}

type RefreshResult struct {
	Session IssuedSession
}

type AuthenticatedSession struct {
	SessionID string
	Account   Account
	Device    Device
}

type RecoverPasswordInput struct {
	Username    string
	RecoveryKey string
	NewPassword string
	SourceKey   string
}

type ReplaceRecoveryKeyInput struct {
	CurrentPassword string
}

type ChallengeRecoveryInput struct {
	ChallengeToken string
	RecoveryKey    string
	SourceKey      string
}

type ChallengeRecoveryResult struct {
	Session     IssuedSession
	RecoveryKey string
}

type TrustedRecoveryInput struct {
	Username        string
	NewPassword     string
	DeviceInstallID string
	DeviceLabel     string
	Platform        string
	SourceKey       string
}

type DeviceView struct {
	Device
	Current bool
	Active  bool
}

type AvatarUpdateResult struct {
	Profile Profile
}
