"""Resume only the observed post-create acceptance boundary from a saved failed run.

This is a UI-driven continuation, not a replay of claimed results. It checks the
persisted temporary identities from the original report and records new evidence.
"""
import argparse
import json
from pathlib import Path
from types import SimpleNamespace
import traceback

from verify_collections_app import Acceptance, row, keys, sha, state, require, memberships


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("root", "evidence", "prior", "cli"):
        p.add_argument("--" + name, required=True, type=Path)
    p.add_argument("--pipe", required=True)
    args = p.parse_args()
    prior = json.loads(args.prior.read_text(encoding="utf-8"))
    require(prior["status"] == "failed", "Continuation needs an explicitly failed boundary")
    accepted = {c["name"]: c for c in prior["checks"]}
    ids = accepted["membership Save/Cancel/Escape and atomic create-and-add"]["temporary"]
    require(len(ids) == 3, "Expected this run's three temporary collections")
    original = json.loads((args.prior.parent / "native-collections-before.json").read_text(encoding="utf-8"))
    options = SimpleNamespace(root=args.root, evidence=args.evidence, cli=args.cli, pipe=args.pipe,
        store=Path(prior["store"]), mode="core-continuation", primary="Across worlds", timeout=45, route=None)
    t = Acceptance(options)
    t.report["continuedFrom"] = str(args.prior)
    t.report["priorPassedChecks"] = list(accepted)
    try:
        t.client.call("window-set-state", {"state": "normal"})
        primary = row(original, "Across worlds")
        first = next(i["reference"] for i in primary["items"] if i["reference"]["route"] == "theatre-detail")
        second = next(i["reference"] for i in primary["items"] if i["reference"]["route"] == "manga-detail")
        a, b, c = ids
        a_name = row(state(t.store), a)["name"]
        add = t.title(primary["id"], second)
        t.picker(add)
        if a not in memberships(state(t.store), second["key"]):
            t.toggle_membership(a)
        t.click("pickerDone")
        t.prop("collectionsPanel", "pickerActive", False)
        t.collection(a)
        t.click("editCollectionButton")
        t.prop("collectionsPanel", "pendingChanges", True)
        staged = sha(t.store)
        note = 'kept note <one> & "two"'
        description = "An actual native-store acceptance collection"
        t.fill("collectionNameInput", a_name + " edited")
        t.fill("collectionDescriptionInput", description)
        t.fill("noteInput_" + first["key"], note)
        t.click("moveLater_" + first["key"])
        t.draft_order([second["key"], first["key"]])
        t.click("removeTitle_" + second["key"])
        t.click("undoRemoveTitle")
        t.draft_order([second["key"], first["key"]])
        t.unchanged(staged)
        t.click("saveCollectionButton")
        t.prop("collectionsPanel", "pendingChanges", False)
        changed = row(state(t.store), a)
        require(changed["name"] == a_name + " edited" and keys(changed) == [second["key"], first["key"]], "Name/order differs")
        require(changed["description"] == description and changed["items"][1]["note"] == note, "Description/note differs")
        t.screenshot("saved-name-notes-order")
        saved = sha(t.store)
        t.click("editCollectionButton")
        t.fill("collectionNameInput", "unsaved continuation")
        t.fill("noteInput_" + first["key"], "discard this note")
        t.click("moveEarlier_" + first["key"])
        t.click("cancelCollectionEdit")
        t.prop("collectionsPanel", "pendingChanges", False)
        t.unchanged(saved)
        t.click("editCollectionButton")
        t.fill("collectionNameInput", "discard on back continuation")
        t.click("backToCollections")
        t.prop("collectionPage", "leaving", True)
        t.prop("stayEditingButton", "activeFocus", True)
        t.client.key("Escape")
        t.prop("collectionPage", "leaving", False)
        t.prop("collectionsPanel", "pendingChanges", True)
        t.unchanged(saved)
        t.click("backToCollections")
        t.click("discardCollectionEdits")
        t.prop("collectionsPanel", "collectionId", "")
        t.unchanged(saved)
        t.check("draft note/order/name/description Save, removal Undo, Cancel and guarded Back")
        t.delete(b, check_cancel=True)
        t.delete(c)
        t.delete(a)
        require(sha(t.store) == prior["collectionsBefore"], "Original seed bytes were not restored")
        t.check("delete Cancel/confirm and exact original seed restoration")
        t.finish()
    except Exception as exc:
        t.report.update(status="failed", error=str(exc), traceback=traceback.format_exc())
        try:
            t.screenshot("continuation-failure")
        except Exception as capture_error:
            t.report["captureError"] = str(capture_error)
    finally:
        t.report.update(collectionsAfter=sha(t.store), protectedAfter=t.protected())
        t.report["protectedUnchanged"] = t.protected() == prior["protectedBefore"]
        (t.evidence / "core-continuation.json").write_text(json.dumps(t.report, indent=2), encoding="utf-8")
    print(json.dumps({"status": t.report["status"], "report": str(t.evidence / "core-continuation.json")}))
    return 0 if t.report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
