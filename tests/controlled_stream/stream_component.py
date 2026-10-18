#!/usr/bin/env python3
"""Owned torrent transport component only. Execute only in approved live GitHub namespace.
No namespace creation/mutation; --self-test never starts a process or opens a socket.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import urllib.request
from check_isolation import validate_current_namespace, safe_diagnostic

HASH = '6f86507e80761923581b503b016f608e3043b60b'
PAYLOAD = '54ca8e68aec74460b13505c4a4e0356fba5d8174e876a02e9f30eda634d30229'
PINS = {'stremio-runtime': 'a2e203a57671d2914ac6311dfed01ebda3bc7f93c24fcae9d804ab611da1d10b',
        'server.js': '567a397bb11b788571bf1750fd05dd78927f97bec0c9ddeaa6d9cc1eccee3922',
        'ffmpeg': '3ea58083710f63bf920b16c7d5d24ae081e7d731f57a656fed11af0410d4eb48'}
OPTIONS = {'dht': False, 'tracker': False, 'peerSearch': {'min': 1, 'max': 1,
           'sources': ['tracker:http://127.0.0.1:49022/announce']}}
REQUIRED = ['fixture.json', 'create-options.json', 'owned-test-pattern.mp4',
            'owned-test-pattern.torrent', 'loopback_seed', 'loopback_tracker.py']

def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''): h.update(block)
    return h.hexdigest()

def demand(ok, message):
    if not ok: raise ValueError(message)

def diagnostic_excerpt(text):
    text = re.sub(r'(?<![A-Za-z0-9])/(?:[^\s:]+/?)+', '[path redacted]', text)
    return safe_diagnostic(text[-3000:])

def validate_proof(proof, actual):
    demand(proof.get('status') == 'PASS', 'Isolation proof not PASS')
    demand(proof.get('loopbackBind', {}).get('status') == 'PASS' and
           proof['loopbackBind'].get('closed') is True and
           proof['loopbackBind'].get('address') == '127.0.0.1', 'Missing closed loopback preflight')
    for key, value in actual.items():
        demand(proof.get(key) == value, 'Isolation proof mismatch: ' + key)
    demand(actual.get('interfaces') == ['lo'] and actual.get('externalRoutes') is False
           and actual.get('loopbackUp') is True, 'Unsafe namespace state')

def validate_range(status, headers, body, original):
    demand(status == 206, 'Range response is not HTTP 206')
    demand(headers.get('Content-Range') == 'bytes 0-4095/116123', 'Incorrect Content-Range')
    demand(headers.get('Content-Length') == '4096', 'Incorrect range Content-Length')
    demand(len(body) == 4096 and body == original[:4096], 'Range bytes differ from owned source')

def validate_files(value):
    demand(value.get('infoHash') == HASH, 'Registered info hash mismatch')
    files = value.get('files')
    demand(isinstance(files, list) and len(files) == 1, 'Expected exactly one registered file')
    f = files[0]
    demand(f.get('name') == 'owned-test-pattern.mp4' and f.get('length', f.get('size')) == 116123,
           'Registered file identity/size mismatch')
    return {'name': f['name'], 'bytes': 116123, 'index': 0}

def cleanup_children(children, kill_group=os.killpg):
    receipts = []
    # Only our process groups. Attempt every group even when one shutdown fails.
    for name, proc in reversed(children):
        requested = proc.poll() is None
        forced = False
        cleanup_error = None
        try:
            # A reaped leader may still have live descendants in its owned group.
            try: kill_group(proc.pid, signal.SIGTERM)
            except ProcessLookupError: pass
            try: proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                forced = True
                try: kill_group(proc.pid, signal.SIGKILL)
                except ProcessLookupError: pass
                proc.wait(timeout=3)
        except Exception as error:
            cleanup_error = diagnostic_excerpt(str(error))
            # A last bounded best-effort kill must not suppress later children.
            try: kill_group(proc.pid, signal.SIGKILL)
            except Exception: pass
        group_empty = False
        try:
            try:
                kill_group(proc.pid, 0)
                forced = True
                kill_group(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                group_empty = True
            if not group_empty:
                until = time.monotonic() + 1
                while time.monotonic() < until:
                    try: kill_group(proc.pid, 0)
                    except ProcessLookupError:
                        group_empty = True; break
                    time.sleep(.05)
        except Exception as error:
            cleanup_error = diagnostic_excerpt(str(error))
        receipts.append({'name': name, 'pid': proc.pid, 'exitCode': proc.poll(),
                                   'shutdownRequested': requested, 'forcedKill': forced,
                                   'cleanupError': cleanup_error, 'ownedProcessGroupEmpty': group_empty})
    return receipts

class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        raise ValueError('HTTP redirect forbidden')

def main_run(args):
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    result = {'status': 'BLOCKED', 'scope': 'owned torrent to official server.js HTTP to FFmpeg component',
              'fullApplicationTested': False, 'publicSwarmAccessIntended': False,
              'driverSha256': sha(__file__), 'qualificationSourceCommit': args.source_commit, 'children': [], 'checks': {}}
    children, handles = [], []
    started = time.monotonic()
    deadline = started + 170
    private = Path(tempfile.mkdtemp(prefix='b1-stream-private-', dir=output.parent))
    def remaining(cap=30):
        left = deadline - time.monotonic()
        if left <= 0: raise TimeoutError('170 second component deadline exceeded')
        return min(cap, left)
    def spawn(name, command, env, cwd):
        handle = (private / (name + '.log')).open('wb'); handles.append(handle)
        proc = subprocess.Popen(command, cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                                stdout=handle, stderr=subprocess.STDOUT, start_new_session=True)
        children.append((name, proc)); return proc
    def alive():
        for name, proc in children:
            demand(proc.poll() is None, name + ' exited before checks finished')
    def interrupted(signum, frame):
        raise InterruptedError('Termination signal ' + str(signum))
    previous = {s: signal.signal(s, interrupted) for s in (signal.SIGTERM, signal.SIGINT, signal.SIGALRM)}
    signal.alarm(170)
    try:
        demand(re.fullmatch(r'[0-9a-f]{40}', args.source_commit), 'Invalid qualification source commit')
        demand(os.environ.get('GITHUB_ACTIONS') == 'true', 'Execution restricted to authorized GitHub runner')
        fixture = Path(args.fixture_dir).resolve(); runtime = Path(args.runtime_dir).resolve()
        provenance = json.loads(Path(args.runtime_provenance).read_text())
        pinned = {}
        for name, expected in PINS.items():
            demand(provenance['runtime_files'][name]['sha256'] == expected, 'Unapproved runtime manifest')
            demand(sha(runtime / name) == expected, 'Runtime hash mismatch: ' + name)
            pinned[name] = expected
        manifest = {}
        for line in (fixture / 'SHA256SUMS').read_text().splitlines():
            digest, name = line.split(maxsplit=1); name = name.lstrip('*')
            demand(Path(name).name == name and len(digest) == 64, 'Invalid fixture manifest entry')
            manifest[name] = digest
        for name in REQUIRED: demand(sha(fixture / name) == manifest[name], 'Fixture hash mismatch: ' + name)
        facts = json.loads((fixture / 'fixture.json').read_text())
        demand(facts['info_hash'] == HASH and facts['payload_sha256'] == PAYLOAD and
               facts['payload_bytes'] == 116123 and facts['file_index'] == 0, 'Unexpected owned fixture')
        original = (fixture / 'owned-test-pattern.mp4').read_bytes()
        demand(hashlib.sha256(original).hexdigest() == PAYLOAD, 'Source payload hash mismatch')
        options = (fixture / 'create-options.json').read_bytes()
        demand(json.loads(options) == OPTIONS, 'Unsafe create options')
        values = (args.parent_net, args.parent_user, args.parent_uid, args.parent_gid, args.ip)
        actual = validate_current_namespace(*values)
        validate_proof(json.loads(Path(args.isolation_proof).read_text()), actual)
        result['isolation'] = actual; result['runtimeHashes'] = pinned
        result['fixtureHashes'] = {n: manifest[n] for n in REQUIRED}
        # No inherited credentials/proxy/node settings; keep only normal executable/library configuration.
        env = {k: os.environ[k] for k in ('PATH', 'LD_LIBRARY_PATH', 'LANG', 'LC_ALL') if k in os.environ}
        result['removedEnvironmentNames'] = sorted(k for k in os.environ if k not in env)
        for key, dirname in [('HOME', 'home'), ('APP_PATH', 'runtime-cache'),
                             ('XDG_CACHE_HOME', 'cache'), ('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data')]:
            path = private / dirname; path.mkdir(); env[key] = str(path)
        env['NO_HTTPS_SERVER'] = '1'
        result['cacheInitiallyEmpty'] = all(not list((private / d).iterdir()) for d in ('runtime-cache', 'cache', 'config', 'data'))
        # Revalidate immediately before any children. The probe receipt from an exited namespace cannot pass.
        validate_proof(json.loads(Path(args.isolation_proof).read_text()), validate_current_namespace(*values))
        spawn('tracker', [sys.executable, str(fixture / 'loopback_tracker.py'), HASH], env, fixture)
        spawn('seeder', [str(fixture / 'loopback_seed'), str(fixture / 'owned-test-pattern.torrent'), str(fixture)], env, fixture)
        seed_deadline = time.monotonic() + 23
        while 'SEEDER_READY verified owned payload' not in (private / 'seeder.log').read_text(errors='replace'):
            alive(); remaining()
            if time.monotonic() >= seed_deadline: raise TimeoutError('Verified seeder readiness deadline')
            time.sleep(.1)
        result['checks']['seederReady'] = {'status': 'PASS', 'pieceVerification': 'libtorrent is_seeding after normal add_torrent'}
        spawn('runtime', [str(runtime / 'stremio-runtime'), str(runtime / 'server.js')], env, runtime)
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
        def request(path, data=None, range_header=False, maximum=1024*1024):
            alive()
            headers = {'Content-Type': 'application/json'} if data is not None else {}
            if range_header: headers['Range'] = 'bytes=0-4095'
            req = urllib.request.Request('http://127.0.0.1:11470' + path, data=data, headers=headers)
            with opener.open(req, timeout=remaining()) as response:
                body = response.read(maximum + 1)
                demand(len(body) <= maximum, 'Response exceeds bounded length')
                return response.status, response.headers, body
        startup_deadline = time.monotonic() + 25
        while True:
            try:
                status, _, body = request('/settings')
                demand(status == 200 and isinstance(json.loads(body), dict), 'Invalid settings response'); break
            except (ConnectionError, urllib.error.URLError):
                alive()
                if time.monotonic() >= startup_deadline: raise TimeoutError('Runtime startup deadline')
                time.sleep(.2)
        demand('EngineFS server started at http://127.0.0.1:11470' in (private / 'runtime.log').read_text(errors='replace'), 'Expected runtime listener not confirmed')
        result['checks']['settings'] = {'status': 'PASS', 'httpStatus': status}
        t = time.monotonic()
        status, _, body = request('/' + HASH + '/create', data=options)
        demand(status == 200, 'Create did not return 200')
        result['checks']['registration'] = {'status': 'PASS', 'file': validate_files(json.loads(body)), 'elapsedMs': round((time.monotonic()-t)*1000)}
        t = time.monotonic()
        status, headers, body = request('/' + HASH + '/0', range_header=True, maximum=4096)
        validate_range(status, headers, body, original)
        result['checks']['range'] = {'status': 'PASS', 'httpStatus': status, 'bytes': len(body), 'elapsedMs': round((time.monotonic()-t)*1000)}
        t = time.monotonic()
        status, _, body = request('/' + HASH + '/0', maximum=116123)
        demand(status == 200 and len(body) == 116123 and hashlib.sha256(body).hexdigest() == PAYLOAD, 'Full streamed payload mismatch')
        result['checks']['fullPayload'] = {'status': 'PASS', 'httpStatus': status, 'bytes': len(body), 'sha256': PAYLOAD, 'elapsedMs': round((time.monotonic()-t)*1000)}
        streamed = private / 'streamed.mp4'; streamed.write_bytes(body)
        decode = spawn('decoder', [str(runtime / 'ffmpeg'), '-nostdin', '-v', 'error', '-i', str(streamed), '-f', 'null', '-'], env, private)
        code = decode.wait(timeout=remaining(30)); demand(code == 0, 'FFmpeg decode failed')
        result['checks']['decode'] = {'status': 'PASS', 'exitCode': code}
        announces = (private / 'tracker.log').read_text(errors='replace').count('ANNOUNCE accepted fixture hash')
        demand(announces > 0, 'No exact fixture tracker announce witnessed')
        result['checks']['tracker'] = {'status': 'PASS', 'acceptedFixtureAnnounces': announces}
        result['status'] = 'PASS'
    except Exception as error:
        result['status'] = 'FAIL' if children else 'BLOCKED'
        result['errorType'] = type(error).__name__; result['diagnostic'] = diagnostic_excerpt(str(error))
        result['childDiagnostics'] = {}
        for name, _ in children:
            log = private / (name + '.log')
            if log.exists(): result['childDiagnostics'][name] = diagnostic_excerpt(log.read_text(errors='replace'))
    finally:
        signal.alarm(0)
        for sig in previous: signal.signal(sig, signal.SIG_IGN)
        result['children'] = cleanup_children(children)
        for handle in handles: handle.close()
        for sig, handler in previous.items(): signal.signal(sig, handler)
        result['elapsedMs'] = round((time.monotonic()-started)*1000)
        result['ownedChildrenReaped'] = all(p.poll() is not None for _, p in children)
        result['ownedProcessGroupsEmpty'] = all(r['ownedProcessGroupEmpty'] for r in result['children'])
        if not result['ownedChildrenReaped'] or not result['ownedProcessGroupsEmpty']: result['status'] = 'FAIL'
        result['shutdownScope'] = 'owned component sidecars terminated; no application clean-exit claim'
        (output / 'component-result.json').write_text(json.dumps(result, indent=2) + '\n')
    return 0 if result['status'] == 'PASS' else 2

def self_test():
    count = 0
    def check(fn, fails=False):
        nonlocal count
        try: fn()
        except (ValueError, KeyError, TypeError):
            if not fails: raise
        else:
            if fails: raise AssertionError('Unsafe fixture accepted')
        count += 1
    actual = {'isolatedNetNamespace': 'net:[2]', 'interfaces': ['lo'], 'externalRoutes': False, 'loopbackUp': True}
    proof = dict(actual, status='PASS', loopbackBind={'status': 'PASS', 'closed': True, 'address': '127.0.0.1'})
    check(lambda: validate_proof(proof, actual))
    for change in ({'status':'FAIL'}, {'isolatedNetNamespace':'net:[1]'}, {'interfaces':['lo','eth0']}, {'loopbackBind':{}}, {'loopbackUp':False}):
        check(lambda c=change: validate_proof(dict(proof, **c), actual), True)
    data = bytes(116123); headers = {'Content-Range':'bytes 0-4095/116123','Content-Length':'4096'}
    check(lambda: validate_range(206, headers, data[:4096], data))
    check(lambda: validate_range(200, headers, data[:4096], data), True)
    check(lambda: validate_range(206, dict(headers, **{'Content-Range':'bytes 1-4096/116123'}), data[:4096], data), True)
    check(lambda: validate_range(206, headers, b'x'*4096, data), True)
    check(lambda: validate_range(206, headers, data[:4095], data), True)
    valid = {'infoHash': HASH, 'files':[{'name':'owned-test-pattern.mp4','length':116123}]}
    check(lambda: validate_files(valid))
    check(lambda: validate_files(dict(valid, infoHash='0'*40)), True)
    check(lambda: validate_files({'infoHash':HASH, 'files':[]}), True)
    check(lambda: validate_files({'infoHash':HASH, 'files':valid['files']*2}), True)
    check(lambda: validate_files({'infoHash':HASH, 'files':[{'name':'other.mp4','length':116123}]}), True)
    check(lambda: validate_files({'infoHash':HASH, 'files':[{'name':'owned-test-pattern.mp4','length':0}]}), True)
    check(lambda: demand('/private' not in diagnostic_excerpt('failure /private/profile/file'), 'Path not redacted'))
    check(lambda: demand('secret-value' not in diagnostic_excerpt('token=secret-value'), 'Secret not redacted'))
    class FakeChild:
        def __init__(self, pid, outcome): self.pid, self.outcome, self.code, self.waits = pid, outcome, None, 0
        def poll(self): return self.code
        def wait(self, timeout):
            if self.code is not None: return self.code
            self.waits += 1
            if self.outcome == 'timeout' and self.waits == 1: raise subprocess.TimeoutExpired('fake', timeout)
            if self.outcome == 'failure': raise RuntimeError('modeled cleanup failure')
            self.code = -15; return self.code
    calls = []
    timed = FakeChild(1, 'timeout'); exited = FakeChild(2, 'normal'); exited.code = 0
    vanished = FakeChild(3, 'normal'); failed = FakeChild(4, 'failure'); after = FakeChild(5, 'normal')
    surviving_groups = {2}
    def fake_kill(pid, sig):
        calls.append((pid, sig))
        if pid == 3: raise ProcessLookupError()
        if sig == signal.SIGKILL: surviving_groups.discard(pid)
        if sig == 0 and pid not in surviving_groups: raise ProcessLookupError()
    records = cleanup_children([('after', after), ('failed', failed), ('vanished', vanished), ('exited', exited), ('timed', timed)], fake_kill)
    by_name = {r['name']:r for r in records}
    check(lambda: demand(by_name['timed']['forcedKill'] and timed.waits == 2, 'Timeout cleanup failed'))
    check(lambda: demand(by_name['exited']['exitCode'] == 0 and not by_name['exited']['shutdownRequested'], 'Exited child mishandled'))
    check(lambda: demand(by_name['vanished']['cleanupError'] is None, 'Disappearance race mishandled'))
    check(lambda: demand(by_name['failed']['cleanupError'] and by_name['failed']['exitCode'] is None, 'Cleanup failure hidden'))
    check(lambda: demand(by_name['after']['exitCode'] == -15 and len(records) == 5, 'Remaining children skipped'))
    check(lambda: demand((2, signal.SIGTERM) in calls and (2, signal.SIGKILL) in calls and by_name['exited']['ownedProcessGroupEmpty'], 'Descendant group survived exited leader'))
    print(str(count) + ' offline component helper checks passed; no sockets or subprocesses')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    for flag in ('fixture-dir','runtime-dir','runtime-provenance','isolation-proof','parent-net','parent-user','ip','output','source-commit'):
        parser.add_argument('--'+flag)
    parser.add_argument('--parent-uid', type=int); parser.add_argument('--parent-gid', type=int)
    args = parser.parse_args()
    if args.self_test: self_test(); sys.exit(0)
    for key, value in vars(args).items():
        if key != 'self_test' and value is None: parser.error('Missing --'+key.replace('_','-'))
    sys.exit(main_run(args))
