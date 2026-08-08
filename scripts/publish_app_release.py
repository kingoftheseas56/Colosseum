"""Publish Colosseum releases, with a signed draft-only updater path."""
from __future__ import annotations

import argparse
import hashlib
import json
import mimetypes
import os
from pathlib import Path
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "data_vault"))
from vault_common import gh, resolve_token  # noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "update"))
from generate_update_manifest import manifest_bytes, sign_raw  # noqa: E402
from verify_update_release import verify  # noqa: E402

APP_REPO = "kingoftheseas56/Colosseum"
ASSET_TYPES = {
    ".exe": "application/vnd.microsoft.portable-executable",
    ".json": "application/json",
    ".sig": "application/octet-stream",
    ".png": "image/png",
    ".jpg": "image/jpeg",
    ".jpeg": "image/jpeg",
    ".webp": "image/webp",
}


def json_load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def asset_files(installer: Path, manifest_dir: Path) -> list[Path]:
    manifest_path = manifest_dir / "colosseum-update-v1.json"
    paths = [installer, manifest_path, manifest_dir / "colosseum-update-v1.json.sig"]
    paths.extend(manifest_dir / item["asset"] for item in json_load(manifest_path).get("artwork", []))
    missing = [str(path) for path in paths if not path.is_file()]
    if missing:
        raise SystemExit("missing release asset(s): " + ", ".join(missing))
    return paths


def publish_draft(tag: str, installer: Path, manifest_dir: Path, token: str,
                  transport=gh, public_der: Path | None = None) -> dict:
    if not tag.startswith("v"):
        raise SystemExit("draft releases require a vX.Y.Z tag")
    try:
        release = transport(f"/repos/{APP_REPO}/releases/tags/{tag}", token)
    except Exception:
        release = transport(f"/repos/{APP_REPO}/releases", token, method="POST", payload={
            "tag_name": tag,
            "name": tag,
            "body": "Signed Colosseum update draft.",
            "draft": True,
            "prerelease": False,
        })
    if not release.get("draft", False):
        raise SystemExit("refusing to mutate an already-published release")

    paths = asset_files(installer, manifest_dir)
    if public_der is not None:
        verify(manifest_dir / "colosseum-update-v1.json",
               manifest_dir / "colosseum-update-v1.json.sig", installer, public_der,
               manifest_dir)
    for path in paths:
        existing = next((item for item in release.get("assets", [])
                         if item.get("name") == path.name), None)
        if existing:
            transport(f"/repos/{APP_REPO}/releases/assets/{existing['id']}", token,
                      method="DELETE")
        upload = release["upload_url"].split("{")[0] + "?name=" + path.name
        content_type = ASSET_TYPES.get(path.suffix.lower(),
                                       mimetypes.guess_type(path.name)[0]
                                       or "application/octet-stream")
        uploaded = transport("", token, raw_url=upload, method="POST", content_type=content_type,
                             data=path.read_bytes())
        if isinstance(uploaded, dict) and uploaded.get("size") is not None:
            if int(uploaded["size"]) != path.stat().st_size:
                raise SystemExit(f"GitHub size mismatch after upload: {path.name}")
        if isinstance(uploaded, dict) and uploaded.get("digest"):
            remote_digest = uploaded["digest"].split(":", 1)[-1]
            if remote_digest.lower() != sha256(path):
                raise SystemExit(f"GitHub digest mismatch after upload: {path.name}")
    print(f"DRAFT_RELEASE={release.get('html_url', release.get('id', 'unknown'))}")
    return release


def publish_verified_draft(tag: str, token: str, transport=gh) -> int:
    release = transport(f"/repos/{APP_REPO}/releases/tags/{tag}", token)
    if not release.get("draft", False):
        raise SystemExit("refusing to publish a release that is not a draft")
    transport(f"/repos/{APP_REPO}/releases/{release['id']}", token, method="PATCH",
              payload={"draft": False})
    print(f"PUBLISHED_RELEASE={release.get('html_url', release.get('id', 'unknown'))}")
    return 0


def draft_mode(args: argparse.Namespace) -> int:
    manifest_dir = Path(args.manifest_dir)
    installer = Path(args.installer)
    if args.private_key and args.presentation:
        manifest_path = manifest_dir / "colosseum-update-v1.json"
        manifest_path.parent.mkdir(parents=True, exist_ok=True)
        manifest_path.write_bytes(manifest_bytes(args.version, installer, Path(args.presentation)))
        (manifest_dir / "colosseum-update-v1.json.sig").write_bytes(
            sign_raw(manifest_path.read_bytes(), Path(args.private_key)))
    public_der = Path(args.public_key_der) if args.public_key_der else None
    return 0 if publish_draft(f"v{args.version}", installer, manifest_dir, resolve_token(),
                               public_der=public_der) else 1


def legacy_mode(argv: list[str]) -> int:
    if len(argv) < 3:
        print(__doc__)
        return 2
    tag, zip_path = argv[1], argv[2]
    title, notes = tag, ""
    args = list(argv[3:])
    while args:
        option = args.pop(0)
        if option == "--title" and args:
            title = args.pop(0)
        elif option == "--notes" and args:
            notes = args.pop(0)
    if not os.path.exists(zip_path):
        raise SystemExit(f"asset not found: {zip_path}")
    token = resolve_token()
    try:
        release = gh(f"/repos/{APP_REPO}/releases/tags/{tag}", token)
        print(f"release exists: {release['html_url']}")
    except Exception:
        release = gh(f"/repos/{APP_REPO}/releases", token, method="POST", payload={
            "tag_name": tag, "name": title, "body": notes,
            "draft": False, "prerelease": False,
        })
        print(f"release created: {release['html_url']}")
    name = os.path.basename(zip_path)
    for asset in release.get("assets", []):
        if asset["name"] == name:
            gh(f"/repos/{APP_REPO}/releases/assets/{asset['id']}", token, method="DELETE")
    upload = release["upload_url"].split("{")[0] + f"?name={name}"
    asset = gh("", token, raw_url=upload, method="POST", content_type="application/zip",
               data=Path(zip_path).read_bytes())
    print(f"uploaded {name} ({asset['size'] / 1024 / 1024:.1f} MB): {asset['browser_download_url']}")
    return 0


def main() -> int:
    if "--draft" not in sys.argv and "--publish-verified-draft" not in sys.argv:
        return legacy_mode(sys.argv)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--draft", action="store_true")
    parser.add_argument("--publish-verified-draft", action="store_true")
    parser.add_argument("--version", required=True)
    parser.add_argument("--installer")
    parser.add_argument("--manifest-dir")
    parser.add_argument("--private-key")
    parser.add_argument("--presentation")
    parser.add_argument("--public-key-der")
    args = parser.parse_args()
    if args.publish_verified_draft:
        return publish_verified_draft(f"v{args.version}", resolve_token())
    if not args.installer or not args.manifest_dir:
        parser.error("--draft requires --installer and --manifest-dir")
    return draft_mode(args)


if __name__ == "__main__":
    raise SystemExit(main())
