"""Real-app failure, profile, and restart acceptance in a marked test root only.

The only file fault is a temporary Windows read handle that denies replacement of
this fixture's Collections file. Profile switches use the compiled, marked
CollectionsTestProfileController; all collection mutations use visible UI.
"""
import argparse
import ctypes
import json
from pathlib import Path
from types import SimpleNamespace
import traceback

from drive_app import Candidate
from verify_collections_app import Acceptance, PROFILE_IDS, require, row, state, sha, keys

NAME = "Lifecycle persistence proof"
NOTE = 'Restart keeps this note: <one> & "two".'
DESCRIPTION = "Local Collections: exact identities, order and notes survive restart."


def options(args, mode="lifecycle"):
    return SimpleNamespace(root=args.root, evidence=args.evidence, cli=args.cli,
        pipe=args.pipe, store=args.root / "profiles/local/named-collections-v2.json",
        mode=mode, primary="Across worlds", timeout=45, route=None)


def profile(client, action, count=None):
    reply = client.call("collections-test-profile", {"action": action})
    require(reply.get("success"), "Native profile activation refused: " + str(reply))
    require(reply.get("collectionsHealthy"), "Collections profile is unhealthy")
    if count is not None:
        require(reply.get("collectionsCount") == count, "Unexpected profile collection count")
    return reply


def create_in_profile(client, name):
    client.click("taskbarCollectionsAction")
    client.wait("overviewPage", "visible", True)
    client.click("newCollectionButton")
    client.wait("collectionNameField", "activeFocus", True)
    client.call("ui-text-input", {"target": "collectionNameField", "text": name})
    client.click("createAndAddButton")
    client.wait("collectionsPanel", "pickerActive", False)


