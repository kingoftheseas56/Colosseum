#!/usr/bin/env python3
"""Stage the canonical collection viewer into a reviewed static-site baseline.

Writes a NEW directory only. This is artifact generation, not deployment or
runtime qualification. The manifest is the final completion marker.
"""
import argparse
import base64
import difflib
import hashlib
import html
import json
from pathlib import Path
import re
import stat
import subprocess
from urllib.parse import urlsplit


PUBLIC_ORIGIN = 'https://getcolosseum.world'
MANIFEST = 'collections-stage-manifest.json'
STAGE_DIFF = 'collections-stage.diff'
EXCLUDED_DIRS = {'.git', '.hg', '.svn', '.codex', '.superpowers', '.idea',
                 '.vscode', 'node_modules', '__pycache__', '.pytest_cache',
                 '.cache', '.wrangler', 'build', 'dist', 'target', 'coverage', 'test-results',
                 'playwright-report', 'review-hub', 'review_hub', 'reviewhub',
                 'interactive-previews', 'interactive_previews', 'preview', 'private'}
EXCLUDED_FILES = {'preview.html', '.ds_store', 'thumbs.db', '.gitignore',
                  '.gitattributes', '.gitmodules', 'credentials.json',
                  'credentials', 'secrets.json', 'secrets', '.npmrc', '.pypirc',
                  '.netrc', 'id_rsa', 'id_ed25519', MANIFEST, STAGE_DIFF}
SECRET_SUFFIXES = {'.pem', '.key', '.p12', '.pfx', '.jks', '.keystore',
                   '.sqlite', '.sqlite3', '.db', '.log', '.pyc'}
BRAND_FILES = ('assets/cold-ripple.jpg', 'assets/Fraunces-Regular.ttf',
               'assets/Fraunces-Italic.ttf', 'assets/colosseum.svg')
