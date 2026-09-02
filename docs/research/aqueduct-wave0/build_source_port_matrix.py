#!/usr/bin/env python3
"""Build the W00 functional source-to-port matrix from the exact split bundle."""
from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path

ROUTE_RE = re.compile(
    r"(?:router|Router|enginefs\.router|externalRouter|addonHTTP|middleware)\."
    r"(get|post|put|delete|all|use|head|options)\(\s*[`\"']([^`\"']+)"
)
EXPECTED_ROUTE_COUNT = 90

INTERNAL_ROWS = [
    (105, "settings/defaults/persistence", "W05"),
    (172, "EngineFS registry/events/activity counters", "W07-W09"),
    (414, "cache and disk cleanup policy", "W06"),
    (564, "server lifecycle/listener/top-level wiring", "W03-W04,W31"),
    (613, "peer-search coordinator", "W14"),
    (614, "DHT peer source", "W14"),
    (625, "tracker peer source", "W14"),
    (816, "torrent engine metadata/selection/scheduler/swarm", "W10-W19"),
]
INTERNAL_ROWS += [
    (845, "ut_metadata exchange", "W10"),
    (846, "persistent piece store", "W12"),
    (847, "circular piece store", "W13"),
    (848, "FileStream moving read window", "W20"),
    (853, "16 KiB piece block reservations", "W17"),
    (667, "legacy HLS coordinator", "W23-W24"),
    (855, "HLS v2 converter HTTP router", "W25"),
    (944, "casting discovery/transcode/player bridge", "W29"),
    (1024, "local-addon handlers/indexing", "W30"),
    (1025, "addon HTTP manifest/resource contract", "W30"),
]


def packet_for(module_id: int, route: str) -> str:
    if module_id == 172:
        return "W21-W22"
    if module_id == 805:
        return "W27"
    if module_id == 855:
        return "W25"
    if module_id == 944:
        return "W29"
    if module_id in {1024, 1025}:
        return "W30"
    if module_id == 1088 or module_id == 1306:
        return "W36"
    if module_id == 1121:
        return "W33"
    if module_id in {1234, 1285}:
        return "W34"
    if module_id in {1293, 1300}:
        return "W35"
    if module_id != 564:
        raise ValueError(f"unowned route module {module_id}: {route}")
    if route.startswith("/settings"):
        return "W05-W06"
    if route.startswith("/hlsv2") or ":playlist" in route:
        return "W25"
    if route.endswith(".m3u8") or any(token in route for token in ("stream", "dlna", "thumb.jpg")):
        return "W24"
    if route.startswith("/probe") or route.startswith("/tracks/") or route.startswith("/samples/"):
        return "W23"
    if route.startswith("/subtitles") or route.startswith("/opensubHash"):
        return "W26"
    if route.startswith("/yt/"):
        return "W28"
    if route.startswith("/casting"):
        return "W29"
    if route.startswith("/local-addon"):
        return "W30"
    if route.startswith("/proxy"):
        return "W27"
    if route.startswith("/rar"):
        return "W33"
    if route.startswith("/zip") or route.startswith("/7zip"):
        return "W34"
    if route.startswith("/tar") or route.startswith("/tgz"):
        return "W35"
    if route.startswith("/ftp") or route.startswith("/nzb"):
        return "W36"
    if route in {"/", "/heartbeat", "/network-info", "/device-info", "/get-https", "/hwaccel-profiler"}:
        return "W31"
    raise ValueError(f"unowned module 564 route: {route}")


def extract_routes(modules_dir: Path) -> list[tuple[int, str, str]]:
    rows: list[tuple[int, str, str]] = []
    for path in modules_dir.glob("*.js"):
        text = path.read_text(encoding="utf-8", errors="strict")
        for match in ROUTE_RE.finditer(text):
            rows.append((int(path.stem), match.group(1).upper(), match.group(2)))
    return sorted(rows)

def build_rows(routes: list[tuple[int, str, str]]) -> list[dict[str, str]]:
    result: list[dict[str, str]] = []
    for module_id, method, route in routes:
        result.append({
            "kind": "route",
            "module": str(module_id),
            "surface": f"{method} {route}",
            "decision": "PORT",
            "packet": packet_for(module_id, route),
        })
    for module_id, surface, packet in INTERNAL_ROWS:
        result.append({
            "kind": "internal",
            "module": str(module_id),
            "surface": surface,
            "decision": "PORT",
            "packet": packet,
        })
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("modules_dir", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    routes = extract_routes(args.modules_dir)
    if len(routes) != EXPECTED_ROUTE_COUNT:
        raise SystemExit(
            f"expected {EXPECTED_ROUTE_COUNT} functional route registrations, found {len(routes)}"
        )
    rows = build_rows(routes)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=["kind", "module", "surface", "decision", "packet"],
        )
        writer.writeheader()
        writer.writerows(rows)
    if any(row["decision"] not in {"PORT", "PORT VIA PROVEN EQUIVALENT"} for row in rows):
        raise SystemExit("matrix contains an unresolved decision")
    print(f"GREEN: {len(routes)} routes + {len(INTERNAL_ROWS)} internal rows -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
