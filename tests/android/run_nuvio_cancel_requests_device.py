#!/usr/bin/env python3
"""Compile/run the isolated Nuvio cancellation regression; no CMake or network setup.
Requires an already-running delayed loopback fixture and root-owned adb reverse.
All outputs and device paths are fresh; no cleanup or donor/source mutation.
"""
import argparse, datetime, hashlib, json, pathlib, re, shlex, shutil, subprocess, sys, uuid

FIXTURE_SHA = '0a739d0665cd732d8391ab827cd05ba42af373599c0ba7234ac86a85d7e9a9e0'

def sha(path):
    with pathlib.Path(path).open('rb') as stream:
        digest = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1024 * 1024), b''): digest.update(chunk)
        return digest.hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('donor-build', 'ndk', 'adb', 'serial', 'fixture', 'out-dir'):
        p.add_argument('--' + key, required=True)
    p.add_argument('--source', type=pathlib.Path,
                   default=pathlib.Path(__file__).with_name('nuvio_cancel_requests_regression.cpp'))
    p.add_argument('--peer-port', type=int, default=51414)
    p.add_argument('--expect-retired-crash', action='store_true')
    a = p.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_.:-]{1,128}', a.serial): p.error('unsafe or empty explicit serial')
    if not 1024 <= a.peer_port <= 65535: p.error('peer-port must be 1024..65535 (127.0.0.1 only)')
    out = pathlib.Path(a.out_dir).resolve()
    out.mkdir(parents=True, exist_ok=False)  # Refuse stale evidence/output reuse.
    report = {'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'arguments': {k: str(v) if isinstance(v, pathlib.Path) else v for k, v in vars(a).items()},
              'commands': [], 'passed': False, 'peer_host': '127.0.0.1'}
    def save():
        (out / 'provenance.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    def run(label, argv, timeout=30, expected=0):
        argv = [str(x) for x in argv]
        item = {'label': label, 'argv': argv, 'timeout_seconds': timeout}
        report['commands'].append(item); save()
        try:
            r = subprocess.run(argv, capture_output=True, timeout=timeout, shell=False)
            stdout, stderr, code = r.stdout, r.stderr, r.returncode
        except subprocess.TimeoutExpired as error:
            stdout, stderr, code = error.stdout or b'', error.stderr or b'', None
            item['timed_out'] = True
        (out / (label + '.stdout.txt')).write_bytes(stdout)
        (out / (label + '.stderr.txt')).write_bytes(stderr)
        item.update(exit_code=code, stdout_sha256=hashlib.sha256(stdout).hexdigest(),
                    stderr_sha256=hashlib.sha256(stderr).hexdigest()); save()
        if expected is not None and code != expected:
            raise RuntimeError('%s exit %s, expected %s; see saved output' % (label, code, expected))
        return stdout.decode('utf-8', errors='replace')
    try:
        donor, ndk, source, fixture = map(lambda x: pathlib.Path(x).resolve(),
                                          (a.donor_build, a.ndk, a.source, a.fixture))
        ninja, cache = donor / 'build.ninja', donor / 'CMakeCache.txt'
        archive = donor / '_deps/nuvio_libtorrent-build/libtorrent-rasterbar.a'
        includes = [donor / '_deps/nuvio_libtorrent-src/include', donor / '_deps/nuvio_boost-src']
        for path in (ninja, cache, archive, source, fixture):
            if not path.is_file(): raise ValueError('missing input: ' + str(path))
        if fixture.stat().st_size != 2262 or sha(fixture) != FIXTURE_SHA:
            raise ValueError('fixture must be the pinned original 2262-byte torrent')
        if any(not x.is_dir() for x in includes): raise ValueError('missing donor headers')
        cache_text = cache.read_text(encoding='utf-8')
        if not re.search(r'^ANDROID_ABI:[^=]+=arm64-v8a$', cache_text, re.M):
            raise ValueError('runner requires an explicit arm64-v8a donor')
        if not re.search(r'^ANDROID_PLATFORM:[^=]+=android-28$', cache_text, re.M):
            raise ValueError('runner requires donor Android API 28')
        ninja_text = ninja.read_text(encoding='utf-8')
        blocks = [m.group(0) for m in re.finditer(r'^build [^\n]*\n(?:[ \t]+[^\n]*\n)*', ninja_text, re.M)
                  if re.search(r'[/\\]peer_connection\.cpp\.o:', m.group(0).splitlines()[0])]
        if len(blocks) != 1: raise ValueError('need exactly one peer_connection.cpp.o rule')
        block = blocks[0]
        def field(name):
            m = re.search(r'^  ' + name + r' = (.*)$', block, re.M)
            if not m: raise ValueError('missing rule field ' + name)
            return m.group(1)
        raw_defines, raw_flags = field('DEFINES'), field('FLAGS')
        defines = list(dict.fromkeys(x for x in shlex.split(raw_defines + ' ' + raw_flags)
                                     if x.startswith('-D') and x.split('=')[0] != '-DTORRENT_BUILDING_LIBRARY'))
        if '-DNDEBUG' not in defines or '-DTORRENT_DISABLE_LOGGING' not in defines:
            raise ValueError('donor must preserve NDEBUG and TORRENT_DISABLE_LOGGING')
        host = {'win32': 'windows-x86_64', 'linux': 'linux-x86_64', 'darwin': 'darwin-x86_64'}.get(sys.platform)
        if host is None: raise ValueError('unsupported NDK host')
        toolchain = ndk / 'toolchains/llvm/prebuilt' / host
        suffix = '.exe' if sys.platform == 'win32' else ''
        clang, strip = [toolchain / 'bin' / (x + suffix) for x in ('clang++', 'llvm-strip')]
        for path in (clang, strip):
            if not path.is_file(): raise ValueError('missing NDK tool ' + str(path))
        adb_path = shutil.which(a.adb)
        if not adb_path: raise ValueError('adb executable not found')
        adb = [adb_path, '-s', a.serial]
        pinned = {str(x): sha(x) for x in (source, fixture, archive, ninja, cache, clang, strip)}
        report.update(input_sha256=pinned, peer_rule=block, client_defines=defines,
                      excluded_define='TORRENT_BUILDING_LIBRARY', target='aarch64-none-linux-android28')
        exe = out / 'nuvio_cancel_requests_regression'
        command = [clang, '--target=aarch64-none-linux-android28', '--sysroot=' + str(toolchain / 'sysroot'),
                   '-O2', '-std=c++17', '-ffunction-sections', '-fdata-sections', *defines]
        for include in includes: command += ['-I', include]
        command += [source, archive, '-pthread', '-ldl', '-latomic', '-lm', '-static-libstdc++',
                    '-Wl,--gc-sections', '-o', exe]
        run('compile', command, timeout=300)
        run('strip', [strip, '--strip-debug', exe])
        if any(sha(path) != digest for path, digest in pinned.items()):
            raise RuntimeError('input changed during compilation; refusing device run')
        report['executable_sha256'] = sha(exe)
        identity = None
        def verify(label):
            nonlocal identity
            serial = run(label + '-serial', adb + ['get-serialno']).strip()
            props = run(label + '-getprop', adb + ['shell', 'getprop']).replace('\r', '')
            values = dict(re.findall(r'^\[([^\]]+)\]: \[(.*)\]$', props, re.M))
            current = {k: values.get(k, '') for k in ('ro.product.model', 'ro.build.fingerprint',
                       'ro.product.cpu.abilist', 'ro.serialno')}
            if serial != a.serial or 'arm64-v8a' not in current['ro.product.cpu.abilist'].split(','):
                raise RuntimeError('explicit serial/arm64 device verification failed')
            if not current['ro.serialno'] or not current['ro.build.fingerprint']:
                raise RuntimeError('missing hardware serial or build fingerprint')
            if identity is not None and current != identity: raise RuntimeError('device identity changed')
            identity = current; report['device_identity'] = current; save()
        verify('before-push')
        remote = '/data/local/tmp/nuvio-cancel-' + uuid.uuid4().hex
        report['owned_remote_directory'] = remote; save()
        run('mkdir-root', adb + ['shell', 'mkdir', remote])
        run('push-executable', adb + ['push', exe, remote + '/probe'])
        run('push-fixture', adb + ['push', fixture, remote + '/fixture.torrent'])
        run('chmod-executable', adb + ['shell', 'chmod', '700', remote + '/probe'])
        for name, expected in (('probe', report['executable_sha256']), ('fixture.torrent', FIXTURE_SHA)):
            actual = run('remote-hash-' + name.replace('.', '-'), adb + ['shell', 'sha256sum', remote + '/' + name])
            if actual.split()[0].lower() != expected: raise RuntimeError('remote SHA mismatch: ' + name)
        for mode in ('active', 'retired'):
            verify('before-' + mode)
            run('mkdir-' + mode, adb + ['shell', 'mkdir', remote + '/' + mode])
            expected = 139 if mode == 'retired' and a.expect_retired_crash else 0
            text = run(mode, adb + ['shell', 'timeout', '90', remote + '/probe', mode,
                       remote + '/fixture.torrent', remote + '/' + mode, str(a.peer_port)], 110, expected)
            rows = [json.loads(line) for line in text.splitlines() if line.startswith('{')]
            before = next((x for x in rows if x.get('phase') == 'before-cancel'), {})
            if before.get('retired') != (mode == 'retired') or not before.get('sent', 0) or not before.get('unsent', 0):
                raise RuntimeError(mode + ': no proven sent and unsent requests')
            if expected == 0 and not any(x.get('phase') == 'after-cancel' and x.get('passed') is True
                                        and x.get('retired') == (mode == 'retired') for x in rows):
                raise RuntimeError(mode + ': missing successful cancellation evidence')
        report['passed'] = True
    except Exception as error:
        report['error'] = repr(error)
    finally:
        report['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat(); save()
    print(json.dumps({'passed': report['passed'], 'provenance': str(out / 'provenance.json'),
                      'error': report.get('error')}))
    return 0 if report['passed'] else 1

if __name__ == '__main__':
    raise SystemExit(main())

