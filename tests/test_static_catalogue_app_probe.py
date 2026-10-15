import unittest

from static_catalogue_app_probe import activate_universe_portal


def banner(handle):
    return {"class": "UniverseBanner", "visible": True,
            "sceneRect": {"y": 400}, "handle": handle}


class UniversePortalLifecycleTest(unittest.TestCase):
    def test_detached_target_is_recollected_before_click(self):
        snapshots = iter([[banner("old")], [banner("new")]])
        clicks = []

        def click(handle):
            clicks.append(handle)
            if handle == "old":
                raise RuntimeError({"code": "NO_WINDOW", "message": handle})

        activate_universe_portal(lambda: next(snapshots), click,
                                 lambda: self.fail("visible portal needs no scroll"),
                                 sleep=lambda _: None)
        self.assertEqual(clicks, ["old", "new"])

    def test_expired_generation_is_recollected_before_click(self):
        snapshots = iter([[banner("expired")], [banner("current")]])
        clicks = []

        def click(handle):
            clicks.append(handle)
            if handle == "expired":
                raise RuntimeError({"code": "NO_SUCH_ITEM", "message": handle})

        activate_universe_portal(lambda: next(snapshots), click, lambda: None,
                                 sleep=lambda _: None)
        self.assertEqual(clicks, ["expired", "current"])

    def test_other_bridge_errors_are_not_hidden(self):
        def click(_):
            raise RuntimeError({"code": "DRIVE_DISABLED"})

        with self.assertRaisesRegex(RuntimeError, "DRIVE_DISABLED"):
            activate_universe_portal(lambda: [banner("target")], click,
                                     lambda: None, sleep=lambda _: None)

    def test_repeated_detachment_fails_at_the_bound(self):
        clicks = []

        def click(handle):
            clicks.append(handle)
            raise RuntimeError({"code": "NO_WINDOW"})

        with self.assertRaisesRegex(AssertionError, "No real universe portal"):
            activate_universe_portal(lambda: [banner("target")], click,
                                     lambda: None, sleep=lambda _: None, attempts=3)
        self.assertEqual(len(clicks), 3)


if __name__ == "__main__":
    unittest.main()
