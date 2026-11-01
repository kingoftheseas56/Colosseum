"""Observed acceptance checks for an already-running, isolated Colosseum candidate.

Run core and routes separately. This file never writes a profile/store file and
never invokes arbitrary QML/native methods. Core mutations use the real UI, only
create/delete this run's temporary collections, and must restore the seed bytes.
Routes opens exact persisted references; unavailable is reported as partial, not
passed. The three core screenshots are actual window grabs from Lanista.

Selectors below come from staged qml/collections, Taskbar and actual title owners.
Review/adaptation points: --store if both legacy/local files exist; --primary if
the seed collection was renamed; ROUTES if title owners acquire new objectNames.
No assumptions about absolute coordinates, collection UUIDs or canonical keys.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import re
import time
import traceback

from drive_app import Candidate


ROUTES = {  # route: (actual owner landmark, Add wrapper, optional Vinyl page)
    "theatre-detail": ("theatreSeriesPage", "theatreAddToCollections", ""),
    "manga-detail": ("mangaSeriesPage", "mangaAddToCollections", ""),
    "manga-weebcentral-detail": ("mangaSeriesPage", "weebcentralAddToCollections", ""),
    "comic-locg-detail": ("", "comicAddToCollections", ""),
    "comic-gcd-detail": ("", "comicAddToCollections", ""),
    "comic-posts-detail": ("", "comicAddToCollections", ""),
    "comic-tag-detail": ("", "comicAddToCollections", ""),
    "comic-pack-detail": ("", "comicAddToCollections", ""),
    "book-detail": ("biblioBookDetail", "bookAddToCollections", ""),
    "vault-film-detail": ("vaultBrowseSheet", "vaultAddToCollections", ""),
    "vinyl-album-detail": ("vinylAlbumPage", "vinylAlbumAddToCollections", "album"),
    "vinyl-song-detail": ("vinylSongPage", "vinylSongAddToCollections", "song"),
    "vinyl-artist-detail": ("vinylArtistPage", "vinylArtistAddToCollections", "artist"),
}
PROFILE_IDS = {"A": "11111111-1111-4111-8111-111111111181",
               "B": "11111111-1111-4111-8111-111111111182"}


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def sha(path: Path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def write_evidence(path: Path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def linked(path: Path):
    return path.is_symlink() or (hasattr(path, "is_junction") and path.is_junction())


def fixture_root(path: Path):
    path = path.absolute()
    require(all(not linked(p) for p in (path, *path.parents)), "Fixture root contains a link/junction")
    require(re.fullmatch(r"Colosseum-dltest-collections-fixture-[A-Za-z0-9_-]{1,48}", path.name),
            "--root must identify the explicitly tagged Collections fixture")
    require((path / ".collections-synthetic-fixture").read_bytes()
            == b"collections-synthetic-fixture-v1\n", "Fixture marker does not match")
    return path.resolve()


def state(path: Path):
    value = json.loads(path.read_text(encoding="utf-8"))
    require(value.get("schemaVersion") == 2 and isinstance(value.get("collections"), list),
            "Unexpected native collections schema")
    return value


def row(value, identifier):
    found = [r for r in value["collections"] if r["id"] == identifier or r["name"] == identifier]
    require(len(found) == 1, f"Expected one collection for {identifier!r}, found {len(found)}")
    return found[0]


def keys(collection):
    return [item["reference"]["key"] for item in collection["items"]]


def memberships(value, key):
    return {r["id"] for r in value["collections"] if key in keys(r)}


class Acceptance:
    def __init__(self, args):
        self.args, self.root = args, fixture_root(args.root)
        self.client = Candidate(args.cli, args.pipe, args.evidence)
        self.evidence = self.client.evidence
        self.report = {"mode": args.mode, "status": "running", "checks": [], "routes": [],
                       "root": str(self.root), "pipe": args.pipe, "screenshots": []}
        status = self.profile_status()
        self.report["profileBefore"] = status
        alias = status["profileAlias"]
        dirs = [self.root / "profiles" / PROFILE_IDS[alias]] if alias in PROFILE_IDS else [
            self.root / "profiles/local", self.root / "profiles/legacy"]
        allowed = [d / "named-collections-v2.json" for d in dirs]
        choices = [args.store.absolute()] if args.store else [p for p in allowed if p.is_file()]
        require(len(choices) == 1, "Ambiguous/missing native store: supply --store for the active profile")
        self.store = choices[0]
        require(self.store in allowed and not linked(self.store) and not linked(self.store.parent),
                "--store must be this active fixture profile's actual native file")
        self.initial = state(self.store)
        require(len(self.initial["collections"]) == status["collectionsCount"],
                "Store file count does not match the running native profile")
        self.initial_hash = sha(self.store)
        self.protected_before = self.protected()
        self.report.update(store=str(self.store), collectionsBefore=self.initial_hash,
                           protectedBefore=self.protected_before)
        write_evidence(self.evidence / "native-collections-before.json", self.initial)

    def profile_status(self):
        s = self.client.call("collections-test-profile", {"action": "status"})
        require(s.get("success") is True, f"Native fixture status refused: {s}")
        require(s.get("profileAlias") in ("local", "A", "B"), "Unrecognized active fixture profile")
        require(all(s.get(k) is True for k in ("collectionsHealthy", "libraryHealthy", "progressHealthy")),
                f"A native personal store is unhealthy: {s}")
        return s

    def protected(self):
        # Exact ProfilePaths and tagged LegacyPersonalStateStorage names.
        # Missing files are observed too: creation is a change. Vinyl personal
        # library rows share CollectionStore; no imaginary extra Vinyl file.
        dirs = [self.root / "profiles" / p for p in ("local", *PROFILE_IDS.values())]
        paths = [d / name for d in dirs for name in ("collection.ini", "progress.ini")]
        paths += [self.root / "collection-store.ini", self.root / "progress-store.ini"]
        return {str(p.relative_to(self.root)): sha(p) for p in paths}

    def check(self, name, **observed):
        self.report["checks"].append({"name": name, "status": "passed", **observed})

    def props(self, target, *names, missing=False):
        r = self.client.call("qml-get", {"object": target, "props": list(names)}, allow_error=missing)
        if r.get("type") != "reply":
            require(missing and r.get("code") == "NO_SUCH_ITEM", f"qml-get failed: {r}")
            return {}
        return r["props"]

    def until(self, predicate, description, timeout=None):
        end = time.monotonic() + (timeout or self.args.timeout)
        while time.monotonic() < end:
            value = predicate()
            if value:
                return value
            time.sleep(0.12)
        raise AssertionError("Timed out observing " + description)

    def prop(self, target, name, expected):
        return self.until(lambda: self.props(target, name, missing=True).get(name) == expected,
                          f"{target}.{name} == {expected!r}")

    def settled(self, target):
        # Three stable observations avoid clicking at the start/end of wheel inertia.
        end, previous, stable = time.monotonic() + 5, None, 0
        while time.monotonic() < end:
            p = self.props(target, "moving", "flicking", "contentY", missing=True)
            if not p:
                return False  # A loading page replaced this transient ancestor.
            still = p.get("moving") is not True and p.get("flicking") is not True
            stable = stable + 1 if still and p.get("contentY") == previous else 0
            if stable >= 2:
                return True
            previous = p.get("contentY")
            time.sleep(0.15)
        raise AssertionError("Scroll motion did not settle: " + target)

    def reveal(self, target):
        # ui-query supplies real clip ancestor handles; no guessed viewport coordinates.
        for _ in range(30):
            q = self.client.call("ui-query", {"object": target})
            if not all(self.settled(c["handle"]) for c in q.get("clipChain", [])):
                # The bridge may retain a handle from an unloaded page at a
                # reused item address. Start a fresh observation epoch only;
                # this neither invokes a UI action nor changes application data.
                self.client.call("dump-ui", {"maxDepth": 0, "maxItems": 1})
                continue  # Re-query by stable objectName before any click.
            # A target initially in bounds can leave them while inertia settles.
            q = self.client.call("ui-query", {"object": target})
            require(q.get("visible") and q.get("enabled"), f"Control unavailable: {target}")
            r = q["rect"]; cx, cy = r["x"] + r["width"] / 2, r["y"] + r["height"] / 2
            require(r["width"] > 0 and r["height"] > 0, f"Zero-area control: {target}")
            clipped = next((c for c in q.get("clipChain", []) if not (
                c["sceneRect"]["x"] + 2 <= cx <= c["sceneRect"]["x"] + c["sceneRect"]["width"] - 2
                and c["sceneRect"]["y"] + 2 <= cy <= c["sceneRect"]["y"] + c["sceneRect"]["height"] - 2)), None)
            if not clipped:
                w = q.get("rootWindow", {})
                require(0 <= cx <= w.get("width", 0) and 0 <= cy <= w.get("height", 0),
                        f"Control center outside actual window: {target}")
                return
            p = self.props(clipped["handle"], "contentY", "contentHeight", "height")
            require(isinstance(p.get("contentY"), (int, float)), f"Clipped by a non-scrolling ancestor: {target}")
            direction = 240 if cy < clipped["sceneRect"]["y"] else -240
            self.client.call("ui-scroll", {"target": clipped["handle"], "dy": direction,
                                            "xFraction": 0.97, "yFraction": 0.5})
            self.settled(clipped["handle"])
        raise AssertionError("Could not reveal control: " + target)

    def draft_order(self, expected):
        # Reorder/Undo recreate delegates. Resolve fresh observed objects rather
        # than retaining bridge handles that belonged to a previous delegate.
        self.client.call("ui-snapshot")
        for position, key in enumerate(expected):
            def matches():
                button = self.client.call("ui-query", {"object": "mediaCard_" + key})
                column = self.client.call("ui-query", {"object": button["parentHandle"]})
                owner = self.props(column["parentHandle"], "position", "itemCount", missing=True)
                return owner.get("position") == position and owner.get("itemCount") == len(expected)
            self.until(matches, "actual recreated card position " + str(position))

    def click(self, target):
        self.reveal(target)
        self.client.click(target)

    def fill(self, target, text):
        self.click(target)
        self.prop(target, "activeFocus", True)  # Do not mask pointer failures with text-input's focus helper.
        self.client.key("Ctrl+A")
        self.client.key("Backspace")
        if text:
            self.client.call("ui-text-input", {"target": target, "text": text})
        self.prop(target, "text", text)

    def screenshot(self, label):
        self.report["screenshots"].append(self.client.screenshot(label))

    def unchanged(self, expected_hash):
        require(sha(self.store) == expected_hash, "Native collections bytes changed during a canceled/staged action")

    def overview(self):
        panel = self.props("collectionsPanel", "showing", "collectionId")
        if not panel.get("showing"):
            if self.props("vaultBrowseSheet", "visible", missing=True).get("visible"):
                self.client.key("Escape")
                self.prop("vaultBrowseSheet", "visible", False)
            dock = self.props("taskbarCollectionsAction", "visible", "enabled")
            if dock.get("visible") and dock.get("enabled"):
                self.click("taskbarCollectionsAction")
            else:
                # Manga's existing immersive shell intentionally hides the dock.
                # Use its actual Back control and observe the Collections return.
                back = next((name for name in ("tankobanReadingRoomBack", "mangaChapterSeriesBack", "weebcentralSeriesBack")
                             if self.props(name, "visible", "enabled", missing=True).get("visible")), None)
                require(back is not None, "No visible owner Back action while the dock is hidden")
                self.click(back)
                self.prop("collectionsPanel", "showing", True)
        panel = self.props("collectionsPanel", "showing", "collectionId")
        if panel.get("collectionId"):
            self.click("backToCollections")
        self.prop("collectionsPanel", "showing", True)
        self.prop("collectionsPanel", "collectionId", "")
        self.prop("overviewPage", "visible", True)
        if self.props("overviewSearch", "text").get("text"):
            self.fill("overviewSearch", "")

    def collection(self, identifier):
        current = row(state(self.store), identifier)
        self.overview()
        self.fill("overviewSearch", current["name"])
        self.click("collectionCard_" + current["id"])
        self.prop("collectionsPanel", "collectionId", current["id"])
        return current

    def toggle_membership(self, identifier):
        current = row(state(self.store), identifier)
        self.fill("pickerSearch", current["name"])
        name = "membership_" + current["id"]
        before = self.props(name, "included")["included"]
        self.click(name)
        self.prop(name, "included", not before)

    def open_member(self, collection_id, reference):
        self.collection(collection_id)
        self.click("mediaCard_" + reference["key"])
        definition = ROUTES.get(reference["route"])
        latest = {}
        def outcome():
            panel = self.props("collectionsPanel", "showing", "message", "collectionId")
            if panel.get("showing") and panel.get("message"):
                return {"status": "unavailable", "message": panel["message"], "panel": panel}
            if panel.get("showing") or not definition:
                return None
            owner, add, vinyl_page = definition
            p = self.props(add, "visible", "available", "resolution", missing=True)
            latest.update(p)
            resolution = p.get("resolution") or {}
            if not p.get("visible") or not resolution.get("ok"):
                return None
            actual = resolution.get("reference") or {}
            require(actual.get("key") == reference["key"], "Opened owner resolved a different canonical key")
            require(actual.get("route") == reference["route"], "Opened a different owner route")
            require(actual.get("sourceId") == reference["sourceId"] and actual.get("editionId") == reference["editionId"],
                    "Opened a different source identity or edition")
            detail = self.props(owner, "visible", "loading", "errorMsg", "readError", missing=True) if owner else {}
            if owner and detail.get("visible") is not True:
                return None
            if vinyl_page:
                live = self.props("vinylLiveWorld", "page", "browsingSong")
                require(live.get("page") == vinyl_page, "Wrong Vinyl detail page")
                if vinyl_page == "song":
                    require(live.get("browsingSong") is True, "Collection opened a song in playback mode")
                detail.update(live)
            return {"status": "opened", "owner": owner, "add": add, "resolution": resolution,
                    "observedOwner": detail}
        try:
            return self.until(outcome, "exact title owner or visible unavailable explanation")
        except AssertionError as error:
            raise AssertionError(f"{error}; last Add state={latest}") from error

    def title(self, collection_id, reference):
        result = self.open_member(collection_id, reference)
        require(result["status"] == "opened", f"Core title route unavailable: {result}")
        return result["add"]

    def picker(self, add):
        if add == "vaultAddToCollections":
            # Native Vault can independently prompt about the two identical
            # synthetic comic copies. Cancel its visible fixture prompt; never
            # choose an identity relationship or modify Library/progress.
            snapshot = self.client.call("ui-snapshot")
            for item in snapshot.get("elements", []):
                if item.get("objectName") == "vaultCancelIdentity":
                    self.client.click(item["handle"])
        self.click(add + "Action")
        self.prop("collectionsPanel", "pickerActive", True)
        self.prop("pickerSearch", "activeFocus", True)

    def dismiss_picker(self, add, control="pickerCancel"):
        self.client.key("Escape") if control == "Escape" else self.click(control)
        self.prop("collectionsPanel", "pickerActive", False)
        self.prop(add + "Action", "activeFocus", True)

    def create_empty(self, name):
        self.click("newCollectionButton")
        self.prop("collectionsPanel", "pickerCreateOnly", True)
        self.fill("collectionNameField", name)
        self.click("createAndAddButton")
        self.prop("collectionsPanel", "pickerActive", False)
        return row(state(self.store), name)["id"]

    def delete(self, identifier, check_cancel=False):
        current = self.collection(identifier)
        before = sha(self.store)
        self.click("deleteCollectionButton")
        self.prop("collectionsPanel", "deleteActive", True)
        self.prop("cancelDeleteCollection", "activeFocus", True)
        if check_cancel:
            self.click("cancelDeleteCollection")
            self.prop("collectionsPanel", "deleteActive", False)
            self.prop("deleteCollectionButton", "activeFocus", True)
            self.unchanged(before)
            self.click("deleteCollectionButton")
        self.click("confirmDeleteCollection")
        self.prop("collectionsPanel", "deleteActive", False)
        self.prop("collectionsPanel", "collectionId", "")
        require(all(r["id"] != current["id"] for r in state(self.store)["collections"]), "Delete did not persist")

    def core(self):
        primary = row(self.initial, self.args.primary)
        first = next((i["reference"] for i in primary["items"] if i["reference"]["route"] == "theatre-detail"), None)
        second = next((i["reference"] for i in primary["items"] if i["reference"]["route"] == "manga-detail"), None)
        require(first and second and first["key"] != second["key"], "Core seed needs distinct Theatre and manga members")
        token = str(time.time_ns())[-10:]
        a_name, b_name, c_name = [f"acceptance {n} {token}" for n in ("first", "second", "atomic")]
        self.overview(); self.screenshot("actual-overview")
        self.collection(primary["id"]); self.screenshot("actual-collection")
        self.overview(); self.click("newCollectionButton")
        self.click("createAndAddButton")
        require(bool(self.props("pickerValidationError", "text")["text"]), "Blank creation has no visible validation")
        self.unchanged(self.initial_hash)
        self.prop("collectionNameField", "activeFocus", True)
        self.client.key("Tab"); self.prop("pickerCancelCreate", "activeFocus", True)
        self.client.key("Shift+Tab"); self.prop("collectionNameField", "activeFocus", True)
        self.client.key("Escape"); self.prop("collectionsPanel", "pickerActive", False)
        self.prop("newCollectionButton", "activeFocus", True)
        self.client.key("Return"); self.prop("collectionsPanel", "pickerActive", True)
        self.fill("collectionNameField", "canceled " + token); self.click("pickerCancelCreate")
        self.prop("collectionsPanel", "pickerActive", False); self.unchanged(self.initial_hash)
        a, b = self.create_empty(a_name), self.create_empty(b_name)
        before = sha(self.store)
        self.click("newCollectionButton"); self.fill("collectionNameField", a_name.upper())
        self.click("createAndAddButton")
        require(bool(self.props("pickerValidationError", "text")["text"]), "Duplicate name has no visible validation")
        self.click("pickerCancelCreate"); self.prop("collectionsPanel", "pickerActive", False); self.unchanged(before)
        self.check("create, blank/duplicate validation, cancel and keyboard focus", created=[a, b])

        add = self.title(primary["id"], first)
        self.picker(add); self.screenshot("actual-add-to-collections")
        self.toggle_membership(a); self.toggle_membership(b); self.dismiss_picker(add)
        self.unchanged(before)
        self.picker(add); self.toggle_membership(a); self.dismiss_picker(add, "Escape"); self.unchanged(before)
        original_memberships = memberships(state(self.store), first["key"])
        self.picker(add); self.toggle_membership(a); self.toggle_membership(b); self.click("pickerDone")
        self.prop("collectionsPanel", "pickerActive", False)
        require(memberships(state(self.store), first["key"]) == original_memberships | {a, b}, "Membership Save differs")
        saved = sha(self.store)
        self.picker(add); self.toggle_membership(a); self.click("pickerNewCollection")
        self.fill("collectionNameField", "nested cancel " + token); self.click("pickerCancelCreate")
        self.prop("collectionsPanel", "pickerActive", True); self.dismiss_picker(add); self.unchanged(saved)
        self.picker(add); self.toggle_membership(b); self.click("pickerNewCollection")
        self.fill("collectionNameField", ""); self.click("createAndAddButton")
        require(bool(self.props("pickerValidationError", "text")["text"]), "Atomic blank create lacks validation")
        self.unchanged(saved)
        revision = self.profile_status()["collectionsRevision"]
        self.fill("collectionNameField", c_name); self.click("createAndAddButton")
        self.prop("collectionsPanel", "pickerActive", False)
        c = row(state(self.store), c_name)["id"]
        require(memberships(state(self.store), first["key"]) == original_memberships | {a, c}, "Atomic membership result differs")
        require(self.profile_status()["collectionsRevision"] == revision + 1, "Create-and-add was not one native commit")
        self.check("membership Save/Cancel/Escape and atomic create-and-add", temporary=[a, b, c])

        add = self.title(primary["id"], second)
        self.picker(add); self.toggle_membership(a); self.click("pickerDone")
        self.prop("collectionsPanel", "pickerActive", False)
        self.collection(a); self.click("editCollectionButton")
        self.prop("collectionsPanel", "pendingChanges", True)
        staged = sha(self.store); note = 'kept note <one> & "two"'
        description = "An actual native-store acceptance collection"
        self.fill("collectionNameInput", a_name + " edited")
        self.fill("collectionDescriptionInput", description)
        self.fill("noteInput_" + first["key"], note)
        self.click("moveLater_" + first["key"])
        self.draft_order([second["key"], first["key"]])
        self.click("removeTitle_" + second["key"]); self.click("undoRemoveTitle")
        self.draft_order([second["key"], first["key"]]); self.unchanged(staged)
        self.click("saveCollectionButton"); self.prop("collectionsPanel", "pendingChanges", False)
        changed = row(state(self.store), a)
        require(changed["name"] == a_name + " edited" and keys(changed) == [second["key"], first["key"]], "Draft name/order Save differs")
        require(changed["description"] == description, "Description Save differs")
        require(changed["items"][1]["note"] == note, "Note text did not round-trip exactly")
        saved = sha(self.store)
        self.click("editCollectionButton"); self.fill("collectionNameInput", "unsaved " + token)
        self.fill("noteInput_" + first["key"], "discard this note")
        self.click("moveEarlier_" + first["key"]); self.click("cancelCollectionEdit")
        self.prop("collectionsPanel", "pendingChanges", False); self.unchanged(saved)
        self.click("editCollectionButton"); self.fill("collectionNameInput", "discard on back " + token)
        self.click("backToCollections"); self.prop("collectionPage", "leaving", True)
        self.prop("stayEditingButton", "activeFocus", True)
        self.client.key("Escape"); self.prop("collectionPage", "leaving", False)
        self.prop("collectionsPanel", "pendingChanges", True); self.unchanged(saved)
        self.click("backToCollections"); self.click("discardCollectionEdits")
        self.prop("collectionsPanel", "collectionId", ""); self.unchanged(saved)
        self.check("draft note/order/name Save, removal Undo, Cancel and guarded Back")
        self.delete(b, check_cancel=True); self.delete(c); self.delete(a)
        self.unchanged(self.initial_hash)
        self.check("delete Cancel/confirm and temporary-collection cleanup; original seed restored")

    def audit_routes(self):
        self.overview(); self.screenshot("route-audit-overview")
        total = sum(1 for c in self.initial["collections"] for i in c["items"]
                    if not self.args.route or i["reference"]["route"] in self.args.route)
        self.report["routeTotalMembers"] = total
        blocked = False
        for collection in deepcopy(self.initial["collections"]):
            for item in collection["items"]:
                reference = item["reference"]
                if self.args.route and reference["route"] not in self.args.route:
                    continue
                record = {"collectionId": collection["id"], "collection": collection["name"], "reference": reference}
                try:
                    record.update(self.open_member(collection["id"], reference))
                    if record["status"] == "opened":
                        self.picker(record["add"])
                        self.dismiss_picker(record["add"])
                        record["pickerCancelPreserved"] = sha(self.store) == self.initial_hash
                        require(record["pickerCancelPreserved"], "Route picker Cancel changed native data")
                    if record["status"] == "unavailable":
                        require(record["panel"]["collectionId"] == collection["id"], "Unavailable route lost the collection")
                        self.screenshot(f"route-{len(self.report['routes']) + 1:02d}-unavailable")
                    self.unchanged(self.initial_hash)
                except Exception as error:
                    record.update(status="failed", error=str(error))
                    try:
                        self.screenshot(f"route-{len(self.report['routes']) + 1:02d}-failed")
                    except Exception as grab_error:
                        record["grabError"] = str(grab_error)
                try:
                    self.unchanged(self.initial_hash)
                    self.overview()  # Actual persistent dock return, never arbitrary invocation.
                except Exception as recovery_error:
                    record.update(status="failed", recoveryError=str(recovery_error))
                    blocked = True
                self.report["routes"].append(record)
                write_evidence(self.evidence / "route-audit-progress.json", self.report["routes"])
                if blocked:
                    break  # Do not continue from changed native state or unrecoverable UI.
            if blocked:
                break
        require(self.report["routes"], "No persisted references matched the route audit")
        self.unchanged(self.initial_hash)
        self.report["routeNotAttempted"] = total - len(self.report["routes"])
        self.report["routeCoverageMeaning"] = "Exact owner + Add canonical resolution observed; provider metadata loading is recorded separately."

    def finish(self):
        self.report.update(collectionsAfter=sha(self.store), protectedAfter=self.protected(),
                           profileAfter=self.profile_status())
        require(self.report["protectedAfter"] == self.protected_before, "Profile Library/progress files changed")
        require(self.report["profileAfter"]["profileAlias"] == self.report["profileBefore"]["profileAlias"],
                "Active profile changed during acceptance")
        self.check("profile Library/progress hashes unchanged", paths=list(self.protected_before))
        self.report["status"] = "passed"
        if any(r["status"] == "failed" for r in self.report["routes"]):
            self.report["status"] = "failed"
        elif any(r["status"] != "opened" for r in self.report["routes"]):
            self.report["status"] = "partial"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("root", "evidence", "cli"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--pipe", required=True)
    parser.add_argument("--mode", choices=("core", "routes"), default="core")
    parser.add_argument("--store", type=Path, help="Exact active native file when local/legacy is ambiguous")
    parser.add_argument("--primary", default="Across worlds")
    parser.add_argument("--timeout", type=float, default=25, help="Observed UI/route deadline in seconds")
    parser.add_argument("--route", action="append", help="Optional exact route filter for routes mode")
    args = parser.parse_args()
    require(2 <= args.timeout <= 120, "Use a bounded timeout from 2 to 120 seconds")
    test = None
    try:
        test = Acceptance(args)
        test.core() if args.mode == "core" else test.audit_routes()
        test.finish()
    except Exception as error:
        if test is None:
            raise
        test.report.update(status="failed", error=str(error), traceback=traceback.format_exc(),
                           collectionsAfter=sha(test.store), protectedAfter=test.protected())
        try:
            test.screenshot("acceptance-failure")
        except Exception as grab_error:
            test.report["failureGrabError"] = str(grab_error)
    finally:
        if test is not None:
            try:
                write_evidence(test.evidence / "native-collections-after.json", state(test.store))
            except Exception as read_error:
                test.report.update(status="failed", finalNativeReadError=str(read_error))
            test.report["protectedUnchanged"] = test.report.get("protectedAfter") == test.protected_before
            write_evidence(test.evidence / (args.mode + "-acceptance.json"), test.report)
    print(json.dumps({"status": test.report["status"], "report": str(test.evidence / (args.mode + "-acceptance.json"))}))
    return 0 if test.report["status"] == "passed" else 3 if test.report["status"] == "partial" else 1


if __name__ == "__main__":
    raise SystemExit(main())
