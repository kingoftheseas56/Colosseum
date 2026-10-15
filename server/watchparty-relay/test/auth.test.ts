import { afterEach, describe, expect, it, vi } from "vitest";

import { validateBearer, type Env } from "../src/auth";

function testEnv(values: Record<string, string> = {}): Env {
  return values as unknown as Env;
}

afterEach(() => {
  vi.restoreAllMocks();
});

describe("watch party bearer validation", () => {
  it("validates a production bearer through the account profile endpoint", async () => {
    const fetchSpy = vi.spyOn(globalThis, "fetch").mockResolvedValue(
      new Response(JSON.stringify({ username: "Hemanth57", avatar_id: "laurel" }), {
        status: 200,
        headers: { "Content-Type": "application/json" },
      })
    );

    const identity = await validateBearer(
      testEnv({ RELAY_DEV_AUTH: "0", RELAY_ACCOUNT_SERVICE_URL: "https://accounts.example.test" }),
      "real-access-token"
    );

    expect(identity).toEqual({ username: "Hemanth57" });
    expect(fetchSpy).toHaveBeenCalledTimes(1);
    const [url, init] = fetchSpy.mock.calls[0];
    expect(String(url)).toBe("https://accounts.example.test/v1/profile");
    expect(init?.method).toBe("GET");
    expect(new Headers(init?.headers).get("Authorization"))
      .toBe("Bearer real-access-token");
  });

  it("fails closed when the account service rejects or returns malformed identity", async () => {
    const fetchSpy = vi.spyOn(globalThis, "fetch");
    fetchSpy.mockResolvedValueOnce(new Response("unauthorized", { status: 401 }));
    fetchSpy.mockResolvedValueOnce(
      new Response(JSON.stringify({ username: "   " }), {
        status: 200,
        headers: { "Content-Type": "application/json" },
      })
    );

    const env = testEnv({
      RELAY_DEV_AUTH: "0",
      RELAY_ACCOUNT_SERVICE_URL: "https://accounts.example.test/",
    });

    await expect(validateBearer(env, "rejected-token")).resolves.toBeNull();
    await expect(validateBearer(env, "empty-user-token")).resolves.toBeNull();
  });

  it("keeps the explicit dev bearer map isolated from production validation", async () => {
    const fetchSpy = vi.spyOn(globalThis, "fetch");
    const env = testEnv({
      RELAY_DEV_AUTH: "1",
      RELAY_DEV_BEARERS: JSON.stringify({ "dev-token": "DevHost" }),
      RELAY_ACCOUNT_SERVICE_URL: "https://accounts.example.test",
    });

    await expect(validateBearer(env, "dev-token"))
      .resolves.toEqual({ username: "DevHost" });
    expect(fetchSpy).not.toHaveBeenCalled();
  });

  it("fails closed when no production validator is configured", async () => {
    const fetchSpy = vi.spyOn(globalThis, "fetch");

    await expect(validateBearer(testEnv({ RELAY_DEV_AUTH: "0" }), "token"))
      .resolves.toBeNull();
    expect(fetchSpy).not.toHaveBeenCalled();
  });
});
