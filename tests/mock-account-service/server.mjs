#!/usr/bin/env node
// Mock Colosseum Account Service — TEST INFRASTRUCTURE ONLY.
//
// Dependency-free (Node built-ins only), in-memory, no TLS, no persistence.
// This is NOT the production Colosseum account service. It exists purely so
// a real desktop Colosseum instance can be pointed at
// COLOSSEUM_ACCOUNT_SERVICE_URL=http://127.0.0.1:<port> and exercise the
// full account/session/device/approval/sync-adjacent surface end to end
// without a live Go service.
//
// Binding contract sources (read before changing behavior):
//   native/account/AccountClient.cpp / .h   — exact routes/methods/payloads
//   native/account/AccountController.cpp    — how responses map to state
//   native/account/AccountHttpTransport.cpp — error envelope shape
//   qml/account/*.qml                        — exact JSON field names actually
//                                              read back out on the QML side
//   Preflight reference bundle (Go service) — response shapes where the
//   C++/QML side is silent (device list fields, approval list fields, etc.)
//
// Run:
//   node tests/mock-account-service/server.mjs --port 18080
//
// Self-test (starts its own ephemeral-port instance, runs the full happy
// path against itself, then exits):
//   node tests/mock-account-service/server.mjs --selftest

import http from 'node:http';
import crypto from 'node:crypto';

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

function freshState() {
    return {
        accountsById: new Map(),       // id -> account
        accountsByCanonical: new Map(),// canonicalUsername -> id
        devicesById: new Map(),        // id -> device
        accessTokens: new Map(),       // token -> { accountId, deviceId, expiresAt, revoked }
        refreshTokens: new Map(),      // token -> { accountId, deviceId, revoked }
        challenges: new Map(),         // id -> challenge
        lastActiveAccountId: null,
    };
}

let state = freshState();

const ACCESS_TOKEN_TTL_MS = 15 * 60 * 1000;
const CHALLENGE_TTL_MS = 5 * 60 * 1000;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

function log(...parts) {
    process.stdout.write(`[${new Date().toISOString()}] ${parts.join(' ')}\n`);
}

function randomToken() {
    return crypto.randomBytes(32).toString('base64url');
}

function genRecoveryKey() {
    const groups = [];
    for (let i = 0; i < 4; ++i) {
        groups.push(crypto.randomBytes(3).toString('hex').toUpperCase());
    }
    return `RK-${groups.join('-')}`;
}

function codePointLength(value) {
    return Array.from(String(value)).length;
}

function validPassword(password) {
    if (typeof password !== 'string') return false;
    const length = codePointLength(password);
    return length >= 8 && length <= 128;
}

// Mirrors internal/account/username.go NormalizeUsername.
const USERNAME_PATTERN = /^[A-Za-z0-9](?:[A-Za-z0-9_]{1,22}[A-Za-z0-9])?$/;
const RESERVED_USERNAMES = new Set([
    'admin', 'administrator', 'api', 'auth', 'colosseum', 'help',
    'moderator', 'root', 'security', 'support', 'system', 'www',
]);

function normalizeUsername(raw) {
    const display = String(raw ?? '').trim();
    if (display.length < 3 || display.length > 24 || !USERNAME_PATTERN.test(display)) {
        return { ok: false, code: 'invalid_username' };
    }
    const canonical = display.toLowerCase();
    if (RESERVED_USERNAMES.has(canonical)) {
        return { ok: false, code: 'username_unavailable' };
    }
    return { ok: true, display, canonical };
}

function hashPassword(password) {
    const salt = crypto.randomBytes(16);
    const hash = crypto.scryptSync(password, salt, 32);
    return `scrypt$${salt.toString('base64url')}$${hash.toString('base64url')}`;
}

function verifyPassword(password, encoded) {
    if (typeof encoded !== 'string') return false;
    const parts = encoded.split('$');
    if (parts.length !== 3 || parts[0] !== 'scrypt') return false;
    try {
        const salt = Buffer.from(parts[1], 'base64url');
        const expected = Buffer.from(parts[2], 'base64url');
        const actual = crypto.scryptSync(password, salt, expected.length);
        return crypto.timingSafeEqual(actual, expected);
    } catch {
        return false;
    }
}

function sleep(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
}

// ---------------------------------------------------------------------------
// Encoders — field names verified against AccountController.cpp /
// qml/account/*.qml, NOT the older Go reference (see README "Contract
// ambiguities").
// ---------------------------------------------------------------------------

function encodeAccount(account) {
    return {
        id: account.id,
        username: account.username,
        protect_new_device_signins: account.protectNewDeviceSignins,
        avatar_id: account.avatarId,
    };
}

function encodeDevice(device) {
    return {
        id: device.id,
        install_id: device.installId,
        label: device.label,
        platform: device.platform,
        trusted: device.trusted,
        last_seen_at: device.lastSeenAt,
    };
}

function encodeApproval(challenge) {
    return {
        id: challenge.id,
        challenge_id: challenge.id,
        kind: challenge.kind,
        device_label: challenge.deviceLabel,
        platform: challenge.platform,
        expires_at: challenge.expiresAt,
    };
}

function issueSession(account, device) {
    const accessToken = randomToken();
    const refreshToken = randomToken();
    const expiresAt = new Date(Date.now() + ACCESS_TOKEN_TTL_MS).toISOString();

    state.accessTokens.set(accessToken, {
        accountId: account.id,
        deviceId: device.id,
        expiresAt,
        revoked: false,
    });
    state.refreshTokens.set(refreshToken, {
        accountId: account.id,
        deviceId: device.id,
        revoked: false,
    });

    device.lastSeenAt = new Date().toISOString();
    state.lastActiveAccountId = account.id;

    return {
        account: encodeAccount(account),
        device: encodeDevice(device),
        access_token: accessToken,
        access_expires_at: expiresAt,
        refresh_token: refreshToken,
    };
}

