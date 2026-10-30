"""Adversarial stored-input checks, contained to the existing marked A fixture.

All injected bytes are restored in finally, while A is inactive. Native status,
actual disabled controls, visible screenshots and exact file hashes are evidence.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
from types import SimpleNamespace
import traceback

from drive_app import Candidate
from verify_collections_app import Acceptance, PROFILE_IDS, fixture_root, require, state, row, sha


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("root", "evidence", "cli"):
        p.add_argument("--" + name, required=True, type=Path)
    p.add_argument("--pipe", required=True)
    a = p.parse_args()
    root = fixture_root(a.root)
    c = Candidate(a.cli, a.pipe, a.evidence)
    path = root / "profiles" / PROFILE_IDS["A"] / "named-collections-v2.json"
    require(path.is_file() and not path.is_symlink(), "Existing A fixture required")
    original = path.read_bytes()
    document = json.loads(original)
    require([r["name"] for r in document["collections"]] == ["Only profile A"], "Unexpected A fixture data")
    report = {"status": "running", "checks": [], "originalSha256": sha(path)}
    (a.evidence / "profile-a-original.json").write_bytes(original)
    def activate(action):
        result = c.call("collections-test-profile", {"action": action})
        require(result.get("success"), "Profile switch refused: " + str(result))
        return result
    try:
        c.call("window-set-state", {"state": "normal"})
        for label, bad in (("malformed", b'{"schemaVersion":2,"collections":['),
                           ("unsupported-schema", b'{"schemaVersion":999,"collections":[]}')):
            activate("activate-local")
            path.write_bytes(bad)
            observed = activate("activate-a")
            require(observed["collectionsHealthy"] is False, "Invalid input was silently accepted")
            c.click("taskbarCollectionsAction")
            c.wait("overviewPage", "visible", True)
            require(c.props("newCollectionButton", "enabled")["enabled"] is False, "Creation remained enabled over invalid data")
            screenshot = c.screenshot(label + "-preserved")
            require(path.read_bytes() == bad, "Opening invalid data replaced original bytes")
            report["checks"].append({"name": label, "status": "passed", "observed": observed,
                                     "sha256": hashlib.sha256(bad).hexdigest(), "screenshot": screenshot})
            activate("activate-local")
            require(path.read_bytes() == bad, "Leaving invalid profile altered its data")
            path.write_bytes(original)
        activate("activate-local")
        local = state(root / "profiles/local/named-collections-v2.json")
        ref = copy.deepcopy(row(local, "Across worlds")["items"][0]["reference"])
        ref["route"] = "future-provider-detail"
        future = copy.deepcopy(document)
        future["collections"][0]["items"] = [{"reference": ref, "note": "Keep unresolved data; never substitute by title."}]
        injected = json.dumps(future, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        path.write_bytes(injected)
        observed = activate("activate-a")
        require(observed["collectionsHealthy"] is True, "A bounded unresolved reference should remain manageable")
        t = Acceptance(SimpleNamespace(root=root, evidence=a.evidence / "unresolved", cli=a.cli, pipe=a.pipe,
              store=path, mode="unresolved", primary="Only profile A", timeout=30, route=None))
        result = t.open_member(future["collections"][0]["id"], ref)
        require(result["status"] == "unavailable" and result.get("message"), "Unresolved item lacked a recoverable explanation")
        require(path.read_bytes() == injected, "Unresolved item was changed or discarded")
        t.screenshot("unresolved-member-retained")
        report["checks"].append({"name": "unresolved future route is retained with explanation", "status": "passed", "observed": result})
        report["status"] = "passed"
    except Exception as error:
        report.update(status="failed", error=str(error), traceback=traceback.format_exc())
        try:
            report["failureScreenshot"] = c.screenshot("recovery-failure")
        except Exception as grab:
            report["captureError"] = str(grab)
    finally:
        # Do not restore under an active owner: switch out before restoring bytes.
        inactive = activate("activate-local")
        path.write_bytes(original)
        require(path.read_bytes() == original, "Original A bytes were not restored")
        report["restoredSha256"] = sha(path)
        healthy = activate("activate-a")
        require(healthy["collectionsHealthy"] and healthy["collectionsCount"] == 1, "Restored profile did not recover")
        activate("activate-local")
        report["restoredHealthy"] = True
        (a.evidence / "recovery-acceptance.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"status": report["status"], "report": str(a.evidence / "recovery-acceptance.json")}))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
