const params = MockUI.params,
  main = document.getElementById("main");
let section = params.section || "profile",
  mode = params.mode || "center",
  avatar = MockUI.get("account.avatar", "Laurel"),
  username = MockUI.get("account.username", "demo_colosseum"),
  devices = MockUI.get("account.devices", ["Demo desktop", "Demo laptop"]),
  pending = MockUI.get("account.pending", true),
  signed = MockUI.get("account.signed", true),
  keySaved = false;
const glyphs = {
    Laurel: "❧",
    Column: "▥",
    Book: "▱",
    Screen: "▣",
    Panels: "▤",
    Initial: "D",
  },
  labels = {
    profile: "Profile",
    feria: "Feria",
    security: "Security",
    devices: "Devices",
    recovery: "Recovery",
    privacy: "Data & privacy",
  };
function symbol(a) {
  let icons = {
    Laurel: "your-colosseum-laurel",
    Book: "book-library",
    Screen: "projector-theatre",
    Panels: "comic-book",
  };
  return icons[a]
    ? `<img src="../assets/icons/${icons[a]}.svg" alt="">`
    : glyphs[a];
}
function sectionBlock(title, copy, controls) {
  return `<section class="section"><div class="row spread"><div><h3>${title}</h3><p>${copy}</p></div></div>${controls || ""}</section>`;
}
function center() {
  mode = "center";
  document.querySelector(".wall").hidden = true;
  main.innerHTML = `<aside class="rail"><div class="identity"><div class="initial">${signed ? "D" : "?"}</div><div><b>${signed ? U.e(username) : "Not signed in"}</b><small style="display:block;margin-top:4px">Colosseum account</small></div></div><nav aria-label="Account Centre sections">${Object.entries(
    labels,
  )
    .map(([id, label], i) =>
      U.btn(
        `<span>${["◌", "▣", "◇", "▣", "↶", "◫"][i]}</span>${label}`,
        "section-" + id,
        id === section ? "selected" : "",
      ),
    )
    .join(
      "",
    )}</nav><div class="foot">${U.btn(signed ? "Sign out of Colosseum" : "Sign in to Colosseum", "session", "link")}${U.btn("‹ Back", "back", "link")}</div></aside><div class="account-content"><h1>${labels[section]}</h1><div id="account-body"></div><div id="status" class="status" role="status"></div></div>`;
  let b = document.getElementById("account-body");
  if (section === "profile")
    b.innerHTML = `<div class="profile-hero"><div class="medallion">${symbol(avatar)}</div><div><h2 style="font-size:31px">${U.e(username)}</h2><p style="font-size:12px;margin:8px 0 23px">Colosseum account</p><small>✓ &nbsp; ${avatar} medallion</small></div></div><section class="section"><div class="row spread"><h3>Avatar</h3><span class="badge">BUILT-IN</span></div><div class="avatar-grid">${Object.keys(
      glyphs,
    )
      .map((a) =>
        U.btn(
          `<span class="symbol">${symbol(a)}</span>${a}`,
          "avatar-" + a,
          a === avatar ? "selected" : "",
        ),
      )
      .join(
        "",
      )}<button disabled>Custom<br><small>Later</small></button></div></section><section class="section"><div style="max-width:490px">${U.field("Username", "username")}<div class="actions">${U.btn("Save changes", "save-profile", "primary")}</div></div></section>`;
  if (section === "security")
    b.innerHTML = `${pending ? `<section class="pending"><div class="row spread"><div><h3>New sign-in waiting for approval</h3><p>A Colosseum sign-in is waiting for your decision.</p></div><span class="badge">New device</span></div><div class="row spread" style="margin-top:18px"><span>Demo notebook · Colosseum sign-in</span><div class="row">${U.btn("Deny", "deny-device")}${U.btn("Approve", "approve-device", "primary")}</div></div></section>` : ""}${sectionBlock("New-device protection", "New sign-ins need approval from a trusted Colosseum device, or the recovery key.", U.toggle("Protection is on", "protection", MockUI.get("account.protection", true)))}${sectionBlock("Password", "Change the password used to sign in to this Colosseum account.", `<div class="actions">${U.btn("Change", "password")}</div>`)}${sectionBlock("Sign out everywhere", "End every Colosseum account session, including this one.", `<div class="actions">${U.btn("Sign out everywhere", "signout-all", "danger")}</div>`)}`;
  if (section === "devices")
    b.innerHTML = `<div class="row spread"><p>${devices.length} trusted devices</p>${U.btn("Refresh", "refresh-devices")}</div><p style="margin:20px 0">Revoking a device ends its account access. It can sign in again later if you approve it.</p><h3>Trusted devices</h3>${devices.length ? devices.map((d, i) => `<div class="device-row row spread"><div><h3>${d}</h3><p>${i === 0 ? "This device · last seen now" : "last seen 1 day ago"}</p></div>${U.btn("Revoke", "revoke-" + i)}</div>`).join("") : '<div class="empty">No trusted devices were returned by the account service.</div>'}`;
  if (section === "recovery")
    b.innerHTML = `${sectionBlock("Your way back in.", "Your recovery key can reset your password if you lose access. Colosseum showed the current key once when it was created and does not keep it available for reveal here.", '<p style="margin-top:18px;color:#f0df9a">Only the newest recovery key works.</p>')}${sectionBlock("Replace recovery key", "Generate a new recovery key if the old one is lost, exposed, or no longer somewhere you trust.", `<div class="actions">${U.btn("Replace key", "replace-key")}</div>`)}${sectionBlock("If you lose your password", "Sign out and choose account recovery. Your username, recovery key, and a new password are enough to recover the account.")}`;
  if (section === "privacy")
    b.innerHTML = `<h3>History</h3>${sectionBlock("Search history", "Remember searches on this device. Search history never needs to leave this machine.", U.toggle("Remember searches on this device", "remember", MockUI.get("account.remember", true)) + U.btn("Clear history", "clear-search"))}${sectionBlock("Your Colosseum activity", "Keep watch and reading activity for Monthly Portrait, recent activity, completion history and personal media insights.", U.toggle("Keep watch and reading activity", "activity", MockUI.get("account.activity", true)) + U.btn("Clear activity", "clear-activity"))}${sectionBlock("Cross-device history", "Sync watch and reading history between your signed-in devices. Turning this off can still leave activity available locally.", U.toggle("Sync watch and reading history", "history", MockUI.get("account.history", true)))}${sectionBlock("Your account data", "Create a portable copy of the account data Colosseum holds for you.", `<h3>Export my data</h3><div class="actions">${U.btn("Request export", "export")}</div>`)}${sectionBlock("What Colosseum keeps where", "Portable account state can follow you, while machine-specific and sensitive data stays outside ordinary sync.", `<div class="grid" style="grid-template-columns:1fr 1fr;margin-top:22px"><div><h3>Can follow your account</h3><p>Continue progress<br>Collection<br>Your Colosseum activity<br>Portable preferences</p></div><div><h3>Stays on this device</h3><p>Search history<br>Window & session state<br>Paths & media<br>Downloads & caches</p></div></div><p style="margin-top:22px">Secrets are a separate boundary. Passwords, recovery keys, session tokens and API credentials do not enter ordinary account sync.</p>`)}${sectionBlock("Delete Colosseum account", "Permanently delete the account and its server-side account data. Local media files on your devices are not part of account deletion.", `<div class="actions">${U.btn("Delete account", "delete-account", "danger")}</div>`)}`;
  if (section === "feria")
    b.innerHTML = `<h2>Apps and services</h2><p style="margin:18px 0">Feria settings and activity are managed separately from your Colosseum account.</p><div class="actions"><button data-go="feria.account">Open Feria account</button><button data-go="feria.activity">Your Feria activity</button></div>`;
  let field = document.getElementById("username");
  if (field) field.value = username;
  MockUI.ready();
}
const authInfo = {
  welcome: [
    "Welcome to Colosseum.",
    "Use Colosseum completely locally, or sign in to carry your Collection, progress, reader setup, preferences, and other personal choices between devices.",
    "How do you want to begin?",
    "An account is optional. You can change your mind later.",
  ],
  signin: [
    "Welcome back.",
    "Sign in to restore your Collection, progress, reader setup, preferences, and other synced Colosseum state on this device.",
    "Enter your account",
    "Use your Colosseum username and password.",
  ],
  create: [
    "Create your account.",
    "Keep your Collection, progress, reader setup, preferences, and other personal Colosseum state with you across devices.",
    "Choose your login",
    "No email or phone number required.",
  ],
  recover: [
    "Recover your account.",
    "Use your username and recovery key to set a new password.",
    "Reset your password",
    "Your recovery key is the way back to your account.",
  ],
  approval: [
    "Approve this device.",
    "A signed-in Colosseum device can approve this sign-in.",
    "Waiting for approval",
    "Approve this device from another signed-in Colosseum desktop, or use your recovery key.",
  ],
  key: [
    "Keep your recovery key.",
    "Your way back in if you lose your password.",
    "Save your recovery key",
    "Shown once. Save the new key before leaving this screen.",
  ],
};
function auth(next) {
  U.clear();
  mode = next;
  document.querySelector(".wall").hidden = false;
  const a = authInfo[mode] || authInfo.welcome;
  main.innerHTML = `<div class="auth-back">${U.btn("‹ Back", "back", "link")}</div><section class="auth-intro"><div class="kicker">COLOSSEUM · ${mode === "welcome" ? "WELCOME" : "ACCOUNT"}</div><h1>${a[0]}</h1><p>${a[1]}</p></section><section class="auth-panel"><div class="kicker">${mode === "signin" ? "SIGN IN" : mode === "create" ? "CREATE ACCOUNT" : "COLOSSEUM"}</div><h2>${a[2]}</h2><p>${a[3]}</p><div class="form" id="auth-form"></div><div id="status" class="status" role="status"></div></section>`;
  let f = document.getElementById("auth-form");
  if (mode === "welcome")
    f.innerHTML =
      [
        [
          "local",
          "Continue without account",
          "Keep everything local on this device.",
        ],
        ["auth-signin", "Sign in", "Bring your Colosseum back on this device."],
        [
          "auth-create",
          "Create account",
          "Start syncing your personal Colosseum.",
        ],
      ]
        .map(([id, t, c]) =>
          U.btn(t + "<small>" + c + "</small>", id, "choice"),
        )
        .join("") +
      '<p style="margin-top:25px">Accounts are optional. Sign in later whenever you want sync.</p>';
  if (["signin", "create", "recover"].includes(mode))
    f.innerHTML =
      U.field("Username", "auth-user") +
      (mode === "recover" ? U.field("Recovery key", "auth-key", true) : "") +
      U.field(
        mode === "recover" ? "New password" : "Password",
        "auth-pass",
        true,
      ) +
      (mode !== "signin"
        ? U.field(
            mode === "recover" ? "Confirm new password" : "Confirm password",
            "auth-confirm",
            true,
          )
        : U.btn("Forgot password?", "auth-recover", "link")) +
      `<div class="actions">${U.btn(mode === "signin" ? "Sign in" : mode === "create" ? "Create account" : "Reset password", "auth-submit", "primary")}${U.btn(mode === "signin" ? "Create account" : "Back to sign in", mode === "signin" ? "auth-create" : "auth-signin")}${U.btn("Continue without account", "local", "link")}</div><small style="display:block;margin-top:18px">DEMO only. Use any sample text; never enter real account details.</small>`;
  if (mode === "approval")
    f.innerHTML = `<div class="pending"><p>Waiting for another device…</p><p>Demo notebook · DEMO request</p></div><div class="actions">${U.btn("Check again", "approval-check")}</div><div style="margin-top:24px">${U.field("Recovery key", "auth-key", true)}</div><div class="actions">${U.btn("Use recovery key", "approval-key", "primary")}${U.btn("Back to sign in", "auth-signin", "link")}</div>`;
  if (mode === "key")
    f.innerHTML = `<div class="badge" style="display:inline-block;margin:5px 0 22px">Shown once · DEMO KEY</div><div class="key">DEMO-ONLY-NOT-A-REAL-KEY<br>COLOSSEUM-SAMPLE-0000</div><div class="actions">${U.btn("Copy recovery key", "copy-key")}${U.btn("I saved it", "saved-key", "primary")}</div><small style="display:block;margin-top:24px">Generating a new one kills the old key. This is an inert fixture.</small>`;
  MockUI.ready();
}
function secretModal(title, copy, fields, action, label) {
  U.modal(
    title,
    `<p>${copy}</p><p class="status">DEMO form. Never enter a real password.</p>${fields.map((x) => U.field(x[0], x[1], true)).join("")}`,
    U.btn(label, action, "primary"),
  );
}
window.handleAction = (a, b) => {
  if (a.startsWith("section-")) {
    section = a.slice(8);
    center();
    return;
  }
  if (a.startsWith("auth-") && a !== "auth-submit") {
    auth(a.slice(5));
    return;
  }
  if (a.startsWith("avatar-")) {
    avatar = a.slice(7);
    MockUI.set("account.avatar", avatar);
    center();
    U.status("Avatar updated in this demo.");
    return;
  }
  if (a === "save-profile") {
    let v = document.getElementById("username").value.trim();
    if (!/^[a-zA-Z0-9_]{3,24}$/.test(v)) {
      U.status("Use 3–24 letters, numbers or underscores.");
      return;
    }
    username = v;
    MockUI.set("account.username", v);
    center();
    U.status("Profile updated. Demo only.");
  }
  if (a === "session") {
    if (!signed) {
      auth("signin");
      return;
    }
    U.modal(
      "Sign out of Colosseum?",
      `<p>${params.pendingSync ? "Syncing — 2 changes pending. These fixture changes have not been synced." : "All changes synced."}</p>`,
      U.btn("Sign out", "confirm-signout", "danger"),
    );
  }
  if (a === "confirm-signout" || a === "confirm-signout-all") {
    U.close();
    signed = false;
    MockUI.set("account.signed", false);
    auth("signin");
  }
  if (a === "password")
    secretModal(
      "Change password",
      "Use 8–128 characters.",
      [
        ["Current password", "current"],
        ["New password", "new"],
        ["Confirm new password", "confirm"],
      ],
      "save-password",
      "Save new password",
    );
  if (a === "save-password") {
    let n = document.getElementById("new").value,
      c = document.getElementById("confirm").value;
    if (n.length < 8 || n.length > 128 || n !== c) {
      document.getElementById("modal-status").textContent =
        n !== c ? "Those passwords do not match." : "Use 8–128 characters.";
      return;
    }
    U.close();
    U.status(
      "Demo password change completed. Typed fields cleared; no password saved.",
    );
  }
  if (a === "approve-device" || a === "deny-device") {
    pending = false;
    MockUI.set("account.pending", false);
    center();
    U.status(
      a === "approve-device"
        ? "Demo sign-in approved."
        : "Demo sign-in denied.",
    );
  }
  if (a === "signout-all")
    U.modal(
      "Sign out everywhere",
      "<p>Every signed-in device will need to sign in again.</p>",
      U.btn("Confirm sign out", "confirm-signout-all", "danger"),
    );
  if (a === "refresh-devices") U.status("Trusted demo devices refreshed.");
  if (a.startsWith("revoke-")) {
    let i = +a.slice(7);
    U.modal(
      "Revoke " + devices[i] + "?",
      `<p>It will lose account access and need to sign in again.</p>`,
      U.btn("Confirm revoke", "confirm-revoke-" + i, "danger"),
    );
  }
  if (a.startsWith("confirm-revoke-")) {
    devices.splice(+a.slice(15), 1);
    MockUI.set("account.devices", devices);
    U.close();
    center();
    U.status("Demo device revoked.");
  }
  if (a === "replace-key")
    secretModal(
      "The old key will stop working.",
      "After replacement, save the new key before leaving the one-time screen.",
      [["Current password", "current"]],
      "generate-key",
      "Generate new key",
    );
  if (a === "generate-key") {
    if (!document.getElementById("current").value) {
      document.getElementById("modal-status").textContent =
        "Enter a sample current password.";
      return;
    }
    U.close();
    auth("key");
  }
  if (a === "copy-key")
    U.status(
      "Demo key selected for review. Clipboard access is intentionally simulated.",
    );
  if (a === "saved-key") {
    signed = true;
    MockUI.set("account.signed", true);
    section = "recovery";
    center();
    U.status("Demo key acknowledged.");
  }
  if (a === "clear-search" || a === "clear-activity")
    U.modal(
      a === "clear-search"
        ? "Clear search history on this device?"
        : "Clear Your Colosseum activity?",
      `<p>${a === "clear-search" ? "Clear remembered searches from this device?" : "Clear your stored watch and reading activity? Continue progress and Collection are not included."}</p>`,
      U.btn(
        a === "clear-search"
          ? "Clear search history"
          : "Clear activity history",
        "confirm-" + a,
        "danger",
      ),
    );
  if (a.startsWith("confirm-clear-")) {
    MockUI.set(
      "account." +
        (a.endsWith("activity") ? "activityCleared" : "searchCleared"),
      true,
    );
    U.close();
    U.status("Demo history cleared. Continue and Collection are unchanged.");
  }
  if (a === "export")
    U.modal(
      "Export my data",
      '<p>Create a portable copy of the account data Colosseum holds for you.</p><p class="status">Demo export ready: profile, collection, progress and portable preferences. No real account data was read.</p>',
      U.btn("Cancel export", "close") +
        U.btn("Review demo export", "review-export", "primary"),
    );
  if (a === "review-export")
    U.modal(
      "Demo account export",
      '<div class="key">Profile: demo_colosseum<br>Collection: 3 fixture titles<br>Progress: 2 fixture resume points<br>Preferences: 3 fixture settings</div><p>No credentials, paths or real personal data are included.</p>',
    );
  if (a === "delete-account")
    secretModal(
      "Delete Colosseum account",
      "This is permanent. Deleting the account removes its server-side identity and portable cloud data. Local media files stay on your devices. Provider backups follow the provider’s retention policy; Colosseum keeps only a capability-protected retry receipt for up to 30 days.",
      [["Current password", "current"]],
      "delete-reauth",
      "Continue to delete",
    );
  if (a === "delete-reauth") {
    U.clear();
    U.close();
    U.modal(
      "Colosseum account deletion",
      "<p>Demo re-authentication completed. Confirm removal of the demo account state?</p>",
      U.btn("Delete demo account", "confirm-delete", "danger"),
    );
  }
  if (a === "confirm-delete") {
    signed = false;
    MockUI.set("account.signed", false);
    U.close();
    auth("welcome");
    U.status("Demo account removed. No external account was affected.");
  }
  if (a === "local") {
    U.clear();
    signed = false;
    MockUI.set("account.signed", false);
    MockUI.set("account.localOnly", true);
    MockUI.go("home");
  }
  if (a === "auth-submit") {
    let user = document.getElementById("auth-user").value.trim(),
      pass = document.getElementById("auth-pass").value,
      conf = document.getElementById("auth-confirm");
    if (!user || !pass) {
      U.status("Enter a sample username and password.");
      return;
    }
    if (conf && (pass.length < 8 || conf.value !== pass)) {
      U.status("Use 8–128 characters and matching passwords.");
      return;
    }
    if (mode === "recover" && !document.getElementById("auth-key").value) {
      U.status("Enter a sample recovery key.");
      return;
    }
    let next = mode === "signin" ? "approval" : "key";
    U.clear();
    auth(next);
  }
  if (a === "approval-check") {
    U.status(
      "Still waiting for the demo device. Use a sample recovery key to continue.",
    );
  }
  if (a === "approval-key") {
    if (!document.getElementById("auth-key").value) {
      U.status("Enter a sample recovery key.");
      return;
    }
    U.clear();
    signed = true;
    MockUI.set("account.signed", true);
    section = "profile";
    center();
    U.status("Demo sign-in complete.");
  }
};
document.addEventListener("change", (e) => {
  if (e.target.dataset.toggle) {
    MockUI.set("account." + e.target.dataset.toggle, e.target.checked);
    U.status("Preference changed for this staged session.");
  }
});
if (mode === "center") center();
else auth(mode);
