import { test } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import vm from "node:vm";
import {
  environment,
  validate,
  validConfiguration,
  sendFeedback,
} from "../web/feedback/public/feedback-core.mjs";

const formspark = {
  provider: "formspark",
  formEndpoint: "https://submit-form.com/test",
};
const web3 = {
  provider: "web3forms",
  accessKey: "11111111-2222-3333-4444-555555555555",
};
const report = {
  type: "Bug report",
  title: "A useful title",
  message: "Enough detail to reproduce this issue.",
  email: "",
  reference: "report-1",
};
test("contact is optional, while empty and oversized reports are rejected", () => {
  assert.deepEqual(validate(report), {});
  assert.ok(validate({ ...report, message: " short " }).message);
  assert.ok(validate({ ...report, message: "x".repeat(6001) }).message);
  assert.ok(validate({ ...report, email: "bad-address" }).email);
});
test("only supported public endpoints are accepted", () => {
  for (const url of [
    "http://submit-form.com/a",
    "https://evil.example/a",
    "https://submit-form.com@evil.example/a",
    "https://submit-form.com/a?q=secret",
  ])
    assert.equal(
      validConfiguration({ ...formspark, formEndpoint: url }),
      false,
    );
  assert.equal(validConfiguration(web3), true);
  assert.equal(
    validConfiguration({ ...web3, turnstileSiteKey: "paid-only" }),
    false,
  );
});
test("a backend switch preserves report fields and uses the provider contract", async () => {
  const requests = [];
  const fake = async (url, options) => {
    requests.push({
      url,
      body: JSON.parse(options.body),
      credentials: options.credentials,
    });
    return new Response(JSON.stringify({ success: true }), { status: 200 });
  };
  await sendFeedback(formspark, report, fake);
  await sendFeedback(web3, report, fake);
  assert.equal(requests[0].url, formspark.formEndpoint);
  assert.equal(requests[1].url, "https://api.web3forms.com/submit");
  for (const { body, credentials } of requests) {
    assert.equal(body.message, report.message);
    assert.equal(body.reference, "report-1");
    assert.equal(credentials, "omit");
  }
  assert.equal(requests[1].body.access_key, web3.accessKey);
});
test("quota rejection and HTTP-200 provider failure never become success", async () => {
  await assert.rejects(
    sendFeedback(
      formspark,
      report,
      async () => new Response("", { status: 402 }),
    ),
    /capacity/,
  );
  await assert.rejects(
    sendFeedback(web3, report, async () => Response.json({ success: false })),
    /could not be sent/,
  );
});
test("network failures and timeout remain retryable", async () => {
  await assert.rejects(
    sendFeedback(formspark, report, async () => {
      throw new TypeError("offline");
    }),
    /Keep this draft/,
  );
  await assert.rejects(
    sendFeedback(
      formspark,
      report,
      (_url, { signal }) =>
        new Promise((_resolve, reject) =>
          signal.addEventListener("abort", () =>
            reject(new DOMException("Timeout", "AbortError")),
          ),
        ),
      5,
    ),
    /Keep this draft/,
  );
});
test("environment accepts no private context or arbitrary platform names", () => {
  assert.deepEqual(environment("?platform=android&version=1.2.1"), {
    platform: "Android",
    version: "1.2.1",
  });
  assert.deepEqual(environment("?platform=<script>&version=<h1>"), {
    platform: "Web",
    version: "h1",
  });
});
test("native page route rejects other sites and documents", () => {
  const scope = {};
  vm.runInNewContext(
    fs
      .readFileSync(new URL("../qml/FeedbackRoute.js", import.meta.url), "utf8")
      .replace(".pragma library", ""),
    scope,
  );
  assert.equal(scope.isDocumentUrl(scope.pageUrl("1.2.1", "android")), true);
  const universe = new URL(scope.pageUrl("1.2.1", "android", true));
  assert.equal(universe.searchParams.get("mode"), "universe");
  assert.equal(scope.isDocumentUrl(universe), true);
  for (const url of [
    "https://evil.example/feedback/",
    "https://getcolosseum.world/feedback/../",
    "https://getcolosseum.world/account",
    "https://getcolosseum.world/feedback/extra",
    "file:///tmp/feedback",
  ])
    assert.equal(scope.isDocumentUrl(url), false);
});

test("universe requests keep type, metadata and subject across providers", async () => {
  const payload = { ...report, type: "Universe request", area: "Universes", app_version: "1.2.0", platform: "Windows" };
  const bodies = [];
  const fake = async (_url, options) => {
    bodies.push(JSON.parse(options.body));
    return Response.json({ success: true });
  };
  await sendFeedback(formspark, payload, fake);
  await sendFeedback(web3, payload, fake);
  for (const body of bodies) {
    assert.equal(body.type, "Universe request");
    assert.equal(body.app_version, "1.2.0");
    assert.equal(body.platform, "Windows");
  }
  assert.equal(bodies[0]._email.subject, bodies[1].subject);
  assert.equal(bodies[1].subject, "[Colosseum] [Universe request] [Universes] A useful title");
});
