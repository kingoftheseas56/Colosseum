# Colosseum feedback page

A static page shared by the desktop WebEngine and Android WebView hosts. The app opens `https://getcolosseum.world/feedback/` from an icon-only Feedback entry in the persistent top bar or Vault header, available from Home and every world. The app supplies only its version and platform. The form works without an account and accepts Bug, Feature, or Universe Request reports, an optional world, title, message, and optional reply email. Its report-type header uses icons, and the optional world is selected through the glass-style picker.

## Current configuration

`public/config.json` uses the public Formspark endpoint supplied for Colosseum. No private API token is required or stored. Formspark currently gives the first workspace 250 accepted submissions in total, not per month. The implementation also supports Web3Forms, whose currently advertised free plan allows 250 submissions each month. Do not purchase a plan or credit bundle as part of this feature.

The browser checks and automated tests intercept submissions. They verify mocked POST handling only; they do not consume the live form's allowance or prove mailbox delivery. No real hosted submission has been confirmed yet. The first delivery check should send one clearly labelled report from the hosted page and verify it arrives in both Formspark and the configured mailbox. After that first delivery test, continue the deeper creative redesign of the feedback experience.

## Formspark setup

1. In the Formspark dashboard, open the **Colosseum feedback** form associated with `https://submit-form.com/VADEhLJlB`.
2. Enable email notifications to the inbox you want to use for reports. Verify that receiving address if prompted. Use this same inbox for any future provider.
3. Keep the form accepting anonymous messages; the reply email is optional. Standard spam filtering is sufficient to begin testing. A Turnstile site key can optionally be configured below; its matching secret belongs only in Formspark's form settings.
4. Once the page is hosted, send one clearly labelled test message and confirm it appears both in Formspark and in the receiving mailbox, including spam/junk folders. This consumes one accepted submission.
5. Review the remaining allowance periodically. Prepare a provider switch with headroom rather than waiting for the last submission. No payment is needed for the implemented switch.

## Build and host

Run from the repository root:

```powershell
node web/feedback/build.mjs
```

The output is `web/feedback/dist`. Copy its contents into the existing website's `feedback/` directory and publish the website using its normal verified deployment process. The output includes the bundled Colosseum mark and Fraunces font, so no remote font service is needed. The application should be released only after the production URL serves this page.

For Cloudflare Pages, merge the output's `_headers` rules into the website's root `_headers` file, scoped to `/feedback/*`, with the config rule at `/feedback/config.json`. Preserve the website's other headers and installer redirects. `_headers` inside a subdirectory is not a Pages configuration file.

For another static host, configure equivalent CSP, content-type, referrer, and cache rules from the supplied `_headers`. `config.json` must not be cached. Retain `/feedback/` as the stable app URL.

Public build overrides are available: `COLOSSEUM_FEEDBACK_PROVIDER`, `COLOSSEUM_FEEDBACK_FORM_ENDPOINT`, `COLOSSEUM_FEEDBACK_ACCESS_KEY`, and `COLOSSEUM_FEEDBACK_TURNSTILE_SITE_KEY`. None is a privileged account API token. The endpoint and keys in the built page are visible to browsers by design.

## Switch to renewable free submissions

1. Create a free Web3Forms account and a form for the **same receiving email address**. Copy that form's public access key. The code does not need a private API token.
2. Export the previous Formspark submissions as CSV or JSON. Keep the old provider available while checking delivery from the replacement. Old notification emails remain in your mailbox; importing old submissions into the new provider's dashboard is not assumed.
3. Update the hosted `config.json`:

```json
{
  "provider": "web3forms",
  "accessKey": "YOUR_PUBLIC_FORM_ACCESS_KEY",
  "formEndpoint": "",
  "turnstileSiteKey": ""
}
```

4. Reload the page and verify a test message reaches the same inbox. This is a website configuration change; neither Windows nor Android needs an app update. The labels, fields, draft, reference, and email subject format stay the same. The service's own dashboard will change.
5. Keep Web3Forms' automatic spam filtering enabled. Its free tier currently includes hCaptcha, while Turnstile is listed as a paid feature; leave `turnstileSiteKey` empty for the free Web3Forms route. This implementation does not include a paid CAPTCHA dependency.

Email subjects have the same format on both providers: `[Colosseum] [Bug report / Feature idea] [Area] Title`. Inbox rules can organize those subjects independently of sender/provider. The public page does not offer ticket status tracking. A reference is included in each report for matching correspondence; it is not a promise that a provider deduplicates retries after a lost response.

Switching to an additional provider requires a website adapter and CSP update, rather than just a new URL. The native hosts pin the top-level document to the owned feedback page and give it no privileged app bridge. HTTPS resources are governed by that page's hosted CSP, so adding another receiving provider does not require a new native app release.

Monthly free quotas can also be exhausted. Web3Forms says it warns at 90% and 100%, then rejects further submissions until renewal. Rejections remain visible and preserve the draft. Provider plans may change: recheck their actual terms before launch or migration. The UI and app hosts do not depend on those pricing terms.

## Verification

```powershell
node --test tests/feedback_submission_test.mjs
node tests/shell_back_policy_test.mjs
node web/feedback/build.mjs
# With Playwright available and Chrome installed:
node tests/feedback_page_browser_test.cjs
qmltestrunner -input tests/feedback_probe -o feedback-qml.txt,txt
```

`tests/feedback_android_compile` is a small Qt Gui CMake target for compiling the actual JNI/foreign-window owner with an Android Qt toolchain. Compile `FeedbackWebHost.java` and `FeedbackNavigationPolicy.java` against the Android SDK, then run `tests/android/FeedbackNavigationPolicyTest.java` on the JVM. These checks do not replace a packaged APK/device check.

Before release, verify the actual Windows build and Android APK: open from Home, send one real report, confirm the destination mailbox, exercise reload/offline errors, keyboard focus/Back, Android IME, rotation, minimize/resume, and repeated open/close. Test OnePlus 11R and Nokia T20 separately. Do not include passwords, account credentials, private paths, or real media history in test messages.

Official service references: [Formspark plans](https://www.formspark.io/pricing/), [Formspark limits](https://documentation.formspark.io/troubleshooting/limits-and-plans.html), [Formspark submission contract](https://documentation.formspark.io/examples/ajax.html), [Web3Forms pricing](https://web3forms.com/pricing), [Web3Forms submission contract](https://docs.web3forms.com/getting-started/api-reference).
