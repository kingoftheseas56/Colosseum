"""Local, bounded owned-cover preparation. No cloud upload or source mutation."""
from __future__ import annotations

import argparse
from contextlib import closing
from email.utils import parsedate_to_datetime
import hashlib
import http.client
import io
import ipaddress
import json
import os
import queue
from pathlib import Path
import socket
import sqlite3
import tempfile
import threading
import time
from urllib.parse import urlsplit, urljoin
import warnings

from PIL import Image, ImageOps

COVER_FIELDS = ("cover", "poster", "source_cover")
TABLES = ("series", "curated_series", "curated_edition", "curated_source")


class SafetyError(ValueError):
    pass


class StopFetch(SafetyError):
    def __init__(self, message, retry_after=None):
        super().__init__(message)
        self.retry_after = retry_after


def retry_deadline(value):
    try:
        return time.time() + max(0, int(value))
    except (TypeError, ValueError):
        try:
            return max(time.time(), parsedate_to_datetime(value).timestamp())
        except (TypeError, ValueError, OverflowError):
            return time.time() + 3600


def atomic_write(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".cover-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def save_manifest(path, manifest):
    atomic_write(path, (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode())


def parse_url(url, allow_loopback=False):
    if not isinstance(url, str) or len(url) > 8192 or any(ord(c) < 33 for c in url) or "\\" in url:
        raise SafetyError("Invalid URL")
    parts = urlsplit(url)
    if parts.scheme not in {"http", "https"} or not parts.hostname or parts.username is not None or parts.password is not None or parts.fragment:
        raise SafetyError("Only credential-free HTTP(S) URLs without fragments are accepted")
    try:
        port = parts.port
    except ValueError as exc:
        raise SafetyError("Invalid port") from exc
    if port is not None and not 0 < port < 65536:
        raise SafetyError("Invalid port")
    return parts


def resolve_url(url, allow_loopback=False, timeout=20):
    parts = parse_url(url, allow_loopback)
    answer = queue.Queue(maxsize=1)
    def lookup():
        try:
            answer.put((socket.getaddrinfo(parts.hostname, parts.port or (443 if parts.scheme == "https" else 80), type=socket.SOCK_STREAM), None))
        except Exception as exc:
            answer.put((None, exc))
    # System DNS cannot be cancelled; a daemon ensures it cannot block process exit.
    # A DNS timeout stops the entire run, preventing accumulating resolver threads.
    threading.Thread(target=lookup, daemon=True).start()
    try:
        addresses, error = answer.get(timeout=max(0.001, timeout))
    except queue.Empty as exc:
        raise StopFetch("DNS timeout") from exc
    if error:
        raise error
    if not addresses:
        raise SafetyError("No address")
    for family, kind, proto, _, address in addresses:
        ip = ipaddress.ip_address(address[0])
        if (not ip.is_global or ip.is_multicast) and not (allow_loopback and ip.is_loopback):
            raise SafetyError("Non-public destination rejected")
    return parts, addresses


def origin_url(origin):
    parts = parse_url(origin)
    if parts.scheme != "https" or parts.path not in {"", "/"} or parts.query:
        raise SafetyError("Owned origin must be an HTTPS origin with no path or query")
    # Normalize local aliases without DNS or any provider traffic. Percent-encoded
    # hostnames are disallowed rather than letting another URL parser reinterpret them.
    if "%" in parts.hostname:
        raise SafetyError("Owned origin must have a canonical public hostname")
    try:
        host = parts.hostname.encode("idna").decode("ascii").lower().rstrip(".")
    except UnicodeError as exc:
        raise SafetyError("Invalid owned hostname") from exc
    if host in {"localhost", "local", "internal"} or host.endswith((".localhost", ".local", ".internal")):
        raise SafetyError("Owned origin must be public")
    try:
        ip = ipaddress.ip_address(host)
    except ValueError:
        try:
            # inet_aton parses legacy decimal/octal/hex IPv4 forms, never DNS.
            ip = ipaddress.ip_address(socket.inet_aton(host))
        except OSError:
            ip = None
    if ip is not None and (not ip.is_global or ip.is_multicast):
        raise SafetyError("Owned origin must be public")
    return origin.rstrip("/")


def inventory(json_path=None, sqlite_path=None):
    """Deduplicate actual URLs, preserving readonly source provenance."""
    result = {}
    def add(value, provenance):
        if isinstance(value, str) and value.strip():
            url = value.strip()
            parse_url(url)
            result.setdefault(url, []).append(provenance)
    if json_path:
        values = json.loads(Path(json_path).read_text(encoding="utf-8"))
        if not isinstance(values, list) or any(not isinstance(v, str) for v in values):
            raise SafetyError("JSON input must be an array of URL strings")
        for index, value in enumerate(values):
            add(value, {"kind": "json", "index": index})
    if sqlite_path:
        path = Path(sqlite_path).resolve(strict=True)
        with closing(sqlite3.connect(path.as_uri() + "?mode=ro", uri=True)) as database:
            database.execute("PRAGMA query_only=ON")
            for table in TABLES:
                columns = {row[1] for row in database.execute(f'PRAGMA table_info("{table}")')}
                for column in sorted(columns & {"cover", "source_cover"}):
                    for (value,) in database.execute(f'SELECT "{column}" FROM "{table}" WHERE "{column}" IS NOT NULL'):
                        add(value, {"kind": "sqlite", "table": table, "column": column})
    return result


def download(url, *, allowed_hosts, allow_loopback=False, max_bytes=8_000_000, timeout=20, max_redirects=4):
    """Resolve/validate each hop, then connect to that exact IP (no DNS rebinding)."""
    deadline = time.monotonic() + timeout
    for hop in range(max_redirects + 1):
        parts = parse_url(url, allow_loopback)
        if parts.hostname.lower() not in allowed_hosts:
            raise SafetyError("Host absent from externally supplied allowlist")
        parts, addresses = resolve_url(url, allow_loopback, deadline - time.monotonic())
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise SafetyError("Total download timeout")
        family, kind, proto, _, address = addresses[0]
        connection_type = http.client.HTTPSConnection if parts.scheme == "https" else http.client.HTTPConnection
        connection = connection_type(parts.hostname, parts.port, timeout=remaining)
        def pinned_connection(_address, timeout=None, source_address=None):
            sock = socket.socket(family, kind, proto)
            try:
                sock.settimeout(timeout)
                sock.connect(address)
                return sock
            except BaseException:
                sock.close()
                raise
        connection._create_connection = pinned_connection
        try:
            path = parts.path or "/"
            if parts.query:
                path += "?" + parts.query
            connection.request("GET", path, headers={"User-Agent": "Colosseum-Cover-Preparation/1.0", "Accept": "image/*", "Accept-Encoding": "identity"})
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise SafetyError("Total download timeout")
            connection.sock.settimeout(remaining)
            response = connection.getresponse()
            headers = dict(response.getheaders())
            if response.status in {429, 403, 503} or response.getheader("cf-mitigated", "").lower() == "challenge":
                raise StopFetch(f"Provider stop HTTP {response.status}; Retry-After={response.getheader('Retry-After', '')}", response.getheader("Retry-After"))
            if response.status in {301, 302, 303, 307, 308}:
                location = response.getheader("Location")
                if not location or hop == max_redirects:
                    raise SafetyError("Invalid or excessive redirects")
                target = urljoin(url, location)
                if parts.scheme == "https" and urlsplit(target).scheme != "https":
                    raise SafetyError("HTTPS downgrade rejected")
                url = target
                continue
            if response.status != 200:
                raise SafetyError(f"HTTP {response.status}")
            if not response.getheader("Content-Type", "").lower().startswith("image/"):
                raise StopFetch("Non-image response; possible challenge")
            if response.getheader("Content-Encoding", "identity").lower() != "identity":
                raise SafetyError("Encoded response rejected")
            if int(response.getheader("Content-Length", "0")) > max_bytes:
                raise SafetyError("Download byte cap exceeded")
            body = bytearray()
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise SafetyError("Total download timeout")
                # HTTPResponse owns its socket after Connection: close.
                if response.fp and response.fp.raw:
                    response.fp.raw._sock.settimeout(remaining)
                chunk = response.read1(min(65536, max_bytes + 1 - len(body)))
                if not chunk:
                    break
                body.extend(chunk)
                if len(body) > max_bytes:
                    raise SafetyError("Download byte cap exceeded")
            return bytes(body), {"final_url": url, "etag": response.getheader("ETag"), "last_modified": response.getheader("Last-Modified")}
        finally:
            connection.close()
    raise SafetyError("Redirect cap")


def encode_webp(body, max_pixels=20_000_000):
    with warnings.catch_warnings():
        warnings.simplefilter("error", Image.DecompressionBombWarning)
        with Image.open(io.BytesIO(body)) as image:
            if image.width * image.height > max_pixels or image.width < 1 or image.height < 1:
                raise SafetyError("Decoded pixel cap exceeded")
            image = ImageOps.exif_transpose(image)
            image.thumbnail((264, 392), Image.Resampling.LANCZOS)
            image = image.convert("RGBA" if "A" in image.getbands() else "RGB")
            output = io.BytesIO()
            image.save(output, "WEBP", quality=80, method=6)
            return output.getvalue(), image.size


def verify_entry(entry, root, origin):
    if entry.get("status") != "complete":
        raise SafetyError("Asset incomplete")
    digest = entry.get("sha256", "")
    if len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
        raise SafetyError("Invalid digest")
    key = f"covers/{digest}.webp"
    if entry.get("key") != key or entry.get("owned_url") != origin_url(origin) + "/" + key:
        raise SafetyError("Asset URL/key mismatch")
    path = Path(root) / key
    if path.is_symlink() or not path.resolve().is_relative_to(Path(root).resolve()):
        raise SafetyError("Asset escapes output root")
    if path.stat().st_size != entry.get("bytes") or path.stat().st_size > 8_000_000:
        raise SafetyError("Asset byte mismatch")
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != digest:
        raise SafetyError("Asset hash mismatch")
    with Image.open(io.BytesIO(data)) as image:
        if image.format != "WEBP" or image.size != (entry.get("width"), entry.get("height")) or not (0 < image.width <= 264 and 0 < image.height <= 392):
            raise SafetyError("Asset dimensions/format mismatch")
        image.load()
    return entry["owned_url"]


def verified_urls(manifest, root):
    """Fail closed on corrupt completed entries. Incomplete entries are omitted."""
    return {url: verify_entry(entry, root, manifest["origin"]) for url, entry in manifest["assets"].items() if entry.get("status") == "complete"}


def transform_publication(value, verified):
    """Pure recursive transform. Pass ONLY verified_urls() output as verified."""
    if isinstance(value, list):
        return [transform_publication(item, verified) for item in value]
    if not isinstance(value, dict):
        return value
    result = {key: transform_publication(item, verified) for key, item in value.items()}
    originals = []
    for key in COVER_FIELDS:
        original = value.get(key)
        if isinstance(original, str) and original in verified:
            result[key] = verified[original]
            originals.append(original)
    if isinstance(value.get("covers"), list):
        result["covers"] = [verified.get(item, item) if isinstance(item, str) else transform_publication(item, verified) for item in value["covers"]]
        originals.extend(item for item in value["covers"] if isinstance(item, str) and item in verified)
    if originals:
        candidates = value.get("coverCandidates", [])
        if not isinstance(candidates, list) or any(not isinstance(item, str) for item in candidates):
            raise SafetyError("coverCandidates must be URL strings")
        result["coverCandidates"] = list(dict.fromkeys(candidates + originals))
    return result


def fetch_assets(urls, root, manifest_path, origin, *, allowed_hosts, max_downloads=100, max_download_bytes=64_000_000, max_objects=10000, max_stored_bytes=256_000_000, max_bytes=8_000_000, max_pixels=20_000_000, timeout=20, interval=1.0, allow_loopback=False, revalidate=False, ttl=None):
    root, manifest_path = Path(root), Path(manifest_path)
    origin = origin_url(origin)
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {"version": 1, "origin": origin, "assets": {}}
    if manifest.get("version") != 1 or manifest.get("origin") != origin:
        raise SafetyError("Manifest version/origin mismatch")
    if any(number <= 0 for number in (max_downloads, max_download_bytes, max_objects, max_stored_bytes, max_bytes, max_pixels, timeout)) or interval < 0 or (ttl is not None and ttl < 0):
        raise SafetyError("Invalid limits")
    # Existing generated objects count against storage, including superseded hashes.
    objects = list((root / "covers").glob("*.webp"))
    stored = sum(path.stat().st_size for path in objects)
    if time.time() < manifest.get("retry_not_before", 0):
        return manifest
    count, downloaded = 0, 0
    for url, provenance in urls.items():
        parse_url(url)
        previous = manifest["assets"].get(url)
        if previous and previous.get("status") == "complete":
            verify_entry(previous, root, origin)
            if not revalidate and (ttl is None or time.time() - previous["fetched_at"] < ttl):
                continue
        if count >= max_downloads or downloaded >= max_download_bytes:
            break
        entry = {"status": "pending", "original_url": url, "provenance": provenance}
        try:
            if count:
                time.sleep(interval)
            count += 1
            # Reserve a whole per-request cap even for failed transfers.
            cap = min(max_bytes, max_download_bytes - downloaded)
            downloaded += cap
            body, metadata = download(url, allowed_hosts={host.lower() for host in allowed_hosts}, allow_loopback=allow_loopback, max_bytes=cap, timeout=timeout)
            downloaded -= cap - len(body)
            data, (width, height) = encode_webp(body, max_pixels)
            digest = hashlib.sha256(data).hexdigest()
            key = f"covers/{digest}.webp"
            target = root / key
            if not target.exists():
                if len(objects) >= max_objects or stored + len(data) > max_stored_bytes:
                    raise StopFetch("Storage budget exhausted")
                if target.parent.is_symlink() or not target.resolve().is_relative_to(root.resolve()):
                    raise SafetyError("Asset escapes output root")
                atomic_write(target, data)
                objects.append(target)
                stored += len(data)
            entry.update(metadata, status="complete", key=key, owned_url=origin + "/" + key, sha256=digest, bytes=len(data), width=width, height=height, fetched_at=time.time())
            verify_entry(entry, root, origin)
        except Exception as exc:
            if isinstance(exc, StopFetch):
                manifest["retry_not_before"] = retry_deadline(exc.retry_after)
                manifest["stop_reason"] = str(exc)
            entry.update(status="stopped" if isinstance(exc, StopFetch) else "failed", error=str(exc))
            # A failed explicit refresh never erases a verified previous asset.
            if previous and previous.get("status") == "complete":
                entry = dict(previous, refresh_error=str(exc))
            manifest["assets"][url] = entry
            save_manifest(manifest_path, manifest)
            if isinstance(exc, StopFetch):
                break
            continue
        manifest["assets"][url] = entry
        save_manifest(manifest_path, manifest)
    if not manifest_path.exists():
        save_manifest(manifest_path, manifest)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    for command in ("inventory", "fetch"):
        cmd = sub.add_parser(command)
        cmd.add_argument("--input-json")
        cmd.add_argument("--source-sqlite")
        if command == "fetch":
            cmd.add_argument("--origin", required=True)
            cmd.add_argument("--root", required=True)
            cmd.add_argument("--manifest", required=True)
            cmd.add_argument("--allowed-host", action="append", required=True)
            for name, default, kind in (("max-downloads", 100, int), ("max-download-bytes", 64000000, int), ("max-objects", 10000, int), ("max-stored-bytes", 256000000, int), ("max-bytes", 8000000, int), ("max-pixels", 20000000, int), ("timeout", 20, float), ("interval", 1.0, float), ("ttl", None, float)):
                cmd.add_argument("--" + name, type=kind, default=default)
            cmd.add_argument("--revalidate", action="store_true")
            cmd.add_argument("--allow-loopback-test", action="store_true")
    for command in ("verify", "transform", "export-map"):
        cmd = sub.add_parser(command)
        cmd.add_argument("--root", required=True)
        cmd.add_argument("--manifest", required=True)
        if command == "transform":
            cmd.add_argument("--input", required=True)
            cmd.add_argument("--output", required=True)
    args = vars(parser.parse_args())
    command = args.pop("command")
    if command in {"inventory", "fetch"}:
        inputs = [Path(args[name]).resolve() for name in ("input_json", "source_sqlite") if args.get(name)]
        if not inputs:
            parser.error("At least one source is required")
        urls = inventory(args.pop("input_json"), args.pop("source_sqlite"))
        if command == "inventory":
            print(json.dumps({"unique_urls": len(urls), "urls": urls}, indent=2))
            return
        manifest_path = Path(args["manifest"]).resolve()
        if manifest_path in inputs or manifest_path.exists() and not isinstance(json.loads(manifest_path.read_text()).get("assets"), dict):
            raise SafetyError("Refusing to overwrite input or unknown manifest")
        if any(path == Path(args["root"]).resolve() / "covers" or path.is_relative_to(Path(args["root"]).resolve() / "covers") for path in inputs):
            raise SafetyError("Input overlaps generated assets")
        args["manifest_path"] = args.pop("manifest")
        args["origin"] = args.pop("origin")
        args["allowed_hosts"] = args.pop("allowed_host")
        args["allow_loopback"] = args.pop("allow_loopback_test")
        manifest = fetch_assets(urls, **args)
        print(json.dumps({"complete": sum(e["status"] == "complete" for e in manifest["assets"].values()), "assets": len(manifest["assets"])}))
    else:
        manifest = json.loads(Path(args["manifest"]).read_text())
        verified = verified_urls(manifest, args["root"])
        if command == "export-map":
            if manifest.get("version") != 1:
                raise SafetyError("Manifest version mismatch")
            origin_url(manifest["origin"])
            asset_inventory = {}
            for original in sorted(verified):
                parse_url(original)
                entry = manifest["assets"][original]
                if entry.get("original_url") != original:
                    raise SafetyError("Manifest original URL mismatch")
                asset_inventory[entry["key"]] = {key: entry[key] for key in ("key", "owned_url", "sha256", "bytes", "width", "height")}
            print(json.dumps({"version": 1, "origin": manifest["origin"], "urls": verified, "inventory": [asset_inventory[key] for key in sorted(asset_inventory)]}))
        elif command == "verify":
            print(json.dumps({"verified": len(verified), "incomplete": len(manifest["assets"]) - len(verified)}))
        else:
            output = Path(args["output"]).resolve()
            if output.exists() or output in {Path(args["input"]).resolve(), Path(args["manifest"]).resolve()}:
                raise SafetyError("Transform output must be a new file")
            value = json.loads(Path(args["input"]).read_text())
            atomic_write(output, (json.dumps(transform_publication(value, verified), indent=2) + "\n").encode())


if __name__ == "__main__":
    main()
