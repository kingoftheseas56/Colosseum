"""Verify local mock deliverables and write REPORT.json last, with evidence bounds."""
from pathlib import Path
import json
import struct
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent
CAP = ROOT / "captures"
PAGES = [
    ("account-profile", "Profile", ["default", "edit", "long"]),
    ("account-security", "Security", ["default", "approval", "password", "logout", "long"]),
    ("account-recovery", "Recovery", ["default", "replace", "long"]),
    ("account-privacy", "Data & privacy", ["default", "search", "activity", "delete", "long"]),
    ("account-devices", "Devices", ["default", "revoke", "long"]),
    ("account-your-colosseum", "Your Colosseum", ["default", "long"])
]
REQUIRED = {"portrait": (412, 900), "landscape": (900, 412), "desktop": (1280, 700)}

def png_size(path):
    if not path.is_file():
        return None
    with path.open("rb") as f:
        header = f.read(24)
    if len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return struct.unpack(">II", header[16:24])

# A valid post-fix 412 x 900 capture supersedes the earlier min-viewport
# screenshot, without ever destroying its target before the replacement exists.
replacements = []
for slug, _, _ in PAGES:
    fresh = CAP / (slug + ("-portrait-verified.png" if slug == "account-your-colosseum" else "-portrait-fixed.png"))
    target = CAP / (slug + "-portrait.png")
    if png_size(fresh) == (412, 900) and fresh.stat().st_size > 12000:
        shutil.copy2(fresh, target)
        replacements.append(slug)
for variant, expected in [("landscape", (900, 412)), ("desktop", (1280, 700)), ("long-small", (354, 792))]:
    verified = CAP / ("account-your-colosseum-" + variant + "-verified.png")
    if png_size(verified) == expected and verified.stat().st_size > 12000:
        shutil.copy2(verified, CAP / ("account-your-colosseum-" + variant + ".png"))
