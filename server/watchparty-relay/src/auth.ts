// Colosseum Watch Party relay — bearer validation seam (Slice 3 of
// docs/superpowers/plans/2026-08-20-watch-party-relay-plan.md).
//
// Fail-closed identity (plan "Standing constraints"): the DEV validator
// (accept-configured-test-bearer) is env-gated (`RELAY_DEV_AUTH=1`) and
// DEFAULT OFF. With no validator configured, signed-in connects are
// REFUSED (typed error), guest connects still work. The dev validator must
// never be enabled in a deployed configuration — wrangler.toml ships it
// "0"; a deploy config must keep it "0" (or absent).
//
// D3 (SERVER-PROTOCOL-CONTRACT.md "External adoption prerequisites" #3):
// real account bearer issuer/validation is account-lane work outside this
// plan (the account service only runs on 127.0.0.1:8080 today, with no
// public bearer-introspection surface). This seam is where that real
// validator plugs in later without touching room-do.ts's call site.

export interface Env {
  ROOMS: DurableObjectNamespace;
  /** "1" to enable the dev bearer map below; any other value (including
   * unset) is the fail-closed default — no signed-in connect can ever
   * authenticate. */
  RELAY_DEV_AUTH?: string;
  /** JSON object: { "<bearer token>": "<username>" }. Only consulted when
   * RELAY_DEV_AUTH === "1". */
  RELAY_DEV_BEARERS?: string;
}

export interface BearerIdentity {
  username: string;
}

/**
 * Validates a bearer token against the currently configured identity
 * seam. Returns the authenticated identity, or null when the token is
 * absent/invalid/unrecognized OR (fail-closed) when no validator is
 * configured at all.
 */
export function validateBearer(
  env: Env,
  token: string | null | undefined
): BearerIdentity | null {
  if (!token) return null;

  // Fail-closed: no validator configured -> every signed-in attempt is
  // refused, never silently treated as guest-equivalent trust.
  if (env.RELAY_DEV_AUTH !== "1") return null;

  if (!env.RELAY_DEV_BEARERS) return null;

  let bearerMap: unknown;
  try {
    bearerMap = JSON.parse(env.RELAY_DEV_BEARERS);
  } catch {
    return null;
  }
  if (typeof bearerMap !== "object" || bearerMap === null) return null;

  const username = (bearerMap as Record<string, unknown>)[token];
  if (typeof username !== "string" || username.trim().length === 0) {
    return null;
  }

  return { username: username.trim() };
}

/**
 * Extracts the bearer token from an `Authorization: Bearer <token>` header
 * value, mirroring the exact prefix the client sends
 * (WebSocketWatchPartyTransport.cpp: `"Bearer " + bearerToken`). Returns
 * null for a missing header, wrong scheme, or empty token — all of which
 * are guest-equivalent (no credential presented).
 */
export function extractBearerToken(
  authorizationHeader: string | null
): string | null {
  if (!authorizationHeader) return null;
  const prefix = "Bearer ";
  if (!authorizationHeader.startsWith(prefix)) return null;
  const token = authorizationHeader.slice(prefix.length).trim();
  return token.length > 0 ? token : null;
}
