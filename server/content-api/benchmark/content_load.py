"""Prepare bounded content fixtures; never fetch URLs or execute QML/JavaScript."""
import argparse
import hashlib
import json
import re
import shutil
import struct
import time
from pathlib import Path

MAX_BODY = 2 * 1024 * 1024
MAX_ASSET = 64 * 1024 * 1024
CASES = {
    'cold-start': {'cache': 'empty', 'expect': '200 bounded requested bodies'},
    'warm-etag': {'cache': 'current', 'expect': 'conditional request; 304 zero body'},
    'revision-change': {'cache': 'prior revision', 'expect': '200 new requested units; consistent revision'},
    'missing-asset': {'cache': 'current', 'expect': '404; installed visual fallback'},
    'unsupported-renderer': {'cache': 'last-good', 'expect': 'reject unsupported renderer; retain last-good'},
    'last-good': {'cache': 'last-good', 'expect': 'network failure; retain last-good'},
}
TRIBUTES = {
    'one-piece': ['OnePieceEastBlueData.js', 'OnePieceParadiseData.js', 'OnePieceNewWorldData.js', 'OnePieceFinalSeaData.js'],
    'dcau': ['DCAUUniverseData.js', 'DCAUUniversePage.qml'],
    'star-wars': ['GalaxyUniversePage.qml', 'StarWarsGalaxySystem.qml'],
    'cosmere': ['Universes.js', 'CosmereApi.js', 'CosmereUniversePage.qml'],
}

def digest(body):
    return hashlib.sha256(body).hexdigest()

def write_json(path, value):
    body = (json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(',', ':')) + '\n').encode()
    if len(body) > MAX_BODY:
        raise ValueError('JSON unit exceeds body bound')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(body)
    return body

def image_info(body, suffix):
    """Header dimensions only; this is not Qt decoding or renderer validation."""
    if body.startswith(b'\x89PNG\r\n\x1a\n') and len(body) >= 24:
        return 'image/png', list(struct.unpack('>II', body[16:24]))
    if body.startswith(b'\xff\xd8'):
        pos = 2
        while pos + 4 <= len(body):
            if body[pos] != 255:
                pos += 1
                continue
            marker = body[pos + 1]
            pos += 2
            if marker in (0xd8, 0xd9) or 0xd0 <= marker <= 0xd7:
                continue
            size = int.from_bytes(body[pos:pos + 2], 'big')
            if size < 2 or pos + size > len(body):
                break
            if marker in (0xc0, 0xc1, 0xc2) and size >= 7:
                return 'image/jpeg', [int.from_bytes(body[pos + 5:pos + 7], 'big'), int.from_bytes(body[pos + 3:pos + 5], 'big')]
            pos += size
        return 'image/jpeg', None
    if suffix == '.svg':
        text = body[:65536].decode('utf-8', errors='replace')
        match = re.search(r'viewBox=["\']\s*[-\d.]+\s+[-\d.]+\s+([\d.]+)\s+([\d.]+)', text)
        return 'image/svg+xml', [float(v) for v in match.groups()] if match else None
    return 'application/octet-stream', None

