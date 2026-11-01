"""Two real localhost peers exercising production native rate sampling."""
import argparse
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
from run_wire_statistics import wait_for


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)


def run(args):
    root = Path(tempfile.mkdtemp(prefix="peer-statistics-", dir=args.work)).resolve()
    subprocess.run([args.executable, "--prepare-upload", str(root)], check=True, capture_output=True, timeout=15)
    info_hash = (root / "info_hash.txt").read_text().strip()
    peers, outputs, ports = [], [], []
    candidate = None
    try:
        for label in ("a", "b"):
            with socket.socket() as reserve:
                reserve.bind(("127.0.0.1", 0))
                ports.append(reserve.getsockname()[1])
            output = (root / f"peer-{label}-process.log").open("w")
            outputs.append(output)
            log = root / f"peer-{label}.log"
            peer = subprocess.Popen([sys.executable, args.peer, "--port", str(ports[-1]), "--label", label,
                "--info-hash", info_hash, "--log", str(log), "--file-length", "16457", "--piece-length", "16384",
                "--first-byte", "U", "--second-byte", "U", "--hold-all", "--release-file", str(root / "release")],
                stdout=output, stderr=subprocess.STDOUT)
            peers.append(peer)
            wait_for(peer, log, "LISTEN ")
        with (root / "candidate.log").open("w") as output:
            candidate = subprocess.Popen([args.executable, "--peer-stats", str(root), *map(str, ports)],
                                         stdout=output, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 30
            dropped = False
            while candidate.poll() is None and time.monotonic() < deadline:
                if not dropped and (root / "drop-peer-a").exists():
                    stop(peers[0])
                    dropped = True
                time.sleep(0.01)
            if candidate.poll() is None:
                raise RuntimeError("native peer-statistics test exceeded 30 seconds")
        print((root / "candidate.log").read_text(), end="", flush=True)
        if candidate.returncode:
            raise RuntimeError(f"native peer-statistics test failed {candidate.returncode}; evidence {root}")
        assert dropped, "native test never requested controlled peer departure"
        for label, span in (("a", "piece=0 start=0 length=16384"), ("b", "piece=1 start=0 length=73")):
            log = root / f"peer-{label}.log"
            lines = log.read_text().splitlines()
            requests = [line for line in lines if line.startswith("REQUEST ")]
            assert len(requests) == 1 and requests[0].endswith(span), requests
            # Held sends use the legacy LATE_PIECE marker even without a CANCEL.
            # Exact REQUEST spans plus native payload checks establish identity.
            assert any((line.startswith("PIECE_SENT ") and line.endswith(span))
                       or line.startswith("LATE_PIECE_SENT_AFTER_RELEASE ") for line in lines), lines
        print(f"K12 peer-statistics fixture PASS; evidence {root}")
    finally:
        if candidate is not None:
            stop(candidate)
        for peer in peers:
            stop(peer)
        for output in outputs:
            output.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True)
    parser.add_argument("--peer", required=True)
    parser.add_argument("--work", required=True)
    run(parser.parse_args())
