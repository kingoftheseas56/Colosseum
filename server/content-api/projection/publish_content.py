"""Build immutable artwork URLs and versioned metadata, without uploading anything.

Only the declared universe artwork tree is published. Inputs remain untouched.
The metadata-only starter has the identical checksums as the served generation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def encode(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False)


def build(native_source, output, starter=None):
    native_source, output = Path(native_source).resolve(), Path(output).resolve()
    if output.exists():
        raise ValueError("Use a fresh output directory")
    documents_dir = output / "internal/content/documents"
    documents_dir.mkdir(parents=True)
    subprocess.run(["node", str(native_source / "scripts/export_universe_content.cjs"), str(documents_dir)],
                   check=True, capture_output=True, text=True, encoding="utf-8")
    descriptions, texts = {}, {}
    for path in sorted(documents_dir.glob("*.json")):
        raw = path.read_bytes()
        value = json.loads(raw)
        if len(raw) > 512 * 1024 or value["schemaVersion"] != 1:
            raise ValueError("Content envelope exceeds contract")
        descriptions[path.stem] = {"revision": value["revision"], "sha256": hashlib.sha256(raw).hexdigest()}
        texts[path.stem] = raw.decode("utf-8")
    if not descriptions:
        raise ValueError("No content documents")
    assets = {}
    asset_root = native_source / "assets/universes"
    artwork = set(path for path in asset_root.rglob("*") if path.is_file() and path.suffix.lower() != ".json")
    # Saga wallpapers are outside the tribute folder. Collect declared file
    # references too; directory prefixes are resolved by exported artByArc data.
    def references(value):
        if isinstance(value, dict):
            for item in value.values(): references(item)
        elif isinstance(value, list):
            for item in value: references(item)
        elif isinstance(value, str) and value.startswith("../assets/") and not value.endswith("/"):
            path = (native_source / "qml" / value).resolve()
            if not path.is_relative_to(native_source / "assets") or not path.is_file():
                raise ValueError("Declared artwork is missing or outside assets: " + value)
            artwork.add(path)
    for raw in texts.values(): references(json.loads(raw)["data"])
    for path in sorted(artwork):
        if not path.is_file() or path.suffix.lower() == ".json":
            continue
        with path.open("rb") as stream:
            raw_hash = hashlib.file_digest(stream, "sha256").hexdigest()
        size = path.stat().st_size
        if size > 25 * 1024 * 1024:
            raise ValueError("Artwork exceeds free static asset limit")
        suffix = path.suffix.lower().removeprefix(".")
        if not suffix.isalnum():
            raise ValueError("Unsupported artwork extension")
        remote = "/assets/content/" + raw_hash + "." + suffix
        target = output / remote.lstrip("/")
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists():
            shutil.copyfile(path, target)
        assets[path.relative_to(native_source).as_posix()] = {"path": remote, "sha256": raw_hash, "bytes": size}
    manifest = {"schemaVersion": 1, "documents": descriptions, "assets": assets}
    manifest["revision"] = hashlib.sha256(encode(manifest).encode()).hexdigest()
    bundle = {"manifest": manifest, "documents": texts}
    content = output / "internal/content"
    (content / "manifest.json").write_text(encode(manifest), encoding="utf-8")
    (content / "rpc-bundle.json").write_text(encode({"apiVersion": 1, "revision": manifest["revision"],
                                                  "method": "bundle", "result": bundle}), encoding="utf-8")
    if starter:
        starter = Path(starter)
        starter.parent.mkdir(parents=True, exist_ok=True)
        starter.write_text(encode(bundle), encoding="utf-8")
    (output / "_headers").write_text("/assets/content/*\n  Cache-Control: public, max-age=31536000, immutable\n"
                                       "  X-Content-Type-Options: nosniff\n", encoding="utf-8")
    files = list(output.rglob("*"))
    count = sum(path.is_file() for path in files)
    if count > 20000:
        raise ValueError("Too many free static asset files")
    return {"revision": manifest["revision"], "documents": len(texts), "artworkMappings": len(assets),
            "files": count, "bytes": sum(path.stat().st_size for path in files if path.is_file())}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--native-source", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--starter")
    args = parser.parse_args()
    print(encode(build(args.native_source, args.output, args.starter)))
