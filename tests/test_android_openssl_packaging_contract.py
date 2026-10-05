from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
deps = (ROOT / "scripts/android/build_deps.sh").read_text(encoding="utf-8")
apk = (ROOT / "scripts/android/build_apk.sh").read_text(encoding="utf-8")
pair = (ROOT / "scripts/android/verify_openssl_runtime_pair.sh").read_text(encoding="utf-8")
apk_pair = (ROOT / "scripts/android/verify_apk_openssl_pair.sh").read_text(encoding="utf-8")

def need(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)

need('./Configure "$target"' in deps, "OpenSSL must use the stock Android Configure target")
need('SHLIB_VERSION_NUMBER= build_libs' in deps, "OpenSSL runtime must be built unversioned for Android")
need('cp libcrypto.so "$prefix/lib/libcrypto_3.so"' in deps, "crypto runtime must be renamed after linking")
need('cp libssl.so "$prefix/lib/libssl_3.so"' in deps, "ssl runtime must be renamed after linking")
need('patchelf --page-size 16384 --set-soname libcrypto_3.so' in deps, "crypto patch must preserve Android 16 KiB ELF alignment")
need('patchelf --page-size 16384 --set-soname libssl_3.so' in deps, "ssl patch must preserve Android 16 KiB ELF alignment")
need('--replace-needed libcrypto.so libcrypto_3.so' in deps, "ssl DT_NEEDED edge must be patched")
need('verify_openssl_runtime_pair.sh' in deps, "dependency build must validate the runtime pair")
need('Configurations/99-colosseum.conf' not in deps, "custom OpenSSL shared-extension target must stay removed")
need('shared_extension => "_3.so"' not in deps, "do not rename OpenSSL during its own link")

need('verify_apk_openssl_pair.sh" "$apk" "$ABI"' in apk, "every built APK must validate its packaged OpenSSL pair")
need('libssl_3.so does not DT_NEEDED libcrypto_3.so' in pair, "pair gate must pin the libssl -> libcrypto dependency")
need('unresolved internal symbol absent from libcrypto_3.so' in pair, "pair gate must reject missing internal symbols")
need('lib/{abi}/libcrypto_3.so' in apk_pair and 'lib/{abi}/libssl_3.so' in apk_pair, "APK gate must extract both ABI-local runtimes")

print("android OpenSSL packaging contract: PASS")