def before_restart(args, t):
    t.client.call("window-set-state", {"state": "normal"})
    primary = row(state(t.store), "Across worlds")
    first = next(i["reference"] for i in primary["items"] if i["reference"]["route"] == "theatre-detail")
    second = next(i["reference"] for i in primary["items"] if i["reference"]["route"] == "book-detail")
    require(not any(c["name"] == NAME for c in state(t.store)["collections"]), "Do not overwrite an earlier lifecycle run")
    t.overview()
    identifier = t.create_empty(NAME)
    for reference in (first, second):
        add = t.title(primary["id"], reference)
        t.picker(add)
        t.toggle_membership(identifier)
        t.click("pickerDone")
        t.prop("collectionsPanel", "pickerActive", False)
    t.collection(identifier)
    t.click("editCollectionButton")
    t.fill("collectionDescriptionInput", DESCRIPTION)
    t.fill("noteInput_" + first["key"], NOTE)
    t.click("moveLater_" + first["key"])
    t.draft_order([second["key"], first["key"]])
    before_bytes, revision = t.store.read_bytes(), t.profile_status()["collectionsRevision"]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_ulong, ctypes.c_ulong,
                                  ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p]
    kernel.CreateFileW.restype = ctypes.c_void_p
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.CreateFileW(str(t.store), 0x80000000, 3, None, 3, 0, None)
    require(handle not in (None, ctypes.c_void_p(-1).value), "Could not establish isolated save-failure fault")
    try:
        t.click("saveCollectionButton")
        t.prop("collectionsPanel", "pendingChanges", True)
        message = t.until(lambda: t.props("collectionPage", "errorText").get("errorText"), "visible failed-save error")
        require(t.store.read_bytes() == before_bytes, "Failed save changed stored bytes")
        require(t.profile_status()["collectionsRevision"] == revision, "Failed save claimed a successful revision")
        t.screenshot("failed-save-visible-error")
        t.check("failed native atomic replacement: visible error, draft retained, bytes/revision unchanged", message=message)
    finally:
        require(kernel.CloseHandle(handle), "Could not release test-only failure handle")
    t.click("saveCollectionButton")
    t.prop("collectionsPanel", "pendingChanges", False)
    saved = row(state(t.store), identifier)
    require(saved["description"] == DESCRIPTION and keys(saved) == [second["key"], first["key"]], "Successful retry lost description/order")
    require(saved["items"][1]["note"] == NOTE, "Successful retry lost exact note")
    t.screenshot("saved-lifecycle-collection")
    local_bytes = t.store.read_bytes()
    t.click("editCollectionButton")
    t.fill("collectionNameInput", "Uncommitted local draft")
    profile(t.client, "activate-a", 0)
    require(t.store.read_bytes() == local_bytes, "Switch committed a local draft")
    for property_name in ("showing", "pickerActive", "pendingChanges", "deleteActive"):
        t.prop("collectionsPanel", property_name, False)
    create_in_profile(t.client, "Only profile A")
    a_path = args.root / "profiles" / PROFILE_IDS["A"] / "named-collections-v2.json"
    a_bytes = a_path.read_bytes()
    require([c["name"] for c in state(a_path)["collections"]] == ["Only profile A"], "Profile A leaked local collections")
    t.client.click("newCollectionButton")
    t.client.call("ui-text-input", {"target": "collectionNameField", "text": "Uncommitted A popup"})
    profile(t.client, "activate-b", 0)
    t.prop("collectionsPanel", "pickerActive", False)
    require(a_path.read_bytes() == a_bytes, "Profile switch committed A's popup")
    create_in_profile(t.client, "Only profile B")
    b_path = args.root / "profiles" / PROFILE_IDS["B"] / "named-collections-v2.json"
    b_bytes = b_path.read_bytes()
    require([c["name"] for c in state(b_path)["collections"]] == ["Only profile B"], "Profile B leaked A's collection")
    profile(t.client, "activate-a", 1)
    require(a_path.read_bytes() == a_bytes and b_path.read_bytes() == b_bytes, "Profile files changed on reactivation")
    profile(t.client, "activate-local", len(state(t.store)["collections"]))
    require(t.store.read_bytes() == local_bytes, "Local Collections changed during A/B switching")
    t.collection(identifier)
    t.screenshot("local-restored-after-profile-switch")
    t.check("real profile-owner swaps isolate A/B/local and discard draft/modal state", profiles=["local", "A", "B"])
    receipt = {"collection": saved, "localSha256": sha(t.store), "aSha256": sha(a_path),
               "bSha256": sha(b_path), "protected": t.protected()}
    (args.evidence / "restart-expected.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    require(t.protected() == t.protected_before, "Membership/editing/profile proof changed Library/progress files")
    t.check("Library and progress preserved throughout the lifecycle proof")
    t.report["status"] = "passed"


def after_restart(args, t):
    expected = json.loads(args.expected.read_text(encoding="utf-8"))
    profile(t.client, "activate-local")
    require(sha(t.store) == expected["localSha256"], "Local Collections bytes changed across process restart")
    saved = row(state(t.store), expected["collection"]["id"])
    require(saved == expected["collection"], "Saved identity/description/order/note did not survive restart")
    t.client.call("window-set-state", {"state": "normal"})
    t.collection(saved["id"])
    t.screenshot("collection-after-process-restart")
    t.click("editCollectionButton")
    t.prop("collectionDescriptionInput", "text", DESCRIPTION)
    t.prop("noteInput_" + saved["items"][1]["reference"]["key"], "text", NOTE)
    t.draft_order(keys(saved))
    t.click("cancelCollectionEdit")
    t.prop("collectionsPanel", "pendingChanges", False)
    for alias in ("A", "B"):
        profile(t.client, "activate-" + alias.lower(), 1)
        path = args.root / "profiles" / PROFILE_IDS[alias] / "named-collections-v2.json"
        require(sha(path) == expected[alias.lower() + "Sha256"], alias + " changed across process restart")
    profile(t.client, "activate-local")
    t.collection(saved["id"])
    require(t.protected() == expected["protected"], "Restart changed Library/progress during Collections acceptance")
    t.check("separate process restart retains exact identities, notes, order, description and A/B isolation")
    t.report["status"] = "passed"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("root", "evidence", "cli"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--pipe", required=True)
    parser.add_argument("--phase", choices=("before", "after"), required=True)
    parser.add_argument("--expected", type=Path)
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    if args.phase == "before":
        # Establish the baseline after normal first-use profile teardown has
        # materialized empty ProgressStore files, not before that lifecycle work.
        client = Candidate(args.cli, args.pipe, args.evidence / "initialization")
        profile(client, "activate-a", 0)
        profile(client, "activate-b", 0)
        profile(client, "activate-local")
    if args.phase == "after":
        require(args.expected is not None, "Restart receipt required")
        client = Candidate(args.cli, args.pipe, args.evidence)
        profile(client, "activate-local")
    t = Acceptance(options(args))
    try:
        before_restart(args, t) if args.phase == "before" else after_restart(args, t)
    except Exception as error:
        t.report.update(status="failed", error=str(error), traceback=traceback.format_exc())
        try:
            t.screenshot("lifecycle-failure")
        except Exception as capture_error:
            t.report["captureError"] = str(capture_error)
    finally:
        t.report.update(collectionsAfter=sha(t.store), protectedAfter=t.protected())
        path = args.evidence / ("lifecycle-" + args.phase + ".json")
        path.write_text(json.dumps(t.report, indent=2), encoding="utf-8")
    print(json.dumps({"status": t.report["status"], "report": str(path)}))
    return 0 if t.report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
