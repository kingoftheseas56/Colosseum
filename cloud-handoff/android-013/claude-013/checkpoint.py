"""Task 013 source checkpoint: create and restore-verify. Python stdlib + git.

  create  <repo> <commit> <out-dir>
      SOURCE-CHECKPOINT.zip   git archive of <commit> (full tree, including the
                              cloud-handoff packet and hardened donor snapshot)
      CHECKPOINT-MANIFEST.json  commit/import identity and sha256 of every file
      SOURCE-DELTA.patch      binary diff import..commit (all new files included)
      NEW-FILES.json          files added since the import, with sha256

  restore <checkpoint.zip> <manifest.json> <new-empty-dir>
      extracts into a NEW directory and verifies every file against the manifest
      (missing, extra and changed files all fail).
"""
import hashlib
import io
import json
import pathlib
import subprocess
import sys
import zipfile

IMPORT_SHA = "5e60f3630e2d7f56ae49556c0472c4b5391dcfe2"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], check=True,
                          capture_output=True).stdout


def create(repo, commit, out):
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    commit = git(repo, "rev-parse", commit).decode().strip()
    archive = git(repo, "archive", "--format=zip", commit)
    zip_path = out / "SOURCE-CHECKPOINT.zip"
    zip_path.write_bytes(archive)
    files = {}
    with zipfile.ZipFile(io.BytesIO(archive)) as z:
        for info in z.infolist():
            if not info.is_dir():
                files[info.filename] = sha256(z.read(info))
    manifest = {
        "task": "claude-android-cloud-013-20261020",
        "import_sha": IMPORT_SHA,
        "checkpoint_commit": commit,
        "worker_branch": "claude/android-stabilization-013-20261020",
        "zip_sha256": sha256(archive),
        "zip_bytes": len(archive),
        "file_count": len(files),
        "files": files,
    }
    (out / "CHECKPOINT-MANIFEST.json").write_text(json.dumps(manifest, indent=1, sort_keys=True))
    patch = git(repo, "diff", "--binary", "--full-index", IMPORT_SHA, commit)
    (out / "SOURCE-DELTA.patch").write_bytes(patch)
    added = git(repo, "diff", "--name-only", "--diff-filter=A", IMPORT_SHA, commit).decode().split()
    (out / "NEW-FILES.json").write_text(json.dumps(
        {path: files.get(path) for path in added}, indent=1, sort_keys=True))
    print(json.dumps({k: manifest[k] for k in ("checkpoint_commit", "zip_sha256", "zip_bytes",
                                                "file_count")}))


def restore(zip_path, manifest_path, destination):
    destination = pathlib.Path(destination)
    if destination.exists() and any(destination.iterdir()):
        raise SystemExit("use a new empty destination")
    manifest = json.loads(pathlib.Path(manifest_path).read_text())
    data = pathlib.Path(zip_path).read_bytes()
    if sha256(data) != manifest["zip_sha256"]:
        raise SystemExit("checkpoint zip sha256 mismatch")
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        z.extractall(destination)
    root = destination.resolve()
    actual = {p.relative_to(root).as_posix(): sha256(p.read_bytes())
              for p in root.rglob("*") if p.is_file()}
    expected = manifest["files"]
    problems = ([f"missing {p}" for p in sorted(expected.keys() - actual.keys())]
                + [f"extra {p}" for p in sorted(actual.keys() - expected.keys())]
                + [f"changed {p}" for p in sorted(expected.keys() & actual.keys())
                   if expected[p] != actual[p]])
    result = {"checkpoint_commit": manifest["checkpoint_commit"], "verified_files": len(actual),
              "problems": problems[:20], "status": "PASS" if not problems else "FAIL"}
    print(json.dumps(result))
    return 0 if not problems else 1


if __name__ == "__main__":
    if sys.argv[1] == "create":
        create(*sys.argv[2:5])
    elif sys.argv[1] == "restore":
        sys.exit(restore(*sys.argv[2:5]))
    else:
        raise SystemExit(__doc__)
