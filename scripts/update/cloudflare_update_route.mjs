// Deployable feed-only Worker. A narrowly scoped zone route preserves the
// existing Pages website. Pages adapters may still import serveStableFeed.
// This module does not publish a release.
export const stableUpdatePath = "/updates/stable.json";
export const stableUpdateOrigin =
  "https://gitlab.com/kingoftheseas56/colosseum-site/-/raw/" +
  "release-assets/stable/updates/stable.json?ref_type=heads";

const CAP_BYTES = 2 * 1024 * 1024;

function unavailable() {
  return new Response("Stable update feed temporarily unavailable\n", {
    status: 503,
    headers: {
      "Content-Type": "text/plain; charset=utf-8",
      "Cache-Control": "no-store",
      "Retry-After": "300",
      "X-Content-Type-Options": "nosniff",
    },
  });
}

export async function serveStableFeed(request, fetcher = fetch) {
  if (new URL(request.url).pathname !== stableUpdatePath) return null;
  if (request.method !== "GET" && request.method !== "HEAD") {
    return new Response(null, { status: 405, headers: { Allow: "GET, HEAD" } });
  }

  const headers = new Headers({ Accept: "application/json" });
  const validator = request.headers.get("If-None-Match");
  if (validator && validator.length <= 128 && !/[\r\n]/.test(validator)) {
    headers.set("If-None-Match", validator);
  }

  try {
    const upstream = await fetcher(stableUpdateOrigin, {
      method: "GET",
      headers,
      redirect: "manual",
    });
    if (upstream.status === 304) {
      const responseHeaders = new Headers({ "Cache-Control": "public, max-age=300, must-revalidate" });
      const etag = upstream.headers.get("ETag");
      if (etag) responseHeaders.set("ETag", etag);
      return new Response(null, { status: 304, headers: responseHeaders });
    }
    if (upstream.status !== 200) return unavailable();

    const statedLength = upstream.headers.get("Content-Length");
    if (statedLength && Number(statedLength) > CAP_BYTES) return unavailable();
    const bytes = await upstream.arrayBuffer();
    if (bytes.byteLength > CAP_BYTES || bytes.byteLength === 0) return unavailable();

    // A very small syntax gate helps prevent accidentally serving an HTML
    // error page from GitLab. Client trust still relies on Ed25519 verification.
    const release = JSON.parse(new TextDecoder().decode(bytes));
    if (typeof release !== "object" || !release || Array.isArray(release)
        || typeof release.tag_name !== "string"
        || release.draft !== false || release.prerelease !== false
        || !Array.isArray(release.assets) || release.assets.length < 3) {
      return unavailable();
    }

    const responseHeaders = new Headers({
      "Content-Type": "application/json; charset=utf-8",
      "Cache-Control": "public, max-age=300, must-revalidate",
      "X-Content-Type-Options": "nosniff",
    });
    const etag = upstream.headers.get("ETag");
    if (etag) responseHeaders.set("ETag", etag);
    return new Response(request.method === "HEAD" ? null : bytes, {
      status: 200,
      headers: responseHeaders,
    });
  } catch {
    return unavailable();
  }
}

export default {
  async fetch(request) {
    const response = await serveStableFeed(request) || new Response("Not found\n", {
      status: 404,
      headers: {
        "Content-Type": "text/plain; charset=utf-8",
        "Cache-Control": "no-store",
      },
    });
    // Also strip error bodies: no signed feed exists during route preparation.
    return request.method === "HEAD" ? new Response(null, response) : response;
  },
};