node = subprocess.run(["node", "--check", str(ROOT / "app.js")],
                      stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
javascript_pass = node.returncode == 0
source_assets = [
    ROOT / ".." / ".." / "reference" / "desktop" / "assets" / "icons" / "power.svg",
    ROOT / ".." / ".." / "reference" / "desktop" / "assets" / "fonts" / "Inter-Regular.otf",
    ROOT / ".." / ".." / "reference" / "local-fonts" / "segoeui.ttf"
]
assets_exist = all(p.exists() for p in source_assets)
index_text = (ROOT / "index.html").read_text(encoding="utf-8")
css_text = (ROOT / "style.css").read_text(encoding="utf-8")
phone_capture_frame_corrected = "body.capture .app-frame{position:fixed;inset:0}" in css_text
page_reports = []
missing = []
verified_total = 0
for slug, title, states in PAGES:
    html_file = ROOT / (slug + ".html")
    html = html_file.read_text(encoding="utf-8") if html_file.exists() else ""
    links_to_local_assets = 'href="style.css"' in html and 'src="app.js"' in html
    key = "your" if slug == "account-your-colosseum" else slug.replace("account-", "")
    correct_page = ('data-page="' + key + '"') in html
    if not links_to_local_assets or not correct_page or slug + ".html" not in index_text:
        missing.append("HTML/wiring/index: " + slug)
    page_captures = []
    for path in sorted(CAP.glob(slug + "-*.png")):
        dims = png_size(path)
        if not dims:
            continue
        label = path.stem[len(slug) + 1:]
        if label.endswith("-fresh") or label.endswith("-fixed") or label.endswith("-verified"):
            continue
        verified_total += 1
        page_captures.append({"file": "captures/" + path.name,
                              "viewportPx": str(dims[0]) + "x" + str(dims[1]),
                              "label": label})
    for label, expected in REQUIRED.items():
        actual = png_size(CAP / (slug + "-" + label + ".png"))
        if actual != expected:
            missing.append(slug + " " + label + ": expected " +
                           str(expected) + " got " + str(actual))
    page_reports.append({
        "file": slug + ".html", "title": title, "states": states,
        "viewports": ["412x900", "900x412", "354x792", "792x354",
                      "1280x700", "800x1280", "1280x800", "custom/insets/font"],
        "captures": page_captures
    })
if not javascript_pass:
    missing.append("node --check app.js: " + node.stderr[:250])
if not assets_exist:
    missing.append("source asset paths could not be verified")
if not phone_capture_frame_corrected:
    missing.append("capture frame 100vw correction not present")
report = {
    "task": "12-account-forms",
    "status": "ready-for-review" if not missing else "blocked",
    "sourceRefs": {
        "approvedDesktopCommit": "a40f66be06f22cdbe75e97e7dc4f1108ab2f5d41",
        "alphaPhoneRecoveryCommit": "77844eef0bd3b7fee2c1f884be4a76db6d63321d",
        "desktopQml": ["account/AccountCenter.qml", "account/AccountProfilePage.qml",
                       "account/AccountSecurityPage.qml", "account/AccountRecoveryPage.qml",
                       "account/AccountDataPrivacyPage.qml", "account/AccountDevicesPage.qml",
                       "account/AccountYourColosseumPage.qml", "account/AccountField.qml",
                       "account/AccountPageFrame.qml", "Theme.qml", "Main.qml"],
        "context": ["context/ANDROID-PORT-CLASSIFICATION.md", "context/VIEWPORTS.json"]
    },
    "pages": page_reports,
    "preservedDesign": [
        "Approved warm-dark Account Centre and original 232px desktop rail",
        "Alpha's existing compact horizontal account-section navigation and Back/sign-out row",
        "Gold/ink/glass/divider palette, Segoe form typography and Inter rail",
        "Original account fields, avatar vector geometry, icon glyph paths and inline buttons",
        "Source section order, full copy, inline confirmations and abstract monthly highlight marks"
    ],
    "fittingChanges": [
        "Stack long forms, hero copy and actions when content width is narrow",
        "Wrap the seven circular avatar choices without changing their selected artwork",
        "Shrink text columns beside source 38px glyphs without clipping",
        "Collapse password columns and privacy boundary lanes without hiding facts",
        "Keep phone navigation horizontally scrollable, with editable viewport, inset and font controls outside the mock",
        "Capture-only correction: size the app frame from the explicit logical preset, not headless Chrome's minimum viewport"
    ],
    "behaviourProposals": [],
    "unresolved": [
        "AccountYourColosseumPage.qml is not mounted in the inspected approved AccountCenter rail; live route ownership unverified",
        "OnePlus is disconnected: all logical viewport dimensions and insets provisional",
        "Native IME, QML runtime interactions, screenreader semantics, account backend and real Android behavior not verified",
        "Some historical small-portrait PNG captures predate the capture-width correction; use the current interactive 354px preset for fresh layout review"
    ] + missing,
    "verification": {
        "htmlPages": len(PAGES),
        "indexLinksAllSix": all(slug + ".html" in index_text for slug, _, _ in PAGES),
        "javascriptSyntax": "PASS node --check" if javascript_pass else "FAIL",
        "localAssets": "verified paths" if assets_exist else "missing asset paths",
        "captureMethod": "installed Chrome headless=new via PowerShell/isolated user-data-dir; original command in render_fallback.ps1, fixed portraits from refresh_portraits.ps1",
        "screenshotsPngHeaderAndSizeValidated": verified_total,
        "requiredPortraitLandscapeDesktop": "PASS" if not any("expected" in m for m in missing) else "INCOMPLETE",
        "correctedPortraitsPromoted": replacements,
        "visualReview": "Corrected 412x900 Profile and 1280x700 desktop screenshots inspected; app does not render from QML. Other screenshots are not a full adversarial accessibility audit.",
        "stateSafety": "No real credentials; all action handlers local only, no network/API/backend calls",
        "unverifiedBoundary": "HTML rendering only; no compiled or executed Colosseum binary, APK, ADB, UI integration, native runtime, production actions or release"
    }
}
# Complete evidence written last; JSON validity is checked again by caller after write.
out = ROOT / "REPORT.json"
out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print("REPORT_PATH=" + str(out))
print("STATUS=" + report["status"])
print("CAPTURES_VALIDATED=" + str(verified_total))
print("PORTRAITS_PROMOTED=" + ",".join(replacements))
print("MISSING=" + repr(missing))