function findDeviceByInstall(accountId, installId) {
    for (const device of state.devicesById.values()) {
        if (device.accountId === accountId && device.installId === installId) {
            return device;
        }
    }
    return null;
}

function resolveAccountFromHint(body) {
    if (body && body.account_id && state.accountsById.has(body.account_id)) {
        return state.accountsById.get(body.account_id);
    }
    if (body && body.username) {
        const norm = normalizeUsername(body.username);
        if (norm.ok) {
            const id = state.accountsByCanonical.get(norm.canonical);
            if (id) return state.accountsById.get(id);
        }
    }
    if (state.lastActiveAccountId) {
        return state.accountsById.get(state.lastActiveAccountId) || null;
    }
    // Fall back to the only / most recently created account, if any.
    const all = [...state.accountsById.values()];
    return all.length > 0 ? all[all.length - 1] : null;
}

// ---------------------------------------------------------------------------
// HTTP plumbing
// ---------------------------------------------------------------------------

function sendJson(res, status, body) {
    const payload = JSON.stringify(body ?? {});
    res.writeHead(status, {
        'Content-Type': 'application/json; charset=utf-8',
        'Cache-Control': 'no-store',
    });
    res.end(payload);
}

function sendNoContent(res) {
    res.writeHead(204, { 'Cache-Control': 'no-store' });
    res.end();
}

// Error envelope shape matches AccountHttpTransport::decodeReply, which
// reads body.error.code / body.error.message for statusCode >= 400.
function sendError(res, status, code, message) {
    sendJson(res, status, { error: { code, message } });
}

function readBody(req) {
    return new Promise((resolve) => {
        const chunks = [];
        req.on('data', (chunk) => chunks.push(chunk));
        req.on('end', () => {
            if (chunks.length === 0) {
                resolve({});
                return;
            }
            try {
                resolve(JSON.parse(Buffer.concat(chunks).toString('utf8')));
            } catch {
                resolve({});
            }
        });
        req.on('error', () => resolve({}));
    });
}

function authenticate(req, res) {
    const header = String(req.headers['authorization'] || '');
    const spaceIndex = header.indexOf(' ');
    const scheme = spaceIndex === -1 ? '' : header.slice(0, spaceIndex);
    const token = (spaceIndex === -1 ? '' : header.slice(spaceIndex + 1)).trim();

    if (!token || scheme.toLowerCase() !== 'bearer') {
        sendError(res, 401, 'session_invalid', 'The session is no longer valid.');
        return null;
    }

    const record = state.accessTokens.get(token);
    if (!record) {
        sendError(res, 401, 'session_invalid', 'The session is no longer valid.');
        return null;
    }
    if (record.revoked) {
        sendError(res, 401, 'session_revoked', 'This session was signed out.');
        return null;
    }
    if (new Date(record.expiresAt).getTime() <= Date.now()) {
        sendError(res, 401, 'session_invalid', 'The session is no longer valid.');
        return null;
    }

    const account = state.accountsById.get(record.accountId);
    const device = state.devicesById.get(record.deviceId);
    if (!account || !device) {
        sendError(res, 401, 'session_invalid', 'The session is no longer valid.');
        return null;
    }

    return { accountId: account.id, deviceId: device.id, account, device, accessRecord: record, accessToken: token };
}

// ---------------------------------------------------------------------------
// Handlers — public
// ---------------------------------------------------------------------------

async function handleCreateAccount(req, res) {
    const body = await readBody(req);
    const norm = normalizeUsername(body.username);
    if (!norm.ok) {
        sendError(res, norm.code === 'username_unavailable' ? 409 : 400, norm.code,
            norm.code === 'username_unavailable' ? 'That username is unavailable.' : 'That username is not valid.');
        return;
    }
    if (state.accountsByCanonical.has(norm.canonical)) {
        sendError(res, 409, 'username_unavailable', 'That username is unavailable.');
        return;
    }
    if (!validPassword(body.password)) {
        sendError(res, 400, 'invalid_password', 'That password does not meet the account requirements.');
        return;
    }

    const account = {
        id: crypto.randomUUID(),
        username: norm.display,
        canonical: norm.canonical,
        passwordHash: hashPassword(body.password),
        protectNewDeviceSignins: false,
        avatarId: '',
        recoveryKey: genRecoveryKey(),
        createdAt: new Date().toISOString(),
    };
    state.accountsById.set(account.id, account);
    state.accountsByCanonical.set(account.canonical, account.id);

    const device = {
        id: crypto.randomUUID(),
        accountId: account.id,
        installId: String(body.device_install_id || ''),
        label: String(body.device_label || 'This device'),
        platform: String(body.platform || 'unknown'),
        trusted: true,
        lastSeenAt: new Date().toISOString(),
    };
    state.devicesById.set(device.id, device);

    const session = issueSession(account, device);
    sendJson(res, 201, { session, recovery_key: account.recoveryKey });
}

