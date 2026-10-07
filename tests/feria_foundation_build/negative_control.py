"""Temporarily restore the raw-plus query bug; require red then restored green."""
import pathlib
import subprocess
import sys

root = pathlib.Path(sys.argv[1]).resolve()
build = pathlib.Path(sys.argv[2]).resolve()
source = root / 'native/feria/FeriaBrowserPolicy.cpp'
original = source.read_bytes()
needle = b'url.setQuery(QStringLiteral("q=") + QString::fromLatin1(QUrl::toPercentEncoding(input)));'
assert original.count(needle) == 1
exe = build / ('feria_foundation_policy_test.exe' if sys.platform == 'win32' else 'feria_foundation_policy_test')
def compile_test():
    subprocess.run(['cmake', '--build', str(build), '--target', 'feria_foundation_policy_test', '--parallel', '2'], check=True)
try:
    source.write_bytes(original.replace(needle, b'QUrlQuery query; query.addQueryItem("q", input); url.setQuery(query);'))
    compile_test()
    red = subprocess.run([str(exe), 'searchEscaping'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(red.stdout)
    assert red.returncode != 0 and 'FAIL!' in red.stdout and 'searchEscaping' in red.stdout
    print('EXPECTED RED: production query encoding regression detected')
finally:
    source.write_bytes(original)
    compile_test()
subprocess.run([str(exe), 'searchEscaping'], check=True)
assert source.read_bytes() == original
print('RESTORED GREEN: source bytes preserved')
