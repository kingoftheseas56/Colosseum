"""Strip delivered catalogue DBs/artwork from an assembled API-enabled package.

Never run on a checkout or a user installation. Every removal is an exact,
checksummed path declared by the metadata-only content starter.
"""
import argparse
import hashlib
import json
from pathlib import Path


def stage_api_content(stage, apply=False):
    stage = Path(stage).resolve(strict=True)
    if (stage / ".git").exists() or not (stage / "qml/Main.qml").is_file():
        raise ValueError("Expected an assembled package, not a Git checkout")
    if not any((stage / path).is_file() for path in
               ("native/build-msvc/colosseum.exe", "usr/bin/colosseum")):
        raise ValueError("Staged runtime missing")
    starter = stage / "assets/content-starter/bundle.json"
    bundle = json.loads(starter.read_text(encoding="utf-8"))
    if len(bundle["manifest"]["documents"]) < 10:
        raise ValueError("Complete metadata starter missing")
    targets = []
    for relative, asset in bundle["manifest"]["assets"].items():
        path = (stage / relative).resolve()
        if not path.is_relative_to(stage / "assets") or path.suffix.lower() == ".json":
            raise ValueError("Asset manifest target outside artwork")
        if path.is_file():
            with path.open("rb") as stream:
                actual = hashlib.file_digest(stream, "sha256").hexdigest()
            if actual != asset.get("sourceSha256", asset["sha256"]):
                raise ValueError("Staged artwork differs from served manifest: " + relative)
            targets.append(path)
    for name in ("mal", "imdb", "comics", "tankoban"):
        path = stage / "data" / (name + "_catalog.db")
        if path.is_file():
            targets.append(path)
    result = {"files": len(targets), "bytes": sum(path.stat().st_size for path in targets),
              "applied": apply, "metadataStarter": True}
    if apply:
        for path in targets:
            path.unlink()
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", required=True)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    print(json.dumps(stage_api_content(args.stage, args.apply)))
