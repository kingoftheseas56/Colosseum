"""Reject an incomplete or disabled M0R CTest receiving set."""

import json
import sys
from collections import Counter
from pathlib import Path


EXPECTED_TESTS = frozenset("""
vinyl_w4_superbuild_link_smoke
vinyl_w4_ba0_contract vinyl_w4_ba1_contract vinyl_w4_ba2a_contract
vinyl_w4_ba2b_contract vinyl_w4_ba2c_contract vinyl_w4_ba2c_adversarial_contract
vinyl_w4_ba2d_bridge_contract vinyl_w4_ba3_contract
vinyl_w4_ba4_ledger_contract vinyl_w4_ba4_persistence_contract
vinyl_w4_ba5_library_contract vinyl_w4_ba5_playlist_contract vinyl_w4_ba5_persistence_contract
vinyl_w4_bb1_contract vinyl_w4_bb2_contract vinyl_w4_bb3_contract
vinyl_w4_bb4_contract vinyl_w4_bb4_cue_contract vinyl_w4_bb5_contract
vinyl_w4_bc1_contract vinyl_w4_bc2_contract vinyl_w4_bc3_contract
vinyl_w4_bd1_contract vinyl_w4_bd2_contract
vinyl_w4_be1_contract vinyl_w4_be2_contract vinyl_w4_be2_persistence_contract
vinyl_w4_be_derivation_contract vinyl_w4_be3_contract
vinyl_w4_bf1_contract vinyl_w4_bf2_contract vinyl_w4_bf3_contract
vinyl_w4_bg1_contract vinyl_w4_bg2_contract
vinyl_w4_h_fake_journey vinyl_w4_h_real_cue_journey
vinyl_artist_catalogue_harness test_artist_artwork artist_personal_harness
test_arc62_artist_media test_artist_service_bridge test_artist_service_composed
vinyl_ba2db2_playlist_identity_probe vinyl_ba2db3_seek_completion_probe
vinyl_w4a_bounded_mpv_probe vinyl_ba2d_mpv_api_probe
""".split())


def validate_inventory(document):
    tests = document.get("tests") if isinstance(document, dict) else None
    if not isinstance(tests, list):
        return ["Inventory has no test list"]
    errors = []
    counts = Counter()
    for test in tests:
        if not isinstance(test, dict) or not isinstance(test.get("name"), str):
            errors.append("Malformed test entry")
            continue
        name = test["name"]
        counts[name] += 1
        for prop in test.get("properties", []):
            if prop.get("name") == "DISABLED" and prop.get("value"):
                errors.append(f"Disabled: {name}")
    errors.extend(f"Missing: {name}" for name in sorted(EXPECTED_TESTS - counts.keys()))
    errors.extend(f"Unexpected: {name}" for name in sorted(counts.keys() - EXPECTED_TESTS))
    errors.extend(f"Duplicate: {name}" for name, count in sorted(counts.items()) if count != 1)
    return errors


def main():
    if len(sys.argv) != 2:
        print("Usage: verify_inventory.py CTEST_JSON_FILE", file=sys.stderr)
        return 2
    try:
        document = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8-sig"))
    except (OSError, ValueError) as error:
        print(f"Invalid inventory: {error}", file=sys.stderr)
        return 1
    errors = validate_inventory(document)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"M0R inventory accepted: {len(EXPECTED_TESTS)} required tests, none disabled")
    return 0


if __name__ == "__main__":
    sys.exit(main())