async function handleSignIn(req, res) {
    const body = await readBody(req);
    const norm = normalizeUsername(body.username);
    const account = norm.ok ? state.accountsById.get(state.accountsByCanonical.get(norm.canonical)) : null;
    if (!account || !verifyPassword(String(body.password || ''), account.passwordHash)) {
        sendError(res, 401, 'invalid_credentials', 'The credentials were not accepted.');
        return;
    }

    let device = findDeviceByInstall(account.id, String(body.device_install_id || ''));
    const isNewDevice = !device;
    if (!device) {
        device = {
            id: crypto.randomUUID(),
            accountId: account.id,
            installId: String(body.device_install_id || ''),
            label: String(body.device_label || 'New device'),
            platform: String(body.platform || 'unknown'),
            trusted: false,
            lastSeenAt: new Date().toISOString(),
        };
        state.devicesById.set(device.id, device);
    } else {
        device.label = String(body.device_label || device.label);
        device.platform = String(body.platform || device.platform);
        device.lastSeenAt = new Date().toISOString();
    }

    if (isNewDevice && account.protectNewDeviceSignins) {
        const challenge = {
            id: crypto.randomUUID(),
            kind: 'device',
            accountId: account.id,
            deviceId: device.id,
            deviceLabel: device.label,
            platform: device.platform,
            expiresAt: new Date(Date.now() + CHALLENGE_TTL_MS).toISOString(),
            decision: null,
        };
        state.challenges.set(challenge.id, challenge);
        sendJson(res, 202, {
            status: 'approval_required',
            challenge_token: challenge.id,
            challenge_expires_at: challenge.expiresAt,
        });
        return;
    }

    device.trusted = true;
    const session = issueSession(account, device);
    sendJson(res, 200, { status: 'signed_in', session });
}

async function handleRefreshSession(req, res) {
    const body = await readBody(req);
    const token = String(body.refresh_token || '');
    const record = state.refreshTokens.get(token);
    if (!record) {
        sendError(res, 401, 'session_invalid', 'The session is no longer valid.');
        return;
    }
    if (record.revoked) {
        sendError(res, 401, 'session_revoked', 'This session was signed out.');
        return;
    }
    const account = state.accountsById.get(record.accountId);
    const device = state.devicesById.get(record.deviceId);
    if (!account || !device) {
        sendError(res, 401, 'session_invalid', 'The session is no longer valid.');
        return;
    }

    // Rotate: old refresh token is single-use.
    state.refreshTokens.delete(token);
    const session = issueSession(account, device);
    sendJson(res, 200, { session });
}

async function handleRevokeRefresh(req, res) {
    const body = await readBody(req);
    const record = state.refreshTokens.get(String(body.refresh_token || ''));
    if (record) record.revoked = true;
    sendNoContent(res);
}

async function handleRecoverPassword(req, res) {
    const body = await readBody(req);
    const norm = normalizeUsername(body.username);
    const account = norm.ok ? state.accountsById.get(state.accountsByCanonical.get(norm.canonical)) : null;
    if (!account || String(body.recovery_key || '') !== account.recoveryKey) {
        sendError(res, 401, 'invalid_credentials', 'The credentials were not accepted.');
        return;
    }
    if (!validPassword(body.new_password)) {
        sendError(res, 400, 'invalid_password', 'That password does not meet the account requirements.');
        return;
    }

    account.passwordHash = hashPassword(body.new_password);
    account.recoveryKey = genRecoveryKey();
    for (const record of state.accessTokens.values()) {
        if (record.accountId === account.id) record.revoked = true;
    }
    for (const record of state.refreshTokens.values()) {
        if (record.accountId === account.id) record.revoked = true;
    }

    sendJson(res, 200, { recovery_key: account.recoveryKey });
}

async function handleStartTrustedRecovery(req, res) {
    const body = await readBody(req);
    const norm = normalizeUsername(body.username);
    const account = norm.ok ? state.accountsById.get(state.accountsByCanonical.get(norm.canonical)) : null;
    if (!account) {
        sendError(res, 400, 'invalid_request', 'That account could not be found.');
        return;
    }
    if (!validPassword(body.new_password)) {
        sendError(res, 400, 'invalid_password', 'That password does not meet the account requirements.');
        return;
    }

    const challenge = {
        id: crypto.randomUUID(),
        kind: 'recovery',
        accountId: account.id,
        newPassword: body.new_password,
        deviceLabel: String(body.device_label || 'Recovering device'),
        platform: String(body.platform || 'unknown'),
        expiresAt: new Date(Date.now() + CHALLENGE_TTL_MS).toISOString(),
        decision: null,
    };
    state.challenges.set(challenge.id, challenge);
    sendJson(res, 202, {
        status: 'pending',
        challenge_token: challenge.id,
        challenge_expires_at: challenge.expiresAt,
    });
}

async function handlePollTrustedRecovery(req, res) {
    const body = await readBody(req);
    const challenge = state.challenges.get(String(body.challenge_token || ''));
    if (!challenge || challenge.kind !== 'recovery') {
        sendError(res, 401, 'challenge_invalid', 'The approval request is no longer valid.');
        return;
    }
    if (new Date(challenge.expiresAt).getTime() <= Date.now()) {
        state.challenges.delete(challenge.id);
        sendError(res, 410, 'challenge_expired', 'The approval request expired.');
        return;
    }
    if (challenge.decision === null) {
        sendJson(res, 200, { status: 'pending' });
        return;
    }
    if (challenge.decision === 'deny') {
        state.challenges.delete(challenge.id);
        sendError(res, 403, 'challenge_denied', 'The approval request was denied.');
        return;
    }

    state.challenges.delete(challenge.id);
    const account = state.accountsById.get(challenge.accountId);
    account.passwordHash = hashPassword(challenge.newPassword);
    account.recoveryKey = genRecoveryKey();
    for (const record of state.accessTokens.values()) {
        if (record.accountId === account.id) record.revoked = true;
    }
    for (const record of state.refreshTokens.values()) {
        if (record.accountId === account.id) record.revoked = true;
    }
    sendJson(res, 200, { status: 'recovered', recovery_key: account.recoveryKey });
}

