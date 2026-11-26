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
from generate_update_manifest import (  # noqa: E402
    LEGACY_MANIFEST, PLATFORM_MANIFEST, VERSION_RE, check_cpp_header, platform_asset_names,
    requires_platform_manifest, write_signed_manifests,
)
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


def asset_files(installer: Path, manifest_dir: Path, tag: str, public_der: Path | None,
                platform_dir: Path | None) -> list[Path]:
    version = tag.removeprefix("v")
    needs_platforms = requires_platform_manifest(version)
    legacy = manifest_dir / LEGACY_MANIFEST
    legacy_sig = manifest_dir / (LEGACY_MANIFEST + ".sig")
    platform = manifest_dir / PLATFORM_MANIFEST
    platform_sig = manifest_dir / (PLATFORM_MANIFEST + ".sig")
    if needs_platforms and (platform_dir is None or public_der is None):
        raise SystemExit("1.5.0 and later require --platform-dir and --public-key-der")
    if public_der is not None:
        checked = verify(legacy, legacy_sig, installer, public_der, manifest_dir)
        if checked["version"] != version:
            raise SystemExit("signed update version does not match draft tag")
    paths = [installer, legacy, legacy_sig]
    if needs_platforms:
        verify(platform, platform_sig, installer, public_der, manifest_dir, platform_dir)
        platform_release = json_load(platform)
        if platform_release.get("schemaVersion") != 2:
            raise SystemExit("platform manifest must have schemaVersion 2")
        platform_release.pop("downloads")
        platform_release["schemaVersion"] = 1
        if platform_release != json_load(legacy):
            raise SystemExit("platform and Windows manifests describe different releases")
        paths.extend((platform, platform_sig))
        paths.extend(platform_dir / name for name in platform_asset_names(version).values())
    paths.extend(manifest_dir / item["asset"] for item in json_load(legacy).get("artwork", []))
    if len({path.name for path in paths}) != len(paths):
        raise SystemExit("duplicate release asset name")
    missing = [str(path) for path in paths if not path.is_file()]
    if missing:
        raise SystemExit("missing release asset(s): " + ", ".join(missing))
    return paths


def find_release_by_tag(tag: str, token: str, transport=gh,
                        *, want_draft: bool | None = None) -> dict | None:
    """Return the release whose tag_name is `tag`, or None if there is none.

    GitHub's /releases/tags/{tag} endpoint resolves only *published* releases: a
    draft carries a tag_name but no git tag ref yet, so it 404s there and can only
    be found by listing releases (which includes drafts for authenticated pushers).
    Set want_draft to filter to just drafts (True) or just published (False)."""
    page = 1
    while True:
        releases = transport(f"/repos/{APP_REPO}/releases?per_page=100&page={page}", token)
        if not isinstance(releases, list) or not releases:
            return None
        for release in releases:
            if release.get("tag_name") != tag:
                continue
            if want_draft is None or bool(release.get("draft", False)) == want_draft:
                return release
        if len(releases) < 100:
            return None
        page += 1


def publish_draft(tag: str, installer: Path, manifest_dir: Path, token: str,
                  transport=gh, public_der: Path | None = None,
                  platform_dir: Path | None = None) -> dict:
    if not tag.startswith("v"):
        raise SystemExit("draft releases require a vX.Y.Z tag")
    paths = asset_files(installer, manifest_dir, tag, public_der, platform_dir)
    release = find_release_by_tag(tag, token, transport)
    if release is None:
        release = transport(f"/repos/{APP_REPO}/releases", token, method="POST", payload={
            "tag_name": tag,
            "name": tag,
            "body": "Signed Colosseum update draft.",
            "draft": True,
            "prerelease": False,
        })
    if not release.get("draft", False):
        raise SystemExit("refusing to mutate an already-published release")

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


def publish_verified_draft(tag: str, token: str, transport=gh, *,
                           installer: Path | None = None, manifest_dir: Path | None = None,
                           public_der: Path | None = None, platform_dir: Path | None = None) -> int:
    if not tag.startswith("v"):
        raise SystemExit("draft releases require a vX.Y.Z tag")
    expected_assets = []
    if requires_platform_manifest(tag[1:]):
        if installer is None or manifest_dir is None:
            raise SystemExit("publishing 1.5.0 and later requires --installer and --manifest-dir")
        expected_assets = asset_files(installer, manifest_dir, tag, public_der, platform_dir)
    release = find_release_by_tag(tag, token, transport)
    if release is None:
        raise SystemExit(f"no release found for {tag}")
    if not release.get("draft", False):
        raise SystemExit("refusing to publish a release that is not a draft")
    uploaded_assets = {item["name"]: item for item in release.get("assets", [])}
    for path in expected_assets:
        remote = uploaded_assets.get(path.name, {})
        if (remote.get("size") != path.stat().st_size
            or remote.get("digest") != "sha256:" + sha256(path)):
            raise SystemExit(f"draft asset missing or did not match signed release: {path.name}")
    payload = {"draft": False}
    if expected_assets:
        payload.update(prerelease=False, make_latest="true")
    transport(f"/repos/{APP_REPO}/releases/{release['id']}", token, method="PATCH", payload=payload)
    print(f"PUBLISHED_RELEASE={release.get('html_url', release.get('id', 'unknown'))}")
    return 0


def draft_mode(args: argparse.Namespace) -> int:
    manifest_dir = Path(args.manifest_dir)
    installer = Path(args.installer)
    if args.private_key and args.presentation:
        write_signed_manifests(args.version, installer, Path(args.presentation),
                               Path(args.private_key), manifest_dir, args.platform_dir)
    public_der = Path(args.public_key_der) if args.public_key_der else None
    return 0 if publish_draft(f"v{args.version}", installer, manifest_dir, resolve_token(),
                               public_der=public_der, platform_dir=args.platform_dir) else 1


def legacy_mode(argv: list[str]) -> int:
    if len(argv) < 3:
        print(__doc__)
        return 2
    tag, zip_path = argv[1], argv[2]
    version = tag.removeprefix("v")
    if VERSION_RE.fullmatch(version) and requires_platform_manifest(version):
        raise SystemExit("use the signed draft flow for 1.5.0 and later")
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
    parser.add_argument("--public-key-der", type=Path)
    parser.add_argument("--platform-dir", type=Path)
    args = parser.parse_args()
    if requires_platform_manifest(args.version):
        if args.public_key_der is None:
            parser.error("1.5.0 and later require --public-key-der")
        header = Path(__file__).resolve().parents[1] / "native/update/UpdatePublicKey.h"
        check_cpp_header(args.public_key_der, header)
    if args.publish_verified_draft:
        return publish_verified_draft(
            f"v{args.version}", resolve_token(),
            installer=Path(args.installer) if args.installer else None,
            manifest_dir=Path(args.manifest_dir) if args.manifest_dir else None,
            public_der=args.public_key_der, platform_dir=args.platform_dir,
        )
    if not args.installer or not args.manifest_dir:
        parser.error("--draft requires --installer and --manifest-dir")
    return draft_mode(args)


if __name__ == "__main__":
    raise SystemExit(main())
