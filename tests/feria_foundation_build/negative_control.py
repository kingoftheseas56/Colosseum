"""Run the real encoding regression red, then restore exact source bytes and green."""
import pathlib
import subprocess
import sys

root = pathlib.Path(sys.argv[1]).resolve()
build = pathlib.Path(sys.argv[2]).resolve()
source = root / 'native/feria/FeriaBrowserPolicy.cpp'
original = source.read_bytes()
needle = b'url.setQuery(QStringLiteral("q=") + QString::fromLatin1(QUrl::toPercentEncoding(input)));'
assert original.count(needle) == 1
name = 'feria_foundation_policy_test' + ('.exe' if sys.platform == 'win32' else '')
config = sys.argv[3] if len(sys.argv) > 3 else 'Release'
logs = build / 'negative-control-logs'
logs.mkdir(exist_ok=True)

def compile_test():
    subprocess.run(['cmake', '--build', str(build), '--config', config,
                    '--target', 'feria_foundation_policy_test', '--parallel', '2'], check=True)

def run_test(label):
    candidates = [build / config / name, build / name]
    exe = next((p for p in candidates if p.is_file()), None)
    assert exe is not None, f'Missing test executable: {candidates}'
    log = logs / (label + '.txt')
    log.unlink(missing_ok=True)
    result = subprocess.run([str(exe), 'searchEscaping', '-o', str(log) + ',txt'])
    assert log.is_file(), f'Qt did not write {log}; exit={result.returncode}'
    text = log.read_text(encoding='utf-8', errors='replace')
    print(text, flush=True)
    print(f'{label} exit={result.returncode}', flush=True)
    return result.returncode, text

try:
    source.write_bytes(original.replace(
        needle, b'QUrlQuery query; query.addQueryItem("q", input); url.setQuery(query);'))
    compile_test()
    code, text = run_test('expected-red')
    assert code != 0 and 'FAIL!' in text and 'searchEscaping' in text and '%2B' in text, text
    print('EXPECTED RED: exact production query assertion detected', flush=True)
finally:
    source.write_bytes(original)
    assert source.read_bytes() == original
    compile_test()
    code, text = run_test('restored-green')
    assert code == 0 and 'PASS' in text and 'searchEscaping' in text and 'FAIL!' not in text, text
    assert source.read_bytes() == original
    print('RESTORED GREEN: source bytes preserved', flush=True)