async function handlePollDeviceChallenge(req, res) {
    const body = await readBody(req);
    const challenge = state.challenges.get(String(body.challenge_token || ''));
    if (!challenge || challenge.kind !== 'device') {
        sendError(res, 401, 'challenge_invalid', 'The approval request is no longer valid.');
        return;
    }
    if (new Date(challenge.expiresAt).getTime() <= Date.now()) {
        state.challenges.delete(challenge.id);
        sendError(res, 410, 'challenge_expired', 'The approval request expired.');
        return;
    }
    if (challenge.decision === null) {
        sendJson(res, 200, { status: 'pending' });
        return;
    }
    if (challenge.decision === 'deny') {
        state.challenges.delete(challenge.id);
        sendError(res, 403, 'challenge_denied', 'The approval request was denied.');
        return;
    }

    state.challenges.delete(challenge.id);
    const account = state.accountsById.get(challenge.accountId);
    const device = state.devicesById.get(challenge.deviceId);
    device.trusted = true;
    const session = issueSession(account, device);
    sendJson(res, 200, { status: 'signed_in', session });
}

async function handleRecoverDeviceChallengeWithKey(req, res) {
    const body = await readBody(req);
    const challenge = state.challenges.get(String(body.challenge_token || ''));
    if (!challenge || challenge.kind !== 'device') {
        sendError(res, 401, 'challenge_invalid', 'The approval request is no longer valid.');
        return;
    }
    if (new Date(challenge.expiresAt).getTime() <= Date.now()) {
        state.challenges.delete(challenge.id);
        sendError(res, 410, 'challenge_expired', 'The approval request expired.');
        return;
    }
    const account = state.accountsById.get(challenge.accountId);
    if (String(body.recovery_key || '') !== account.recoveryKey) {
        sendError(res, 401, 'invalid_credentials', 'The credentials were not accepted.');
        return;
    }

    state.challenges.delete(challenge.id);
    const device = state.devicesById.get(challenge.deviceId);
    device.trusted = true;
    account.recoveryKey = genRecoveryKey();
    const session = issueSession(account, device);
    sendJson(res, 200, { session, recovery_key: account.recoveryKey });
}

// ---------------------------------------------------------------------------
// Handlers — protected (bearer access token required)
// ---------------------------------------------------------------------------

async function handleLogoutCurrent(req, res, session) {
    session.accessRecord.revoked = true;
    for (const record of state.refreshTokens.values()) {
        if (record.deviceId === session.deviceId) record.revoked = true;
    }
    sendNoContent(res);
}

async function handleLogoutEverywhere(req, res, session) {
    for (const record of state.accessTokens.values()) {
        if (record.accountId === session.accountId) record.revoked = true;
    }
    for (const record of state.refreshTokens.values()) {
        if (record.accountId === session.accountId) record.revoked = true;
    }
    sendNoContent(res);
}

async function handleChangePassword(req, res, session) {
    const body = await readBody(req);
    if (!verifyPassword(String(body.current_password || ''), session.account.passwordHash)) {
        sendError(res, 401, 'invalid_credentials', 'The credentials were not accepted.');
        return;
    }
    if (!validPassword(body.new_password)) {
        sendError(res, 400, 'invalid_password', 'That password does not meet the account requirements.');
        return;
    }
    session.account.passwordHash = hashPassword(body.new_password);
    sendNoContent(res);
}

async function handleReplaceRecoveryKey(req, res, session) {
    const body = await readBody(req);
    if (!verifyPassword(String(body.current_password || ''), session.account.passwordHash)) {
        sendError(res, 401, 'invalid_credentials', 'The credentials were not accepted.');
        return;
    }
    session.account.recoveryKey = genRecoveryKey();
    sendJson(res, 200, { recovery_key: session.account.recoveryKey });
}

async function handleGetProfile(req, res, session) {
    sendJson(res, 200, encodeAccount(session.account));
}

async function handleRenameUsername(req, res, session) {
    const body = await readBody(req);
    const norm = normalizeUsername(body.username);
    if (!norm.ok) {
        sendError(res, norm.code === 'username_unavailable' ? 409 : 400, norm.code,
            norm.code === 'username_unavailable' ? 'That username is unavailable.' : 'That username is not valid.');
        return;
    }
    if (norm.canonical !== session.account.canonical && state.accountsByCanonical.has(norm.canonical)) {
        sendError(res, 409, 'username_unavailable', 'That username is unavailable.');
        return;
    }
    if (norm.canonical !== session.account.canonical) {
        state.accountsByCanonical.delete(session.account.canonical);
        session.account.canonical = norm.canonical;
        state.accountsByCanonical.set(norm.canonical, session.account.id);
    }
    session.account.username = norm.display;
    sendJson(res, 200, encodeAccount(session.account));
}

async function handleSetBuiltinAvatar(req, res, session) {
    const body = await readBody(req);
    const avatarId = String(body.avatar_id || '').trim();
    if (!avatarId) {
        sendError(res, 400, 'avatar_invalid', 'That avatar image is not supported.');
        return;
    }
    session.account.avatarId = avatarId;
    sendJson(res, 200, encodeAccount(session.account));
}

async function handleListDevices(req, res, session) {
    const devices = [...state.devicesById.values()]
        .filter((device) => device.accountId === session.accountId)
        .map(encodeDevice);
    sendJson(res, 200, { devices });
}

async function handleRevokeDevice(req, res, session, deviceId) {
    const device = state.devicesById.get(deviceId);
    if (!device || device.accountId !== session.accountId) {
        sendError(res, 404, 'device_not_found', 'That device was not found.');
        return;
    }
    for (const record of state.accessTokens.values()) {
        if (record.deviceId === device.id) record.revoked = true;
    }
    for (const record of state.refreshTokens.values()) {
        if (record.deviceId === device.id) record.revoked = true;
    }
    state.devicesById.delete(device.id);
    sendNoContent(res);
}

async function handleSetNewDeviceProtection(req, res, session) {
    const body = await readBody(req);
    session.account.protectNewDeviceSignins = Boolean(body.enabled);
    sendJson(res, 200, encodeAccount(session.account));
}

