#!/usr/bin/env python3
"""Generate the bundled installed-release chronicle.

Produces a signed manifest, its Ed25519 signature, and verified artwork copies under
``resources/installed-chronicle/`` — the seed that ``InstalledChronicle`` loads at
startup so the Update gallery renders the installed release's chapters at rest.

Reuses the existing ``manifest_bytes`` + ``sign_raw`` trust path from
``generate_update_manifest.py``. The manifest carries an inert installer block
(per the approved 2026-08-09 design decision "fill it, ignore it") so ``parseManifest``
accepts it unchanged — the gallery reads only ``highlights`` + ``artwork``.

Usage (maintainer run with the production private key)::

    python scripts/update/generate_installed_chronicle.py \\
        --presentation release/presentation/1.1.0.json \\
        --version 1.1.0 \\
        --private-key /path/to/colosseum-update-private.pem \\
        --output-dir resources/installed-chronicle

The output ``installed-manifest.json`` + ``.sig`` + ``artwork/*.png`` are committed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path

# Reuse the proven signing + manifest-building path.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_update_manifest import manifest_bytes, sign_raw  # noqa: E402


def copy_verified_artwork(presentation: Path, output_dir: Path) -> list[Path]:
    """Copy each artwork file referenced by the presentation into the bundle dir.

    Returns the list of copied paths. Raises if a referenced artwork file is missing
    on disk — the existing ``manifest_bytes`` call also rejects missing files, but
    copying first gives a clearer error and ensures the bundle is self-contained.
    """
    data = json.loads(presentation.read_text(encoding="utf-8"))
    artwork_dir = output_dir / "artwork"
    artwork_dir.mkdir(parents=True, exist_ok=True)
    repo_root = presentation.resolve().parents[2]
    copied: list[Path] = []
    for entry in data.get("artwork", []):
        source = repo_root / entry["path"]
        if not source.is_file():
            raise FileNotFoundError(f"missing artwork: {source}")
        dest = artwork_dir / entry["asset"]
        shutil.copyfile(source, dest)
        copied.append(dest)
    return copied


def make_placeholder_installer(version: str, temp_dir: Path) -> Path:
    """Create a deterministic placeholder installer so manifest_bytes accepts the manifest.

    The installed chronicle has no real installer — it describes what is already
    installed. This placeholder satisfies the schema; its hash is inert (the gallery
    never downloads or verifies the installer from the installed chronicle).
    At release time a maintainer passes ``--installer`` pointing at the real setup.exe.
    """
    name = f"Colosseum-{version}-setup.exe"
    path = temp_dir / name
    temp_dir.mkdir(parents=True, exist_ok=True)
    path.write_bytes(f"colosseum-installed-chronicle-placeholder-{version}\n".encode("ascii"))
    return path


def generate(presentation: Path, version: str, private_key: Path,
             output_dir: Path, installer: Path | None = None) -> dict[str, str]:
    """Generate the signed installed-chronicle bundle. Returns a dict of output paths."""
    output_dir.mkdir(parents=True, exist_ok=True)

    # Build the manifest bytes (reuses the exact trust path as published releases).
    # manifest_bytes requires an installer file named Colosseum-<version>-setup.exe.
    if installer is not None:
        installer_path = installer
    else:
        installer_path = make_placeholder_installer(version, output_dir / "_placeholder")
    manifest = manifest_bytes(version, installer_path, presentation)
    signature = sign_raw(manifest, private_key)

    # Copy verified artwork into the bundle.
    copy_verified_artwork(presentation, output_dir)

    # Write the signed manifest + signature.
    manifest_out = output_dir / "installed-manifest.json"
    sig_out = output_dir / "installed-manifest.json.sig"
    manifest_out.write_bytes(manifest)
    sig_out.write_bytes(signature)

    # Clean up placeholder if we created one (it is NOT part of the shipped bundle).
    if installer is None:
        placeholder_dir = output_dir / "_placeholder"
        if placeholder_dir.exists():
            shutil.rmtree(placeholder_dir)

    return {
        "MANIFEST_PATH": str(manifest_out),
        "SIGNATURE_PATH": str(sig_out),
        "MANIFEST_SHA256": hashlib.sha256(manifest).hexdigest(),
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--presentation", default="release/presentation/1.1.0.json")
    parser.add_argument("--version", default="1.1.0")
    parser.add_argument("--private-key", required=True)
    parser.add_argument("--output-dir", default="resources/installed-chronicle")
    parser.add_argument("--installer", default=None,
                        help="Real setup.exe for release-time runs; omit for placeholder.")
    args = parser.parse_args(argv)

    try:
        result = generate(
            presentation=Path(args.presentation),
            version=args.version,
            private_key=Path(args.private_key),
            output_dir=Path(args.output_dir),
            installer=Path(args.installer) if args.installer else None,
        )
        for key, value in result.items():
            print(f"{key}={value}")
        return 0
    except (OSError, ValueError) as exc:
        print(f"generate_installed_chronicle: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