def prepare(repo, assets_root, output):
    repo, assets_root, output = map(Path, (repo, assets_root, output))
    roster_source = (repo / 'qml/Universes.js').read_bytes()
    # Only top-level roster declarations; nested saga/world names are excluded.
    routes = re.findall(r'^    \{ name: "([^"]+)", c1: "[^"]+", category: "([^"]+)"', roster_source.decode('utf-8'), re.M)
    if not routes or len({name for name, _ in routes}) != len(routes):
        raise ValueError('missing or duplicate roster routes')
    manifest = {'schema_version': 1, 'measurement_scope': 'local disk and JSON parsing only',
                'deployment_mapping_required': True, 'routes': [], 'units': [], 'assets': [], 'extraction_gaps': [],
                'source_roster_sha256': digest(roster_source)}
    def unit(uid, value):
        path = 'units/' + uid + '.json'
        body = write_json(output / path, value)
        manifest['units'].append({'id': uid, 'body_path': path, 'bytes': len(body), 'sha256': digest(body), 'etag': '"' + digest(body) + '"'})
        return uid
    stems = {'One Piece': 'one-piece', 'DC Animated Universe': 'dcau', 'Star Wars': 'star-wars'}
    for name, category in routes:
        stem = stems.get(name)
        requested = []
        if stem:
            raw = (assets_root / (stem + '.json')).read_bytes()
            if len(raw) > MAX_BODY:
                raise ValueError('source JSON exceeds body bound')
            payload = json.loads(raw)['universe']
            header = {k: v for k, v in payload.items() if k != 'sections'}
            header['sections'] = [{k: v for k, v in s.items() if k != 'entries'} for s in payload['sections']]
            requested.append(unit(stem + '/header', header))
            seen = set()
            for section in payload['sections']:
                sid = section['id']
                if not re.fullmatch(r'[a-zA-Z0-9_-]+', sid) or sid in seen:
                    raise ValueError('invalid/duplicate section id')
                seen.add(sid)
                requested.append(unit(stem + '/sections/' + sid, section))
        manifest['routes'].append({'name': name, 'category': category, 'face': 'standard', 'requested_units': requested[:2], 'available_units': requested,
                                   'status': 'json-fixture' if stem else 'embedded-content-extraction-required'})
    for stem, sources in TRIBUTES.items():
        evidence = []
        for source in sources:
            path = repo / 'qml' / source
            body = path.read_bytes()
            evidence.append({'source': 'qml/' + source, 'bytes': len(body), 'sha256': digest(body)})
        manifest['extraction_gaps'].append({'id': 'tribute/' + stem, 'sources': evidence, 'status': 'declarative-extraction-and-renderer-contract-required'})
        manifest['routes'].append({'name': stem, 'face': 'tribute', 'requested_units': [], 'status': 'embedded-content-extraction-required'})
    for source in sorted(assets_root.rglob('*')):
        if not source.is_file() or source.suffix.lower() not in ('.png', '.jpg', '.jpeg', '.svg', '.webp'):
            continue
        if source.stat().st_size > MAX_ASSET:
            raise ValueError('asset exceeds 64 MiB fixture bound')
        body = source.read_bytes()
        sha = digest(body)
        mime, dimensions = image_info(body, source.suffix.lower())
        target = 'assets/' + sha + source.suffix.lower()
        (output / 'assets').mkdir(parents=True, exist_ok=True)
        if not (output / target).exists():
            shutil.copyfile(source, output / target)
        manifest['assets'].append({'id': source.relative_to(assets_root).as_posix(), 'body_path': target, 'bytes': len(body),
                                   'sha256': sha, 'mime': mime, 'dimensions': dimensions, 'variant': 'original',
                                   'route_assignment': 'requires renderer request capture'})
    write_json(output / 'manifest.json', manifest)
    traces = {'schema_version': 1, 'deployment_mapping_required': True, 'cases': []}
    for route in manifest['routes']:
        for case, contract in CASES.items():
            traces['cases'].append({'route': route['name'], 'face': route['face'], 'case': case, **contract,
                                    'resource_ids': route['requested_units'], 'asset_ids': [],
                                    'asset_selection': 'requires renderer request capture',
                                    'runnable': bool(route['requested_units']), 'http_mapping': None})
    write_json(output / 'request-traces.json', traces)
    return manifest

def measure(output, repeats=5):
    output = Path(output)
    manifest = json.loads((output / 'manifest.json').read_bytes())
    rows = []
    for item in manifest['units'] + manifest['assets']:
        path = (output / item['body_path']).resolve()
        if not path.is_relative_to(output.resolve()):
            raise ValueError('body path escapes fixture root')
        bound = MAX_BODY if item in manifest['units'] else MAX_ASSET
        if path.stat().st_size > bound:
            raise ValueError('fixture exceeds body bound')
        samples = []
        for _ in range(repeats):
            start = time.perf_counter_ns()
            body = path.read_bytes()
            read_ns = time.perf_counter_ns() - start
            if len(body) != item['bytes'] or digest(body) != item['sha256']:
                raise ValueError('fixture body mismatch')
            parse_ns = None
            if item in manifest['units']:
                start = time.perf_counter_ns()
                json.loads(body)
                parse_ns = time.perf_counter_ns() - start
            samples.append({'disk_read_ns': read_ns, 'json_parse_ns': parse_ns})
        rows.append({'id': item['id'], 'bytes': item['bytes'], 'samples': samples})
    result = {'scope': 'local-disk-read-and-json-parse; OS cache uncontrolled', 'network_first_paint_qt_decode_render': 'unmeasured', 'rows': rows}
    write_json(output / 'local-measurements.json', result)
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('prepare')
    p.add_argument('--repo', required=True)
    p.add_argument('--assets-root', required=True)
    p.add_argument('--output', required=True)
    p = sub.add_parser('measure')
    p.add_argument('--output', required=True)
    p.add_argument('--repeats', type=int, default=5)
    args = parser.parse_args()
    if args.command == 'prepare':
        result = prepare(args.repo, args.assets_root, args.output)
        print(json.dumps({'routes': len(result['routes']), 'units': len(result['units']), 'assets': len(result['assets'])}))
    else:
        if not 1 <= args.repeats <= 100:
            parser.error('repeats must be 1..100')
        print(json.dumps({'measured_resources': len(measure(args.output, args.repeats)['rows'])}))

if __name__ == '__main__':
    main()
