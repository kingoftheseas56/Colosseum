"""Generate legal synthetic archives, observe pinned source, test native parser."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--oracle", required=True)
    parser.add_argument("--node", default="node")
    args = parser.parse_args()
    script = Path(__file__).with_name("run_rar_sevenzip_oracle.cjs")
    with tempfile.TemporaryDirectory(prefix="server1-rar-sevenzip-") as temporary:
        golden = Path(temporary) / "oracle.json"
        subprocess.run([args.node, str(script), args.oracle, str(golden)],
                       check=True, timeout=60)
        subprocess.run([args.candidate, str(golden)], check=True, timeout=60)


if __name__ == "__main__":
    main()
