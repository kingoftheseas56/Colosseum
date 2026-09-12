"""Reproduce the M00 source guard, frozen factory identities and ordered differential."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def run(argv, env, output, stem):
    result = subprocess.run([str(x) for x in argv], env=env, capture_output=True, timeout=40)
    (output / (stem + ".stdout.txt")).write_bytes(result.stdout)
    (output / (stem + ".stderr.txt")).write_bytes(result.stderr)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--oracle", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--node", default="node")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    packet = Path(__file__).resolve().parents[1]
    fixture = packet / "scenarios/oracle_m00_probe.mjs"
    authority = json.loads((packet / "source/SOURCE-TRACE.json").read_text(encoding="utf-8"))
    expected = authority["oracle"]["sha256"]
    assert hashlib.sha256(args.oracle.read_bytes()).hexdigest() == expected
    env = dict(os.environ, M00_ORACLE_PATH=str(args.oracle.resolve()))
    with tempfile.TemporaryDirectory(prefix="m00-oracle-reject-") as temp:
        mutated = Path(temp) / "server.js"
        data = bytearray(args.oracle.read_bytes())
        data[-1] ^= 1
        mutated.write_bytes(data)
        invalid = run([args.node, fixture], dict(env, M00_ORACLE_PATH=str(mutated)),
                      args.output, "mutated-oracle")
        assert invalid.returncode != 0 and b"oracle SHA-256 mismatch" in invalid.stderr
    source = run([args.node, fixture, "--json"], env, args.output, "source")
    assert source.returncode == 0, source.stderr.decode(errors="replace")
    report = json.loads(source.stdout)
    assert report["oracle_sha256"] == expected
    frozen = {row["module"]: row for row in authority["authorities"]}
    for module in report["details"]["moduleMap"]:
        row = frozen[module["module"]]
        assert module["factory_sha256"] == row["sha256"], module["module"]
        assert row["lines"] == f'{module["first_lf_line"]}-{module["last_lf_line"]}'
    candidate = run([args.candidate, "--trace"], env, args.output, "candidate")
    assert candidate.returncode == 0, candidate.stderr.decode(errors="replace")
    source_lines = report["trace_lines"]
    candidate_lines = candidate.stdout.decode("utf-8").splitlines()
    assert len(source_lines) == 11 and len(candidate_lines) == 11
    assert source_lines == candidate_lines, {"source": source_lines, "candidate": candidate_lines}
    # Countercheck: ordering and value mutations cannot be accepted by this comparison.
    wrong = list(candidate_lines)
    wrong[-1] = "M00-03 callback-cancel=failed"
    assert source_lines != wrong and source_lines != list(reversed(candidate_lines))
    (args.output / "source.trace.txt").write_text("\n".join(source_lines) + "\n", encoding="utf-8")
    verdict = {"oracle_sha256": expected, "source_exit": source.returncode,
               "candidate_exit": candidate.returncode, "source_lines": len(source_lines),
               "candidate_lines": len(candidate_lines), "differences": 0,
               "mutated_oracle_rejected": True, "frozen_factory_identities": "7/7 matched",
               "scope": "11 field projections; see SOURCE-EXECUTION details for non-equivalent callback/sink APIs and source-only wrapper behavior"}
    (args.output / "RESULT.json").write_text(json.dumps(verdict, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(verdict))


if __name__ == "__main__":
    main()
