from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class AccountSyncProfileSwitchContract(unittest.TestCase):
    def test_sync_is_stopped_before_profile_adapters_are_removed(self):
        source = (ROOT / "native" / "account" / "AccountRuntime.cpp").read_text(
            encoding="utf-8"
        )
        start = source.index("storesAboutToChange")
        end = source.index("accountProfileReadyForSync", start)
        handler = source[start:end]

        self.assertIn("m_syncEngine.active()", handler)
        self.assertIn("m_syncEngine.stopPreservingOutbox", handler)
        self.assertLess(
            handler.index("m_syncEngine.stopPreservingOutbox"),
            handler.index("clearCoreSyncAdapters"),
        )


if __name__ == "__main__":
    unittest.main()
