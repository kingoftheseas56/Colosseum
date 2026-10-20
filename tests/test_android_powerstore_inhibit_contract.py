"""Android PowerStore must not read a result from a timed-out UI-thread call.

Task013 emulator evidence: backgrounding during playback crashed the process
with SIGSEGV in PowerStore::applyPlatformInhibit -> QFuture<QVariant>::result()
after runOnAndroidMainThread's 1 s deadline cancelled the future.
"""
import pathlib
import re
import unittest

SOURCE = pathlib.Path(__file__).resolve().parents[1] / "native" / "player" / "powerstore.cpp"


class PowerStoreInhibitContract(unittest.TestCase):
    def android_branch(self) -> str:
        text = SOURCE.read_text(encoding="utf-8")
        body = text.index("bool PowerStore::applyPlatformInhibit")
        start = text.index("#elif defined(Q_OS_ANDROID)", body)
        return text[start:text.index("#elif", start + 1)]

    def test_deadline_future_result_is_guarded(self):
        branch = self.android_branch()
        self.assertIn("runOnAndroidMainThread", branch)
        result = branch.index("future.result()")
        guard = re.search(r"if \(future\.isCanceled\(\) \|\| future\.resultCount\(\) == 0\)\s*return false;", branch)
        self.assertIsNotNone(guard, "cancelled/empty future must return before result()")
        self.assertLess(guard.start(), result)

    def test_negative_control_unguarded_shape_is_rejected(self):
        unguarded = "future.waitForFinished();\n    return future.isFinished() && future.result().toBool();"
        self.assertIsNone(
            re.search(r"if \(future\.isCanceled\(\) \|\| future\.resultCount\(\) == 0\)", unguarded))


if __name__ == "__main__":
    unittest.main()
