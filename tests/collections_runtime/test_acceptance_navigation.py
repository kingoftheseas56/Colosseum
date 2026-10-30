"""Tests for acceptance-driver navigation, not substitutes for app verification."""
import unittest
from verify_collections_app import Acceptance

class FakeNavigation:
    overview = Acceptance.overview
    def __init__(self, showing, collection_id="", immersive=False):
        self.showing, self.collection_id = showing, collection_id
        self.clicks = []
        self.immersive = immersive
    def click(self, target):
        self.clicks.append(target)
        if target == "taskbarCollectionsAction":
            self.showing = not self.showing
            self.collection_id = ""
        elif target == "tankobanReadingRoomBack":
            self.showing, self.collection_id, self.immersive = True, "collection-id", False
        elif target == "backToCollections":
            self.collection_id = ""
        else:
            raise AssertionError(target)
    def props(self, target, *names, **kwargs):
        if target == "collectionsPanel":
            return {"showing": self.showing, "collectionId": self.collection_id}
        if target == "taskbarCollectionsAction":
            return {"visible": not self.immersive, "enabled": not self.immersive}
        if target == "tankobanReadingRoomBack":
            return {"visible": self.immersive, "enabled": self.immersive}
        if target == "mangaChapterSeriesBack":
            return {}
        if target == "vaultBrowseSheet":
            return {}
        if target == "overviewSearch":
            return {"text": ""}
        raise AssertionError(target)
    def prop(self, target, name, expected):
        value = self.showing if name in ("showing", "visible") else self.collection_id
        if value != expected:
            raise AssertionError(f"{target}.{name}: {value!r} != {expected!r}")

class ScrollObservation:
    settled = Acceptance.settled
    def __init__(self, value): self.value = value
    def props(self, *args, **kwargs): return self.value

class ScrollTests(unittest.TestCase):
    def test_disappearing_clip_is_not_a_stable_target(self):
        self.assertIs(ScrollObservation({}).settled("expired-handle"), False)
    def test_stable_live_clip_is_observed(self):
        self.assertIs(ScrollObservation({"moving": False, "flicking": False, "contentY": 12}).settled("live-handle"), True)

class DraftOrderObservation:
    draft_order = Acceptance.draft_order
    def __init__(self):
        self.client = self
        self.fresh = False
    def call(self, command, payload=None):
        if command == "ui-snapshot": self.fresh = True; return {}
        if command == "ui-query": return {"parentHandle": "card-owner"}
        raise AssertionError(command)
    def props(self, *args, **kwargs):
        return {"position": 0 if self.fresh else 9, "itemCount": 1}
    def prop(self, target, name, expected):
        if self.props().get(name) != expected: raise AssertionError("stale item address")
    def until(self, condition, description):
        if not condition(): raise AssertionError(description)

class DraftOrderTests(unittest.TestCase):
    def test_recreated_delegates_use_a_fresh_observation_epoch(self):
        observation = DraftOrderObservation()
        observation.draft_order(["key"])
        self.assertTrue(observation.fresh)

class NavigationTests(unittest.TestCase):
    def test_immersive_detail_uses_visible_back(self):
        driver = FakeNavigation(False, immersive=True)
        driver.overview()
        self.assertEqual(driver.clicks, ["tankobanReadingRoomBack", "backToCollections"])
    def test_closed_uses_real_taskbar(self):
        driver = FakeNavigation(False)
        driver.overview()
        self.assertEqual(driver.clicks, ["taskbarCollectionsAction"])
    def test_open_overview_does_not_toggle_closed(self):
        driver = FakeNavigation(True)
        driver.overview()
        self.assertEqual(driver.clicks, [])
    def test_detail_uses_real_back_action(self):
        driver = FakeNavigation(True, "collection-id")
        driver.overview()
        self.assertEqual(driver.clicks, ["backToCollections"])

if __name__ == "__main__":
    unittest.main()