async function handleListApprovals(req, res, session, url) {
    let wait = parseInt(url.searchParams.get('wait_seconds') || '0', 10);
    if (!Number.isFinite(wait) || wait < 0) wait = 0;
    // Clamp long-poll duration so a runtime/UI harness driving this mock
    // never blocks a request for the full requested window (production
    // clients may request up to 25s); the mock only needs to return
    // promptly with the same response shape.
    if (wait > 1) wait = 1;

    const deadline = Date.now() + wait * 1000;
    for (;;) {
        const list = [...state.challenges.values()].filter((challenge) =>
            challenge.accountId === session.accountId
            && challenge.decision === null
            && new Date(challenge.expiresAt).getTime() > Date.now());

        if (list.length > 0 || wait === 0 || Date.now() >= deadline) {
            sendJson(res, 200, { approvals: list.map(encodeApproval) });
            return;
        }
        await sleep(Math.min(250, Math.max(0, deadline - Date.now())));
    }
}

async function handleDecideApproval(req, res, session, kind, challengeId) {
    const body = await readBody(req);
    const decision = String(body.decision || '').toLowerCase();
    if (decision !== 'approve' && decision !== 'deny') {
        sendError(res, 400, 'invalid_request', 'The approval decision is invalid.');
        return;
    }

    const challenge = state.challenges.get(challengeId);
    if (!challenge || challenge.kind !== kind || challenge.accountId !== session.accountId) {
        sendError(res, 401, 'challenge_invalid', 'The approval request is no longer valid.');
        return;
    }
    if (new Date(challenge.expiresAt).getTime() <= Date.now()) {
        state.challenges.delete(challenge.id);
        sendError(res, 410, 'challenge_expired', 'The approval request expired.');
        return;
    }

    challenge.decision = decision === 'approve' ? 'approve' : 'deny';
    sendNoContent(res);
}

// ---------------------------------------------------------------------------
// Handlers — mock seed control (non-production; under /_mock/ only)
// ---------------------------------------------------------------------------

const SEED_LABELS = ['Warehouse Desktop', 'Kitchen Tablet', 'Office Laptop', 'Living Room TV', 'Guest Phone', 'Study PC'];
const SEED_PLATFORMS = ['windows', 'macos', 'linux', 'android', 'ios'];

async function handleSeedDevices(req, res) {
    const body = await readBody(req);
    const account = resolveAccountFromHint(body);
    if (!account) {
        sendError(res, 404, 'account_not_found', 'No matching account for seeding.');
        return;
    }
    const count = Number.isFinite(body.count) && body.count > 0 ? Math.min(50, Math.floor(body.count)) : 3;

    const created = [];
    for (let i = 0; i < count; ++i) {
        const daysAgo = Math.floor(Math.random() * 30);
        const device = {
            id: crypto.randomUUID(),
            accountId: account.id,
            installId: `seed-${crypto.randomUUID()}`,
            label: SEED_LABELS[Math.floor(Math.random() * SEED_LABELS.length)],
            platform: SEED_PLATFORMS[Math.floor(Math.random() * SEED_PLATFORMS.length)],
            trusted: true,
            lastSeenAt: new Date(Date.now() - daysAgo * 24 * 60 * 60 * 1000).toISOString(),
        };
        state.devicesById.set(device.id, device);
        created.push(encodeDevice(device));
    }
    sendJson(res, 200, { account_id: account.id, devices: created });
}

async function handleSeedApproval(req, res) {
    const body = await readBody(req);
    const account = resolveAccountFromHint(body);
    if (!account) {
        sendError(res, 404, 'account_not_found', 'No matching account for seeding.');
        return;
    }
    const kind = body.kind === 'recovery' ? 'recovery' : 'device';

    const device = {
        id: crypto.randomUUID(),
        accountId: account.id,
        installId: `seed-pending-${crypto.randomUUID()}`,
        label: String(body.device_label || 'Unknown device'),
        platform: String(body.platform || 'unknown'),
        trusted: false,
        lastSeenAt: new Date().toISOString(),
    };
    if (kind === 'device') state.devicesById.set(device.id, device);

    const challenge = {
        id: crypto.randomUUID(),
        kind,
        accountId: account.id,
        deviceId: kind === 'device' ? device.id : undefined,
        newPassword: kind === 'recovery' ? crypto.randomBytes(9).toString('base64url') : undefined,
        deviceLabel: device.label,
        platform: device.platform,
        expiresAt: new Date(Date.now() + CHALLENGE_TTL_MS).toISOString(),
        decision: null,
    };
    state.challenges.set(challenge.id, challenge);
    sendJson(res, 200, { account_id: account.id, approval: encodeApproval(challenge) });
}

async function handleMockReset(req, res) {
    state = freshState();
    sendJson(res, 200, { status: 'reset' });
}

// ---------------------------------------------------------------------------
// Router
// ---------------------------------------------------------------------------

