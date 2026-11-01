"""Controlled localhost wire-byte qualification against the production adapter."""
import argparse
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time


def wait_for(peer, path, marker, seconds=5):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if path.exists() and marker in path.read_text():
            return
        if peer.poll() is not None:
            raise RuntimeError(f"fixture exited {peer.returncode} before {marker}")
        time.sleep(0.01)
    raise RuntimeError(f"fixture did not report {marker}")


def run(args):
    work = Path(tempfile.mkdtemp(prefix="wire-statistics-", dir=args.work)).resolve()
    for mode in ("normal", "cancel", "unsolicited"):
        case = work / mode
        case.mkdir()
        prepared = subprocess.run([args.executable, "--prepare", str(case)], capture_output=True, text=True, timeout=15)
        (case / "prepare.log").write_text(prepared.stdout + prepared.stderr)
        prepared.check_returncode()
        info_hash = (case / "info_hash.txt").read_text().strip()
        with socket.socket() as reserve:
            reserve.bind(("127.0.0.1", 0))
            port = reserve.getsockname()[1]
        log = case / "peer.log"
        with (case / "peer-process.log").open("w") as output:
            peer = subprocess.Popen([sys.executable, args.peer, "--port", str(port), "--label", mode,
                "--info-hash", info_hash, "--log", str(log), "--file-length", "32768",
                "--piece-length", "16384", "--first-byte", "K", "--second-byte", "K",
                "--hold-all", "--release-file", str(case / "release"), "--unsolicited" if mode == "unsolicited" else "--duplicate-late"],
                stdout=output, stderr=subprocess.STDOUT)
            try:
                wait_for(peer, log, "LISTEN ")
                result = subprocess.run([args.executable, "--wire-stats", str(case), str(port), mode],
                                        capture_output=True, text=True, timeout=20)
                (case / "candidate.log").write_text(result.stdout + result.stderr)
                print(result.stdout + result.stderr, end="", flush=True)
                result.check_returncode()
                wait_for(peer, log, "PIECE_SENT" if mode == "unsolicited" else "DUPLICATE_LATE_PIECE")
                lines = log.read_text().splitlines()
                requests = [line for line in lines if line.startswith("REQUEST ")]
                if mode == "unsolicited":
                    assert not requests, requests
                    assert sum(line.startswith("UNSOLICITED_SCHEDULED") for line in lines) == 1
                    assert any(line.startswith("PIECE_SENT ") and line.endswith("piece=0 start=0 length=16384") for line in lines)
                else:
                    assert len(requests) == 1 and requests[0].endswith("piece=0 start=0 length=16384"), requests
                    assert sum(line.startswith("DUPLICATE_LATE_PIECE") for line in lines) == 1
            finally:
                if peer.poll() is None:
                    peer.terminate()
                    try:
                        peer.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        peer.kill()
                        peer.wait(timeout=3)
    print("K12 wire-statistics fixtures PASS: 3 cases; owned/duplicate/late/unsolicited wire evidence retained")
    print(f"Evidence: {work}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True)
    parser.add_argument("--peer", required=True)
    parser.add_argument("--work", required=True)
    run(parser.parse_args())
