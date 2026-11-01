import copy
import unittest

from verify_inventory import EXPECTED_TESTS, validate_inventory


class ReceivingInventoryTests(unittest.TestCase):
    def setUp(self):
        self.inventory = {"tests": [
            {"name": name, "properties": []} for name in sorted(EXPECTED_TESTS)
        ]}

    def test_all_required_harnesses_are_present(self):
        self.assertEqual(validate_inventory(self.inventory), [])

    def test_missing_real_cue_test_is_rejected(self):
        self.inventory["tests"] = [test for test in self.inventory["tests"]
                                   if test["name"] != "vinyl_w4_h_real_cue_journey"]
        self.assertTrue(validate_inventory(self.inventory))

    def test_disabled_harness_is_rejected(self):
        self.inventory["tests"][0]["properties"] = [{"name": "DISABLED", "value": True}]
        self.assertTrue(validate_inventory(self.inventory))

    def test_duplicate_name_is_rejected(self):
        self.inventory["tests"].append(copy.deepcopy(self.inventory["tests"][0]))
        self.assertTrue(validate_inventory(self.inventory))

    def test_unexpected_replacement_is_rejected(self):
        self.inventory["tests"][0]["name"] = "unrelated_test"
        self.assertTrue(validate_inventory(self.inventory))

    def test_malformed_inventory_is_rejected(self):
        self.assertTrue(validate_inventory({}))


if __name__ == "__main__":
    unittest.main()