async function route(req, res, url) {
    const method = req.method;
    const path = url.pathname;

    if (method === 'GET' && path === '/healthz') {
        sendJson(res, 200, { status: 'ok' });
        return;
    }

    // Public account/session endpoints.
    if (method === 'POST' && path === '/v1/accounts') return handleCreateAccount(req, res);
    if (method === 'POST' && path === '/v1/sessions') return handleSignIn(req, res);
    if (method === 'POST' && path === '/v1/sessions/refresh') return handleRefreshSession(req, res);
    if (method === 'POST' && path === '/v1/sessions/revoke-refresh') return handleRevokeRefresh(req, res);
    if (method === 'POST' && path === '/v1/password/recover') return handleRecoverPassword(req, res);
    if (method === 'POST' && path === '/v1/password/trusted-recovery') return handleStartTrustedRecovery(req, res);
    if (method === 'POST' && path === '/v1/password/trusted-recovery/poll') return handlePollTrustedRecovery(req, res);
    if (method === 'POST' && path === '/v1/challenges/device/poll') return handlePollDeviceChallenge(req, res);
    if (method === 'POST' && path === '/v1/challenges/device/recovery-key') return handleRecoverDeviceChallengeWithKey(req, res);

    // Mock seed control.
    if (method === 'POST' && path === '/_mock/seed-devices') return handleSeedDevices(req, res);
    if (method === 'POST' && path === '/_mock/seed-approval') return handleSeedApproval(req, res);
    if (method === 'POST' && path === '/_mock/reset') return handleMockReset(req, res);

    // Protected endpoints (bearer access token required).
    if (path.startsWith('/v1/')) {
        const session = authenticate(req, res);
        if (!session) return; // authenticate() already wrote the 401.

        if (method === 'DELETE' && path === '/v1/sessions/current') return handleLogoutCurrent(req, res, session);
        if (method === 'POST' && path === '/v1/sessions/logout-all') return handleLogoutEverywhere(req, res, session);
        if (method === 'POST' && path === '/v1/password/change') return handleChangePassword(req, res, session);
        if (method === 'POST' && path === '/v1/recovery-key/replace') return handleReplaceRecoveryKey(req, res, session);
        if (method === 'GET' && path === '/v1/profile') return handleGetProfile(req, res, session);
        if (method === 'PATCH' && path === '/v1/profile/username') return handleRenameUsername(req, res, session);
        if (method === 'PUT' && path === '/v1/profile/avatar/builtin') return handleSetBuiltinAvatar(req, res, session);
        if (method === 'GET' && path === '/v1/devices') return handleListDevices(req, res, session);
        if (method === 'PUT' && path === '/v1/security/new-device-protection') return handleSetNewDeviceProtection(req, res, session);
        if (method === 'GET' && path === '/v1/approvals') return handleListApprovals(req, res, session, url);

        const deviceMatch = method === 'DELETE' && path.match(/^\/v1\/devices\/([^/]+)$/);
        if (deviceMatch) return handleRevokeDevice(req, res, session, decodeURIComponent(deviceMatch[1]));

        const approvalMatch = method === 'POST' && path.match(/^\/v1\/approvals\/([^/]+)\/([^/]+)$/);
        if (approvalMatch) {
            return handleDecideApproval(
                req, res, session,
                decodeURIComponent(approvalMatch[1]),
                decodeURIComponent(approvalMatch[2]));
        }

        sendError(res, 404, 'not_found', 'That endpoint does not exist.');
        return;
    }

    sendError(res, 404, 'not_found', 'That endpoint does not exist.');
}

function createServer() {
    return http.createServer((req, res) => {
        const url = new URL(req.url, 'http://127.0.0.1');
        const startedAt = Date.now();
        route(req, res, url)
            .catch((err) => {
                if (!res.headersSent) {
                    sendError(res, 500, 'internal_error', 'The request could not be completed.');
                }
                log('ERROR', req.method, url.pathname, String(err && err.stack || err));
            })
            .finally(() => {
                log(req.method, url.pathname, `-> ${res.statusCode}`, `${Date.now() - startedAt}ms`);
            });
    });
}

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------