RETURN_PANEL = '''
  <section id="collection-return" class="collection-return glass" hidden aria-labelledby="collection-return-title">
    <div class="kicker">Your shared collection</div>
    <h2 id="collection-return-title">Keep your collection link handy.</h2>
    <p>Install Colosseum, then return to the collection to choose whether to save it. Installing does not save or open titles automatically.</p>
    <div class="card-actions"><a id="collection-return-link" class="btn primary">Return to your collection</a><button id="collection-return-copy" class="btn" type="button">Copy collection link</button></div>
    <p id="collection-return-status" role="status"></p>
    <div id="collection-return-fallback" hidden><label for="collection-return-url">Collection link — select and copy</label><input id="collection-return-url" type="text" readonly spellcheck="false" autocomplete="off"></div>
  </section>
'''
RETURN_STYLE = '''
  #collection-return[hidden],#collection-return-fallback[hidden]{display:none!important}
  .collection-return{padding:28px;margin:0 0 30px;border-radius:12px}
  .collection-return h2{margin:12px 0;font-size:28px}
  .collection-return p{max-width:70ch;color:var(--ink-dim);margin:12px 0}
  #collection-return-status{min-height:1.6em;font-size:13px}
  #collection-return-fallback label{display:block;margin:12px 0 8px}
  #collection-return-url{width:100%;min-width:0;padding:12px;font:inherit;color:var(--ink);background:var(--glass-tint);border:1px solid var(--edge);border-radius:6px}
  @media(max-width:600px){.collection-return{padding:22px}.collection-return .card-actions{flex-direction:column;align-items:stretch}}
'''


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write_new(root, name, data):
    destination = root / name
    no_links(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    no_links(destination)
    with destination.open('xb') as stream:
        stream.write(data)


def no_links(path):
    """Reject links/reparse points in an input or output's existing ancestry."""
    for component in reversed((path, *path.parents)):
        try:
            info = component.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise ValueError('Symlinks/reparse points are not permitted in staging paths')


def checked_path(value, must_exist=False):
    path = Path(value).expanduser().absolute()
    no_links(path)
    resolved = path.resolve(strict=must_exist)
    no_links(resolved)
    return resolved


def overlaps(left, right):
    return left == right or left in right.parents or right in left.parents


def api_origin(value):
    if not re.fullmatch(r'https://(?:[A-Za-z0-9][A-Za-z0-9.-]*|\[[0-9A-Fa-f:]+\])(?::[0-9]{1,5})?/?', value):
        raise ValueError('API origin must be an explicit HTTPS origin without credentials or URL suffixes')
    parsed = urlsplit(value)
    if not parsed.hostname or parsed.username is not None or parsed.password is not None:
        raise ValueError('API origin cannot contain credentials')
    if parsed.path not in ('', '/') or parsed.query or parsed.fragment:
        raise ValueError('API origin must not contain a path, query or fragment')
    # Accessing port also rejects malformed/out-of-range port spellings.
    parsed.port
    return value.rstrip('/')


def excluded(relative, directory):
    name = relative.name.lower()
    if directory and name in EXCLUDED_DIRS:
        return 'private, VCS or build/cache directory'
    if (name in EXCLUDED_FILES or name.startswith(('.env', '.git', 'credentials.', 'secrets.', 'service-account'))):
        return 'private, credential, VCS or prior manifest file'
    if relative.suffix.lower() in SECRET_SUFFIXES:
        return 'credential, local state or cache file'
    if relative.as_posix() == '.well-known/assetlinks.json':
        return 'requires independently qualified public association input'
    return ''


def inventory(base):
    files, omissions = {}, []

    def walk(directory, relative):
        for child in sorted(directory.iterdir(), key=lambda p: p.name):
            info = child.lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
                raise ValueError('Baseline contains a symlink/reparse point')
            rel = relative / child.name
            if any(ord(char) < 32 for char in child.name) or '\\' in child.name:
                raise ValueError('Baseline contains an unsafe portable filename')
            if rel.as_posix() in ('_worker.js', 'functions'):
                raise ValueError('Worker/Functions source is outside this static-site staging contract')
            is_dir = stat.S_ISDIR(info.st_mode)
            reason = excluded(rel, is_dir)
            if reason:
                omissions.append({'path': rel.as_posix(), 'reason': reason})
            elif is_dir:
                walk(child, rel)
            elif stat.S_ISREG(info.st_mode):
                files[rel.as_posix()] = child
            else:
                raise ValueError('Baseline contains a nonregular file')

    walk(base, Path())
    return files, omissions


def unique_replace(value, old, new, label):
    if value.count(old) != 1:
        raise ValueError(f'Expected one {label} marker; baseline/source drift requires review')
    return value.replace(old, new)


def stage_index(data):
    value = data.decode('utf-8')
    if re.search(r'interactive-previews|review[-_ ]?hub', value, re.I):
        raise ValueError('Private review-hub markup must not enter the public stage')
    if 'collection-return' in value or 'downloads-return.js' in value:
        raise ValueError('Use the reviewed original baseline, not a previously staged site')
    value = unique_replace(value, '  show(location.hash.slice(1));',
                           '  show(location.pathname === "/downloads" ? "downloads" : location.hash.slice(1));',
                           'initial hash-router selection')
    start = value.find('<main class="page" id="page-downloads">')
    if start < 0 or value.count('id="page-downloads"') != 1:
        raise ValueError('Expected the existing unique Downloads panel')
    end = value.find('</main>', start)
    if end < 0:
        raise ValueError('Downloads panel is not closed')
    panel = value[start:end]
    if panel.count('<section class="lede">') != 1 or '</section>' not in panel:
        raise ValueError('Expected the Downloads lede insertion seam')
    offset = start + panel.index('</section>') + len('</section>')
    value = value[:offset] + RETURN_PANEL + value[offset:]
    style_markers = re.findall(r'<style>\r?\n(?=  @font-face)', value)
    if len(style_markers) != 1:
        raise ValueError('Expected one main site stylesheet before its font declarations')
    value = unique_replace(value, style_markers[0], '<style>' + RETURN_STYLE,
                           'main site stylesheet')
    value = unique_replace(value, '</head>',
                           '<script defer src="/collections/assets/downloads-return.js"></script></head>',
                           'site head')
    scripts = re.findall(r'<script([^>]*)>(.*?)</script>', value, re.S | re.I)
    inline = [body for attrs, body in scripts if not re.search(r'\bsrc\s*=', attrs, re.I)]
    if len(inline) != 2 or len(scripts) != 3:
        raise ValueError('Expected exactly two existing inline scripts and our first-party return script')
    # HTML parsing normalizes CRLF/CR to LF before CSP checks inline scripts.
    hashes = ["'sha256-" + base64.b64encode(hashlib.sha256(body.replace('\r\n', '\n').replace('\r', '\n').encode('utf-8')).digest()).decode() + "'"
              for body in inline]
    return value.encode('utf-8'), hashes


def preview_html(data, origin):
    value = data.decode('utf-8')
    if value.count('<body data-api-origin="" data-site-brand="">') != 1:
        raise ValueError('Expected the canonical viewer body marker contract')
    value = unique_replace(value, 'data-api-origin=""',
                           f'data-api-origin="{html.escape(origin, quote=True)}"', 'viewer API origin')
    value = unique_replace(value, 'data-site-brand=""',
                           'data-site-brand="getcolosseum"', 'viewer site brand')
    return value.encode('utf-8')


def header_block(path, csp, extra=''):
    return (f'{path}\n  Cache-Control: no-store, no-transform\n'
            '  Referrer-Policy: no-referrer\n  X-Content-Type-Options: nosniff\n'
            '  X-Frame-Options: DENY\n'
            '  Permissions-Policy: camera=(), microphone=(), geolocation=()\n'
            f'  Content-Security-Policy: {csp}\n{extra}')


def merged_headers(data, origin, script_hashes, has_assetlinks):
    baseline = data.decode('utf-8')
    # Never silently combine an earlier collection-specific CSP with ours.
    for line in baseline.splitlines():
        if line and not line[0].isspace() and line.strip() in {
                '/c/*', '/collections/*', '/collections/viewer', '/collections/viewer.html', '/downloads'}:
            raise ValueError('Existing collection/Downloads header owner requires explicit reconciliation')
    preview = ("default-src 'none'; script-src 'self'; style-src 'self'; "
               "font-src 'self'; img-src 'self' data:; connect-src " + origin +
               "; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'none'")
    downloads = ("default-src 'none'; script-src 'self' " + ' '.join(script_hashes) +
                 "; style-src 'self' 'unsafe-inline'; font-src 'self'; img-src 'self' data:; "
                 "connect-src 'self'; frame-src 'self' about:; object-src 'none'; "
                 "base-uri 'none'; frame-ancestors 'none'; form-action 'self'")
    result = baseline.rstrip() + '\n\n# Collections sharing (generated from the canonical viewer)\n'
    for path in ('/c/*', '/collections/viewer.html', '/collections/viewer'):
        result += header_block(path, preview) + '\n'
    result += header_block('/downloads', downloads) + '\n'
    result += ('/collections/assets/*\n  Cache-Control: no-store, no-transform\n'
               '  Referrer-Policy: no-referrer\n  X-Content-Type-Options: nosniff\n')
    if has_assetlinks:
        result += ('\n/.well-known/assetlinks.json\n  Content-Type: application/json\n'
                   '  Cache-Control: no-cache\n  X-Content-Type-Options: nosniff\n')
    return result.encode('utf-8')


def merged_redirects(data):
    baseline = data.decode('utf-8')
    for line in baseline.splitlines():
        if line.strip() and not line.lstrip().startswith('#'):
            source = line.split()[0]
            if source in ('/c/*', '/downloads', '/collections/viewer', '/collections/viewer.html'):
                raise ValueError('Existing collection/Downloads redirect owner requires explicit reconciliation')
    return ('# Collections sharing: keep these before any broader fallbacks.\n'
            '/downloads /index.html 200\n/c/* /collections/viewer.html 200\n' + baseline).encode('utf-8')


def public_associations(path):
    if path is None:
        return None
    path = checked_path(path, must_exist=True)
    if not path.is_file() or path.stat().st_size > 65536:
        raise ValueError('Public assetlinks input must be a bounded regular JSON file')
    data = path.read_bytes()

    def unique_keys(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('Duplicate assetlinks JSON key')
            result[key] = value
        return result

    value = json.loads(data.decode('utf-8'), object_pairs_hook=unique_keys)
    if not isinstance(value, list) or not 1 <= len(value) <= 16:
        raise ValueError('Expected a nonempty bounded Android association array')
    for item in value:
        if not isinstance(item, dict) or set(item) != {'relation', 'target'}:
            raise ValueError('Unexpected Android association fields')
        target = item['target']
        if (item['relation'] != ['delegate_permission/common.handle_all_urls']
                or not isinstance(target, dict)
                or set(target) != {'namespace', 'package_name', 'sha256_cert_fingerprints'}
                or target['namespace'] != 'android_app'
                or not isinstance(target['package_name'], str)
                or not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+', target['package_name'])
                or target['package_name'] == 'org.qtproject.example'):
            raise ValueError('Expected public Android application metadata, not a manifest template')
        fingerprints = target['sha256_cert_fingerprints']
        if (not isinstance(fingerprints, list) or not 1 <= len(fingerprints) <= 16
                or not all(isinstance(fp, str) and re.fullmatch(r'(?:[0-9A-Fa-f]{2}:){31}[0-9A-Fa-f]{2}', fp)
                           for fp in fingerprints)):
            raise ValueError('Expected SHA-256 signing-certificate fingerprints')
    return data


def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args], text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--site-base', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--api-origin', required=True)
    parser.add_argument('--assetlinks', help='Independently qualified public release association JSON; never signing keys')
    args = parser.parse_args()
    try:
        repo = checked_path(Path(__file__).parents[2], must_exist=True)
        base = checked_path(args.site_base, must_exist=True)
        output = checked_path(args.output)
        if not base.is_dir() or output.exists() or overlaps(base, output) or overlaps(repo, output):
            raise ValueError('Output must be new and disjoint from both baseline and product source')
        origin = api_origin(args.api_origin)
        if git(repo, 'status', '--porcelain', '--untracked-files=all'):
            raise ValueError('Commit the product source before generating a reproducible stage')
        revision = git(repo, 'rev-parse', 'HEAD')
        files, omissions = inventory(base)
        required = ('index.html', '_headers', '_redirects', *BRAND_FILES)
        if any(name not in files for name in required):
            raise ValueError('Baseline lacks its reviewed site entry, routing/header files or brand assets')
        associations = public_associations(args.assetlinks)
        canonical = 'server/account-service/internal/httpserver/collectionviewer/'
        source_names = [canonical + name for name in ('index.html', 'viewer.css', 'viewer.js')]
        source_names += ['web/collections-sharing/' + name for name in ('stage_site.py', 'downloads-return.js', 'README.md')]
        sources = {}
        for name in source_names:
            path = checked_path(repo / name, must_exist=True)
            sources[name] = path.read_bytes()
        # Transform/validate all small integration inputs before creating output.
        integration_inputs = {name: files[name].read_bytes() for name in ('index.html', '_headers', '_redirects')}
        index, script_hashes = stage_index(integration_inputs['index.html'])
        changes = {
            'index.html': index,
            '_headers': merged_headers(integration_inputs['_headers'], origin, script_hashes, associations is not None),
            '_redirects': merged_redirects(integration_inputs['_redirects']),
            'collections/viewer.html': preview_html(sources[canonical + 'index.html'], origin),
            'collections/assets/viewer.css': sources[canonical + 'viewer.css'],
            'collections/assets/viewer.js': sources[canonical + 'viewer.js'],
            'collections/assets/downloads-return.js': sources['web/collections-sharing/downloads-return.js'],
        }
        if associations is not None:
            changes['.well-known/assetlinks.json'] = associations
        for name in changes:
            if name in files and name not in ('index.html', '_headers', '_redirects'):
                raise ValueError('Baseline already owns a generated integration path; reconcile it explicitly')
        output.mkdir(parents=True, exist_ok=False)
        baseline_hashes, staged_hashes = {}, {}
        for name, source in sorted(files.items()):
            no_links(source)
            data = source.read_bytes()
            if name in integration_inputs and data != integration_inputs[name]:
                raise ValueError('Baseline integration input changed during generation')
            baseline_hashes[name] = digest(data)
            staged = changes.get(name, data)
            write_new(output, name, staged)
            staged_hashes[name] = digest(staged)
        for name, data in sorted(changes.items()):
            if name not in files:
                write_new(output, name, data)
                staged_hashes[name] = digest(data)
        if (git(repo, 'rev-parse', 'HEAD') != revision
                or git(repo, 'status', '--porcelain', '--untracked-files=all')
                or any((repo / name).read_bytes() != data for name, data in sources.items())):
            raise ValueError('Product source changed during generation; no completion manifest will be emitted')
        site_diff = ''.join(''.join(difflib.unified_diff(
            integration_inputs.get(name, b'').decode('utf-8').splitlines(keepends=True),
            data.decode('utf-8').splitlines(keepends=True),
            fromfile='a/' + name, tofile='b/' + name))
            for name, data in sorted(changes.items())).encode('utf-8')
        write_new(output, STAGE_DIFF, site_diff)
        staged_hashes[STAGE_DIFF] = digest(site_diff)
        manifest = {
            'schema_version': 1, 'product_source_revision': revision,
            'product_source_clean': True, 'product_source_diff': [],
            'product_source_sha256': {name: digest(data) for name, data in sorted(sources.items())},
            'site_baseline': {'git_revision': None, 'files_sha256': baseline_hashes},
            'excluded_paths': omissions, 'staged_files_sha256': dict(sorted(staged_hashes.items())),
            'changed_paths': sorted(changes), 'api_origin': origin, 'public_origin': PUBLIC_ORIGIN,
            'viewer_markers': {'data-api-origin': origin, 'data-site-brand': 'getcolosseum'},
            'site_diff': STAGE_DIFF,
            'android_association': {'included': associations is not None,
                                    'qualification': 'caller-supplied public release metadata; syntax validation only' if associations is not None else 'BLOCKED: release package/certificate not supplied'},
            'runtime_qualification': 'NOT RUN: routes, headers, browser, service and installed activation remain unverified',
            'deployment': 'NOT DEPLOYED; existing Workers/routes are outside this artifact',
        }
        # Last write: its presence marks completed generation, not acceptance.
        write_new(output, MANIFEST, (json.dumps(manifest, indent=2, sort_keys=True) + '\n').encode('utf-8'))
        print(json.dumps({'output': str(output), 'manifest': str(output / MANIFEST), 'product_source_revision': revision}))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Staging stopped: {error}. Any partial output is retained; choose a NEW output for a later attempt.\n')


if __name__ == '__main__':
    main()
