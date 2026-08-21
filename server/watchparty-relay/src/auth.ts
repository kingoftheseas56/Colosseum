// Colosseum Watch Party relay — bearer validation.
//
// Production identity uses the account service's existing authenticated
// GET /v1/profile contract. The bearer is forwarded only in the request header;
// it is never written to room state, protocol payloads, logs, or durable storage.
//
// Local acceptance can opt into the explicit dev bearer map with
// RELAY_DEV_AUTH=1. Deployed configuration must keep that switch off.

export interface Env {
  ROOMS: DurableObjectNamespace;
  RELAY_DEV_AUTH?: string;
  RELAY_DEV_BEARERS?: string;
  RELAY_ACCOUNT_SERVICE_URL?: string;
  RELAY_HOST_GRACE_MS?: string;
}

export interface BearerIdentity {
  username: string;
}

function validateDevBearer(
  env: Env,
  token: string
): BearerIdentity | null {
  if (env.RELAY_DEV_AUTH !== "1" || !env.RELAY_DEV_BEARERS) return null;

  let bearerMap: unknown;
  try {
    bearerMap = JSON.parse(env.RELAY_DEV_BEARERS);
  } catch {
    return null;
  }
  if (typeof bearerMap !== "object" || bearerMap === null) return null;

  const username = (bearerMap as Record<string, unknown>)[token];
  if (typeof username !== "string" || username.trim().length === 0) return null;
  return { username: username.trim() };
}

function accountProfileUrl(env: Env): URL | null {
  const configured = env.RELAY_ACCOUNT_SERVICE_URL?.trim();
  if (!configured) return null;

  try {
    const base = new URL(configured);
    if (base.protocol !== "https:" && base.protocol !== "http:") return null;
    if (base.username || base.password || base.search || base.hash) return null;
    return new URL("/v1/profile", base);
  } catch {
    return null;
  }
}

/**
 * Resolves the bearer to the account username. Any network error, non-2xx
 * response, malformed body, or incomplete identity fails closed to null.
 */
export async function validateBearer(
  env: Env,
  token: string | null | undefined
): Promise<BearerIdentity | null> {
  if (!token) return null;
  if (env.RELAY_DEV_AUTH === "1") return validateDevBearer(env, token);

  const profileUrl = accountProfileUrl(env);
  if (!profileUrl) return null;

  try {
    const response = await fetch(profileUrl, {
      method: "GET",
      headers: {
        Authorization: `Bearer ${token}`,
        Accept: "application/json",
      },
      redirect: "error",
      cache: "no-store",
    });
    if (!response.ok) return null;

    const body: unknown = await response.json();
    if (typeof body !== "object" || body === null) return null;
    const username = (body as Record<string, unknown>).username;
    if (typeof username !== "string" || username.trim().length === 0) return null;
    return { username: username.trim() };
  } catch {
    return null;
  }
}

/** Extracts `Bearer <token>` exactly; other schemes are treated as absent. */
export function extractBearerToken(
  authorizationHeader: string | null
): string | null {
  if (!authorizationHeader) return null;
  const prefix = "Bearer ";
  if (!authorizationHeader.startsWith(prefix)) return null;
  const token = authorizationHeader.slice(prefix.length).trim();
  return token.length > 0 ? token : null;
}