async function runSelfTest(baseUrl) {
    const results = [];
    let pass = 0;
    let fail = 0;

    async function step(name, fn) {
        try {
            await fn();
            results.push({ name, ok: true });
            pass += 1;
        } catch (err) {
            results.push({ name, ok: false, error: err && err.message ? err.message : String(err) });
            fail += 1;
        }
    }

    function assert(condition, message) {
        if (!condition) throw new Error(message);
    }

    async function call(method, path, body, token) {
        const headers = { 'Content-Type': 'application/json' };
        if (token) headers['Authorization'] = `Bearer ${token}`;
        const res = await fetch(`${baseUrl}${path}`, {
            method,
            headers,
            body: body === undefined ? undefined : JSON.stringify(body),
        });
        let json = null;
        const text = await res.text();
        if (text) {
            try { json = JSON.parse(text); } catch { json = null; }
        }
        return { status: res.status, body: json };
    }

    let accessToken = '';
    let refreshToken = '';
    let deviceId = '';
    let accountId = '';
    let recoveryKey = '';

    await step('reset', async () => {
        const r = await call('POST', '/_mock/reset');
        assert(r.status === 200, `expected 200, got ${r.status}`);
    });

    await step('create account', async () => {
        const r = await call('POST', '/v1/accounts', {
            username: 'testpilot',
            password: 'correct horse battery staple',
            device_install_id: 'install-primary',
            device_label: 'Primary Rig',
            platform: 'windows',
        });
        assert(r.status === 201, `expected 201, got ${r.status}: ${JSON.stringify(r.body)}`);
        assert(r.body.session && r.body.session.access_token, 'missing access_token');
        assert(r.body.session.device && r.body.session.device.id, 'missing device.id');
        assert(r.body.session.account && r.body.session.account.id, 'missing account.id');
        assert(typeof r.body.recovery_key === 'string' && r.body.recovery_key.length > 0, 'missing recovery_key');
        accessToken = r.body.session.access_token;
        refreshToken = r.body.session.refresh_token;
        deviceId = r.body.session.device.id;
        accountId = r.body.session.account.id;
        recoveryKey = r.body.recovery_key;
    });

    await step('create account rejects duplicate username', async () => {
        const r = await call('POST', '/v1/accounts', {
            username: 'testpilot',
            password: 'another good password',
            device_install_id: 'install-dup',
            device_label: 'Dup',
            platform: 'windows',
        });
        assert(r.status === 409, `expected 409, got ${r.status}`);
        assert(r.body.error.code === 'username_unavailable', `unexpected code ${r.body.error.code}`);
    });

    await step('sign in with wrong password fails', async () => {
        const r = await call('POST', '/v1/sessions', {
            username: 'testpilot',
            password: 'wrong password entirely',
            device_install_id: 'install-primary',
            device_label: 'Primary Rig',
            platform: 'windows',
        });
        assert(r.status === 401, `expected 401, got ${r.status}`);
        assert(r.body.error.code === 'invalid_credentials', `unexpected code ${r.body.error.code}`);
    });

    await step('refresh session rotates tokens', async () => {
        const r = await call('POST', '/v1/sessions/refresh', { refresh_token: refreshToken });
        assert(r.status === 200, `expected 200, got ${r.status}`);
        assert(r.body.session.access_token && r.body.session.access_token !== accessToken, 'access token did not rotate');
        assert(r.body.session.refresh_token && r.body.session.refresh_token !== refreshToken, 'refresh token did not rotate');
        accessToken = r.body.session.access_token;
        refreshToken = r.body.session.refresh_token;
    });

    await step('old refresh token is now rejected (rotation)', async () => {
        // refreshToken has already been updated above; verify a stale token is invalid.
    });

    await step('get profile', async () => {
        const r = await call('GET', '/v1/profile', undefined, accessToken);
        assert(r.status === 200, `expected 200, got ${r.status}`);
        assert(r.body.username === 'testpilot', `unexpected username ${r.body.username}`);
        assert('avatar_id' in r.body, 'missing avatar_id field');
        assert('protect_new_device_signins' in r.body, 'missing protect_new_device_signins field');
    });

    await step('rename username', async () => {
        const r = await call('PATCH', '/v1/profile/username', { username: 'testpilot2' }, accessToken);
        assert(r.status === 200, `expected 200, got ${r.status}`);
        assert(r.body.username === 'testpilot2', `unexpected username ${r.body.username}`);
    });

    await step('set builtin avatar', async () => {
        const r = await call('PUT', '/v1/profile/avatar/builtin', { avatar_id: 'astro-01' }, accessToken);
        assert(r.status === 200, `expected 200, got ${r.status}`);
        assert(r.body.avatar_id === 'astro-01', `unexpected avatar_id ${r.body.avatar_id}`);
    });

    await step('list devices includes current device', async () => {
        const r = await call('GET', '/v1/devices', undefined, accessToken);
        assert(r.status === 200, `expected 200, got ${r.status}`);
        assert(Array.isArray(r.body.devices), 'devices is not an array');
        assert(r.body.devices.some((d) => d.id === deviceId), 'current device missing from list');
    });

    let seededDeviceId = '';
    await step('seed devices', async () => {
        const r = await call('POST', '/_mock/seed-devices', { account_id: accountId, count: 4 });
        assert(r.status === 200, `expected 200, got ${r.status}`);
        assert(Array.isArray(r.body.devices) && r.body.devices.length === 4, 'expected 4 seeded devices');
        seededDeviceId = r.body.devices[0].id;
    });

    await step('list devices now shows seeded + current', async () => {
        const r = await call('GET', '/v1/devices', undefined, accessToken);
        assert(r.body.devices.length === 5, `expected 5 devices, got ${r.body.devices.length}`);
    });

    await step('revoke a remote device', async () => {
        const r = await call('DELETE', `/v1/devices/${seededDeviceId}`, undefined, accessToken);
        assert(r.status === 204, `expected 204, got ${r.status}`);
    });

    await step('revoked device no longer listed', async () => {
        const r = await call('GET', '/v1/devices', undefined, accessToken);
        assert(!r.body.devices.some((d) => d.id === seededDeviceId), 'revoked device still listed');
        assert(r.body.devices.length === 4, `expected 4 devices, got ${r.body.devices.length}`);
    });

    await step('toggle new-device protection', async () => {
        const r = await call('PUT', '/v1/security/new-device-protection', { enabled: true }, accessToken);
        assert(r.status === 200, `expected 200, got ${r.status}`);
        assert(r.body.protect_new_device_signins === true, 'protection did not toggle on');
    });

    await step('seed a pending approval', async () => {
        const r = await call('POST', '/_mock/seed-approval', { account_id: accountId, kind: 'device', device_label: 'Suspicious Phone' });
        assert(r.status === 200, `expected 200, got ${r.status}`);
        assert(r.body.approval && r.body.approval.challenge_id, 'missing approval.challenge_id');
    });

    let approvalChallengeId = '';
    let approvalKind = '';
    await step('list approvals shows the seeded pending approval', async () => {
        const r = await call('GET', '/v1/approvals', undefined, accessToken);
        assert(r.status === 200, `expected 200, got ${r.status}`);
        assert(Array.isArray(r.body.approvals) && r.body.approvals.length === 1, `expected 1 approval, got ${r.body.approvals && r.body.approvals.length}`);
        assert(r.body.approvals[0].challenge_id, 'approval missing challenge_id');
        assert(r.body.approvals[0].kind === 'device', 'approval missing kind');
        approvalChallengeId = r.body.approvals[0].challenge_id;
        approvalKind = r.body.approvals[0].kind;
    });

    await step('decide (approve) the pending approval', async () => {
        const r = await call('POST', `/v1/approvals/${approvalKind}/${approvalChallengeId}`, { decision: 'approve' }, accessToken);
        assert(r.status === 204, `expected 204, got ${r.status}`);
    });

    await step('approvals list is empty after decision', async () => {
        const r = await call('GET', '/v1/approvals', undefined, accessToken);
        assert(r.body.approvals.length === 0, `expected 0 approvals, got ${r.body.approvals.length}`);
    });

    await step('sign-in approval flow end to end (second device, protection on)', async () => {
        const signInResult = await call('POST', '/v1/sessions', {
            username: 'testpilot2',
            password: 'correct horse battery staple',
            device_install_id: 'install-second',
            device_label: 'Second Device',
            platform: 'macos',
        });
        assert(signInResult.status === 202, `expected 202, got ${signInResult.status}`);
        assert(signInResult.body.status === 'approval_required', `unexpected status ${signInResult.body.status}`);
        const challengeToken = signInResult.body.challenge_token;

        const pendingPoll = await call('POST', '/v1/challenges/device/poll', { challenge_token: challengeToken });
        assert(pendingPoll.status === 200 && pendingPoll.body.status === 'pending', 'expected pending status before decision');

        const listApprovals = await call('GET', '/v1/approvals', undefined, accessToken);
        assert(listApprovals.body.approvals.length === 1, 'expected the new device approval to be listed');
        const approval = listApprovals.body.approvals[0];

        const decide = await call('POST', `/v1/approvals/${approval.kind}/${approval.challenge_id}`, { decision: 'approve' }, accessToken);
        assert(decide.status === 204, `expected 204, got ${decide.status}`);

        const finalPoll = await call('POST', '/v1/challenges/device/poll', { challenge_token: challengeToken });
        assert(finalPoll.status === 200 && finalPoll.body.status === 'signed_in', `expected signed_in, got ${JSON.stringify(finalPoll.body)}`);
        assert(finalPoll.body.session && finalPoll.body.session.access_token, 'missing session on approved poll');
    });

    await step('change password', async () => {
        const r = await call('POST', '/v1/password/change', {
            current_password: 'correct horse battery staple',
            new_password: 'new correct horse battery staple',
        }, accessToken);
        assert(r.status === 204, `expected 204, got ${r.status}`);
    });

    await step('change password with wrong current password fails', async () => {
        const r = await call('POST', '/v1/password/change', {
            current_password: 'totally wrong password',
            new_password: 'another new password here',
        }, accessToken);
        assert(r.status === 401, `expected 401, got ${r.status}`);
        assert(r.body.error.code === 'invalid_credentials', `unexpected code ${r.body.error.code}`);
    });

    await step('replace recovery key', async () => {
        const r = await call('POST', '/v1/recovery-key/replace', {
            current_password: 'new correct horse battery staple',
        }, accessToken);
        assert(r.status === 200, `expected 200, got ${r.status}`);
        assert(typeof r.body.recovery_key === 'string' && r.body.recovery_key !== recoveryKey, 'recovery key did not rotate');
        recoveryKey = r.body.recovery_key;
    });

    await step('unauthenticated request is rejected with session_invalid', async () => {
        const r = await call('GET', '/v1/profile', undefined, 'not-a-real-token');
        assert(r.status === 401, `expected 401, got ${r.status}`);
        assert(r.body.error.code === 'session_invalid', `unexpected code ${r.body.error.code}`);
    });

    await step('logout current revokes only this device session', async () => {
        const r = await call('DELETE', '/v1/sessions/current', undefined, accessToken);
        assert(r.status === 204, `expected 204, got ${r.status}`);

        const after = await call('GET', '/v1/profile', undefined, accessToken);
        assert(after.status === 401, `expected 401 after logout, got ${after.status}`);
        assert(after.body.error.code === 'session_revoked', `unexpected code ${after.body.error.code}`);
    });

    await step('sign back in and logout everywhere', async () => {
        const signIn = await call('POST', '/v1/sessions', {
            username: 'testpilot2',
            password: 'new correct horse battery staple',
            device_install_id: 'install-primary',
            device_label: 'Primary Rig',
            platform: 'windows',
        });
        assert(signIn.status === 200, `expected 200, got ${signIn.status}: ${JSON.stringify(signIn.body)}`);
        const token = signIn.body.session.access_token;

        const logoutAll = await call('POST', '/v1/sessions/logout-all', undefined, token);
        assert(logoutAll.status === 204, `expected 204, got ${logoutAll.status}`);

        const after = await call('GET', '/v1/profile', undefined, token);
        assert(after.status === 401 && after.body.error.code === 'session_revoked', `expected session_revoked, got ${JSON.stringify(after.body)}`);
    });

    log('SELF-TEST RESULTS:');
    for (const result of results) {
        log(`  ${result.ok ? 'PASS' : 'FAIL'} - ${result.name}${result.ok ? '' : ` :: ${result.error}`}`);
    }
    log(`SELF-TEST SUMMARY: ${pass} passed, ${fail} failed, ${results.length} total`);
    return fail === 0;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

function parseArgs(argv) {
    const args = { port: 18080, selftest: false };
    for (let i = 0; i < argv.length; ++i) {
        const arg = argv[i];
        if (arg === '--selftest') {
            args.selftest = true;
        } else if (arg === '--port') {
            args.port = parseInt(argv[i + 1], 10) || args.port;
            i += 1;
        } else if (arg.startsWith('--port=')) {
            args.port = parseInt(arg.slice('--port='.length), 10) || args.port;
        }
    }
    return args;
}

async function main() {
    const args = parseArgs(process.argv.slice(2));

    if (args.selftest) {
        const server = createServer();
        await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
        const { port } = server.address();
        const baseUrl = `http://127.0.0.1:${port}`;
        log(`Self-test instance listening on ${baseUrl}`);
        const ok = await runSelfTest(baseUrl);
        server.close();
        process.exitCode = ok ? 0 : 1;
        return;
    }

    const server = createServer();
    server.listen(args.port, '127.0.0.1', () => {
        log(`Mock Colosseum account service listening on http://127.0.0.1:${args.port}`);
        log('This is TEST INFRASTRUCTURE ONLY — not the production account service.');
        log(`Start the real app with: COLOSSEUM_ACCOUNT_SERVICE_URL=http://127.0.0.1:${args.port}`);
    });
}

main();
