"""Build immutable artwork URLs and versioned metadata, without uploading anything.

Only the declared universe artwork tree is published. Inputs remain untouched.
The metadata-only starter has the identical checksums as the served generation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import shutil
import re
from io import BytesIO


def delivery_copy(path):
    """Encode authorized delivery copies; originals and atlas pixels stay untouched."""
    if path.suffix.lower() not in (".png", ".jpg", ".jpeg"):
        return path.read_bytes(), path.suffix.lower().removeprefix("."), None
    from PIL import Image, features
    if not features.check("webp"):
        raise ValueError("WebP encoder is required for artwork publication")
    with Image.open(path) as original:
        if getattr(original, "is_animated", False):
            raise ValueError("Animated artwork needs an explicit delivery policy")
        image = original.convert("RGBA")
        atlas = any(word in path.stem.lower() for word in ("atlas", "map"))
        transparent = image.getchannel("A").getextrema()[0] < 255
        badge = "badge" in path.stem.lower() or any(part in ("badges", "marks") or part.endswith("markers") for part in path.parts)
        lossless = atlas or (transparent and (badge or max(image.size) <= 512))
        if not lossless:
            image.thumbnail((1920, 1920), Image.Resampling.LANCZOS)
            if not transparent:
                image = image.convert("RGB")
        output = BytesIO()
        image.save(output, "WEBP", lossless=lossless, quality=85, method=4, exact=True)
        return output.getvalue(), "webp", {"width": image.width, "height": image.height, "lossless": lossless}


def encode(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False)


def build(native_source, output, starter=None, previous=None):
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
        size = path.stat().st_size
        if size > 25 * 1024 * 1024:
            raise ValueError("Artwork exceeds free static asset limit")
        with path.open("rb") as stream:
            source_hash = hashlib.file_digest(stream, "sha256").hexdigest()
        delivered, suffix, dimensions = delivery_copy(path)
        raw_hash = hashlib.sha256(delivered).hexdigest()
        if len(delivered) > 25 * 1024 * 1024:
            raise ValueError("Artwork exceeds free static asset limit")
        if not suffix.isalnum():
            raise ValueError("Unsupported artwork extension")
        remote = "/assets/content/" + raw_hash + "." + suffix
        target = output / remote.lstrip("/")
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists():
            target.write_bytes(delivered)
        asset = {"path": remote, "sha256": raw_hash, "bytes": len(delivered), "sourceSha256": source_hash, "sourceBytes": size}
        if dimensions:
            asset.update(dimensions)
        assets[path.relative_to(native_source).as_posix()] = asset
    retained = 0
    if previous:
        previous_art = (Path(previous).resolve() / "assets/content").resolve(strict=True)
        for path in previous_art.iterdir():
            if not path.is_file():
                continue
            if not path.resolve().is_relative_to(previous_art) or not re.fullmatch(r"[a-f0-9]{64}\.[a-z0-9]+", path.name):
                raise ValueError("Unsafe previous immutable asset")
            with path.open("rb") as stream:
                actual = hashlib.file_digest(stream, "sha256").hexdigest()
            if actual != path.stem or path.stat().st_size > 25 * 1024 * 1024:
                raise ValueError("Previous immutable asset checksum or size mismatch")
            target = output / "assets/content" / path.name
            if not target.exists():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, target)
                retained += 1
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
            "files": count, "bytes": sum(path.stat().st_size for path in files if path.is_file()),
            "sourceArtworkBytes": sum(asset["sourceBytes"] for asset in assets.values()),
            "deliveredArtworkBytes": sum(asset["bytes"] for asset in assets.values()), "retainedArtworkFiles": retained}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--native-source", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--starter")
    parser.add_argument("--retain-assets-from", help="Previous publication root whose immutable artwork URLs remain valid")
    args = parser.parse_args()
    print(encode(build(args.native_source, args.output, args.starter, args.retain_assets_from)))
