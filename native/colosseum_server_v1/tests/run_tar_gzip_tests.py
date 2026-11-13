"""Run controlled pinned-source TAR/GZIP comparisons and native lifecycle tests."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--candidate', required=True)
parser.add_argument('--oracle', required=True)
parser.add_argument('--node', default='node')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='server1-tar-gzip-') as directory:
    golden = Path(directory) / 'golden.json'
    subprocess.run([args.node, str(Path(__file__).with_name('run_tar_gzip_oracle.cjs')),
                    args.oracle, str(golden)], check=True, timeout=60)
    subprocess.run([args.candidate, str(golden)], check=True, timeout=60)
