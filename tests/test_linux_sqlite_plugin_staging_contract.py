from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
TEST_CMAKE = ROOT / "tests" / "CMakeLists.txt"


class LinuxSqlitePluginStagingContract(unittest.TestCase):
    def test_consumption_history_stages_qt_sqlite_plugin_portably(self):
        cmake = TEST_CMAKE.read_text(encoding="utf-8")
        start = cmake.index("add_executable(tst_consumption_history")
        end = cmake.index("add_executable(tst_activity_playback_tracker", start)
        block = cmake[start:end]

        self.assertNotIn("qsqlite.dll", block)
        self.assertIn("$<TARGET_FILE:Qt6::QSQLiteDriverPlugin>", block)


if __name__ == "__main__":
    unittest.main()
