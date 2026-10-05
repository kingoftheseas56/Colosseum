from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
deps = (ROOT / "scripts/android/build_deps.sh").read_text(encoding="utf-8")
apk = (ROOT / "scripts/android/build_apk.sh").read_text(encoding="utf-8")
pair = (ROOT / "scripts/android/verify_openssl_runtime_pair.sh").read_text(encoding="utf-8")
apk_pair = (ROOT / "scripts/android/verify_apk_openssl_pair.sh").read_text(encoding="utf-8")
qt_patch = (ROOT / "scripts/android/openssl_3_qt_android.patch").read_text(encoding="utf-8")

def need(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)

need('patch -p0 < "$REPO/scripts/android/openssl_3_qt_android.patch"' in deps,
     "OpenSSL source must be patched before Configure")
need('./Configure "$target"' in deps, "OpenSSL must use the stock Android Configure target")
need('-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384' in deps,
     "OpenSSL must be linked for Android 16 KiB page compatibility")
need('SHLIB_VERSION_NUMBER= build_libs' in deps, "OpenSSL shared runtime must stay unversioned beyond _3")
need('cp libcrypto_3.so "$prefix/lib/libcrypto_3.so"' in deps, "crypto runtime must already have the Qt filename")
need('cp libssl_3.so "$prefix/lib/libssl_3.so"' in deps, "ssl runtime must already have the Qt filename")
need("patchelf" not in deps, "OpenSSL runtime ELF must never be rewritten after linking")
need('shlib_variant    => "_3"' in qt_patch, "Qt OpenSSL patch must set the _3 shared-library variant")
need('$prevversion_s = " OPENSSL_$thisversion";' in qt_patch,
     "Qt OpenSSL patch must keep standard OPENSSL_* symbol-version namespaces")
need('$currversion_s = "OPENSSL_$thisversion "' in qt_patch,
     "Qt OpenSSL patch must keep standard current symbol-version namespaces")
need('shared_extension => "_3.so"' not in deps + qt_patch,
     "Do not replace OpenSSL shared_extension with a filename; use shlib_variant")
need('verify_openssl_runtime_pair.sh' in deps, "dependency build must validate the runtime pair")
need('verify_apk_openssl_pair.sh" "$apk" "$ABI"' in apk, "every built APK must validate its packaged OpenSSL pair")
need('libssl_3.so does not DT_NEEDED libcrypto_3.so' in pair, "pair gate must pin the libssl -> libcrypto dependency")
need('unresolved internal symbol absent from libcrypto_3.so' in pair, "pair gate must reject missing internal symbols")
need('lib/{abi}/libcrypto_3.so' in apk_pair and 'lib/{abi}/libssl_3.so' in apk_pair,
     "APK gate must extract both ABI-local runtimes")

print("android OpenSSL packaging contract: PASS")
