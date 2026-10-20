"""Contract checks for the opt-in Android Nuvio playback backend slice.

Source-layout contracts in the style of the existing tests/test_android_*
contracts: substring/regex assertions over the CMake/C++/Java sources, plus
genuine behavioral checks of the hardened-artifact package gate with fixture
prefixes. They complement (never replace) the native behavior tests in
tests/auto/player/tst_nuvio_stream_backend.cpp.

Run with the standard library runner used for the other contracts, e.g.
    python tests/test_android_nuvio_backend_contract.py
or via the task runner with additional file arguments.
"""

import json
import pathlib
import re
import runpy
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
NATIVE = ROOT / "native"


def _read(path):
    return path.read_text(encoding="utf-8")


MAIN_CMAKE = _read(NATIVE / "CMakeLists.txt")
ANDROID_CMAKE = _read(NATIVE / "cmake" / "android.cmake")
NUVIO_BACKEND_CMAKE = _read(NATIVE / "cmake" / "NuvioBackend.cmake")
NUVIO_TESTS_CMAKE = _read(NATIVE / "cmake" / "NuvioBackendTests.cmake")
STREAMSERVER_ANDROID = _read(NATIVE / "player" / "streamserver_android.cpp")
STREAMSERVER_H = _read(NATIVE / "player" / "streamserver.h")
BACKEND_H = _read(NATIVE / "player" / "nuviostreambackend.h")
BACKEND_CPP = _read(NATIVE / "player" / "nuviostreambackend.cpp")
API_H = _read(NATIVE / "player" / "nuvioengineapi.h")
BACKEND_CPP_LOADER = _read(NATIVE / "player" / "nuvioengineapi.cpp")
MAIN_CPP = _read(NATIVE / "main.cpp")
PREPARE = _read(ROOT / "scripts" / "android" / "prepare_nuvio_engine.py")
TRUST_JAVA = _read(
    NATIVE / "platform" / "android" / "src" / "org" / "colosseum" / "player"
    / "NuvioAndroidTrust.java"
)
TESTS_CMAKE = _read(ROOT / "tests" / "CMakeLists.txt")


def require(text, needle, reason):
    if needle not in text:
        raise AssertionError(f"{reason}: missing {needle!r}")


def test_backend_option_defaults_off_and_is_android_only():
    match = re.search(
        r'option\(\s*COLOSSEUM_ANDROID_NUVIO_BACKEND\b.*?\b(OFF)\s*\)',
        NUVIO_BACKEND_CMAKE,
        re.S,
    )
    assert match is not None, "COLOSSEUM_ANDROID_NUVIO_BACKEND option must default to OFF"
    require(
        NUVIO_BACKEND_CMAKE,
        "COLOSSEUM_ANDROID_NUVIO_BACKEND is Android-only",
        "the feature must fail closed outside Android",
    )


def test_enabling_requires_hardened_artifact():
    require(
        NUVIO_BACKEND_CMAKE,
        "requires COLOSSEUM_NUVIO_ENGINE_PREFIX",
        "enabling without a prepared prefix must fail",
    )
    require(
        NUVIO_BACKEND_CMAKE,
        ".colosseum-nuvio-hardened-stamp",
        "the unpatched release AAR must never pass readiness",
    )
    require(
        NUVIO_BACKEND_CMAKE,
        "Never use the unpatched release AAR",
        "stamp gate must say what it rejects",
    )


def test_android_sources_only_join_the_target_behind_the_option():
    block = re.search(
        r'if\(COLOSSEUM_ANDROID_NUVIO_BACKEND\)(.*?)endif\(\)', ANDROID_CMAKE, re.S
    )
    assert block is not None, "android.cmake must gate the adapter sources"
    body = block.group(1)
    for source in ("nuvioengineapi.cpp", "nuviostreambackend.cpp"):
        require(body, source, f"{source} must be gated behind the option")
    require(
        body,
        "QT_ANDROID_EXTRA_LIBS",
        "the private engine .so is packaged, never linked in",
    )
    # The stub source stays registered for the OFF build (default path).
    require(
        ANDROID_CMAKE.split("if(COLOSSEUM_ANDROID_NUVIO_BACKEND)")[0],
        "player/streamserver_android.cpp",
        "the Android stub/delegation source stays unconditional",
    )


def test_stub_is_preserved_when_option_off():
    require(
        STREAMSERVER_ANDROID,
        "#else // stub: Android streaming unavailable",
        "the OFF build must keep the existing unavailable stub",
    )
    require(
        STREAMSERVER_ANDROID,
        "Colosseum Server is not available on Android yet",
        "the stub message must stay intact",
    )


def test_qml_seam_contract_is_preserved():
    for needle in (
        "void play(const QString &infoHash, int fileIdx)",
        "void prefetch(const QString &infoHash, int fileIdx)",
        "QString streamUrl(",
        "void watchStats(",
        "void unwatchStats()",
        "void streamReady(",
        "void fetchReady(",
        "void streamError(",
        "void warmUp()",
    ):
        require(STREAMSERVER_H, needle, "StreamServer contract must keep its seam")
    for needle in ("peers", "unchoked", "downloaded", "downloadSpeed", "streamProgress", "streamLen"):
        require(BACKEND_CPP, f'QStringLiteral("{needle}")', "stats mapping must keep existing keys")


def test_urls_are_validated_loopback_only():
    require(
        BACKEND_CPP,
        'parsed.host() != QStringLiteral("127.0.0.1")',
        "published URLs are validated against the canonical loopback host",
    )
    require(
        BACKEND_CPP,
        "port < 1 || port > 65535",
        "the loopback port must be validated inside 1..65535",
    )
    require(
        BACKEND_CPP,
        "invalid stream url",
        "a non-loopback or malformed URL must surface an error",
    )
    require(BACKEND_H, "generation", "request generations must exist for stale suppression")


def test_async_completions_are_correlated_by_request_id():
    require(
        BACKEND_CPP,
        "event.request_id == chainIt->second.addRequestId",
        "metadata completions must match the originating add request id",
    )
    require(
        BACKEND_CPP,
        "event.request_id == chainIt->second.prepareRequestId",
        "prepare completions must match the originating prepare request id",
    )
    require(
        BACKEND_CPP,
        "static_cast<int>(event.file_index) != chain.fileIdx",
        "a completion for a different selected file must never satisfy a request",
    )


def test_upload_disabled_and_budgets_explicit():
    require(
        BACKEND_CPP,
        "NUVIO_ENGINE_UPLOAD_DISABLED",
        "upload must be explicitly Disabled for this candidate",
    )
    require(
        BACKEND_CPP,
        "memory_cache_capacity_bytes",
        "cache budgets must be configured explicitly",
    )
    require(
        BACKEND_CPP,
        "tls_ca_bundle_path",
        "the TLS CA bundle must be handed to the engine",
    )


def test_warmup_and_startup_do_not_start_networking():
    require(
        BACKEND_CPP,
        "Deliberately lazy: startup warm-up must not start torrent networking",
        "warmUp() must stay inert",
    )
    require(
        STREAMSERVER_ANDROID,
        "never starts torrent networking at app startup",
        "Android warmUp() must stay inert",
    )


def test_background_lifecycle_uses_platform_runtime():
    require(
        MAIN_CPP,
        "stream->setForeground(platformRuntime->foreground())",
        "main.cpp must wire PlatformRuntime foreground to the backend",
    )
    require(
        BACKEND_H,
        "quiesce",
        "the backend must quiesce in the background",
    )


def test_tls_export_helper_exports_system_roots():
    require(TRUST_JAVA, "AndroidCAStore", "trust export must read Android system roots")
    require(TRUST_JAVA, "BEGIN CERTIFICATE", "trust export must emit PEM")
    require(
        TRUST_JAVA,
        "never disabled",
        "certificate verification must never be disabled",
    )


def test_prepare_script_is_pin_aware_and_fail_closed():
    for needle in (
        "v0.1.5",
        "finished_run_end",
        '"apply"',
        ".colosseum-nuvio-hardened-stamp",
        "refusing",
    ):
        require(PREPARE, needle, f"prepare script must be fail-closed about {needle}")


def test_pinned_patch_line_endings_are_portable_without_hunk_changes():
    """Windows CRLF checkout must not weaken the pinned patch application."""
    script = ROOT / "scripts" / "android" / "prepare_nuvio_engine.py"
    normalize = runpy.run_path(str(script))["portable_libtorrent_patch_bytes"]
    import contextlib
    import io
    patch = b"diff --git a/src/demo.cpp b/src/demo.cpp\n@@ -1 +1 @@\n-old\n+new\n"
    with tempfile.TemporaryDirectory(prefix="nuvio-patch-eol-") as directory:
        path = pathlib.Path(directory) / "example.patch"
        path.write_bytes(patch.replace(b"\n", b"\r\n"))
        assert normalize(path) == patch, "CRLF conversion changed patch content"
        path.write_bytes(patch.replace(b"\n", b"\r"))
        diagnostic = io.StringIO()
        with contextlib.redirect_stderr(diagnostic):
            try:
                normalize(path)
            except SystemExit as exc:
                assert exc.code == 1, "bare carriage returns must fail closed"
            else:
                raise AssertionError("bare carriage returns were accepted")
        assert "invalid CR" in diagnostic.getvalue()


def test_pinned_headers_and_notices_are_vendored():
    pinned = NATIVE / "third_party" / "nuvio-engine"
    for relative in (
        "include/nuvio_engine/nuvio_engine.h",
        "include/nuvio_engine/export.h",
        "LICENSE",
        "THIRD_PARTY_NOTICES.md",
        "SOURCE_MANIFEST.md",
    ):
        assert (pinned / relative).exists(), f"missing pinned artifact {relative}"
    header = _read(pinned / "include" / "nuvio_engine" / "nuvio_engine.h")
    require(header, "NUVIO_ENGINE_API_VERSION 3U", "pinned C API header must stay at version 3")


def test_native_test_compiles_production_sources():
    for needle in (
        "COLOSSEUM_NUVIO_BACKEND_SOURCES",
        "nuviostreambackend.cpp",
        "nuvioengineapi.cpp",
        "tst_nuvio_stream_backend",
    ):
        require(NUVIO_TESTS_CMAKE, needle, "the focused test must reuse production sources")
    require(
        TESTS_CMAKE,
        "colosseum_add_nuvio_backend_tests()",
        "tests/CMakeLists must register the nuvio backend tests",
    )
    require(
        MAIN_CMAKE,
        "COLOSSEUM_NUVIO_BACKEND_TESTS_ONLY",
        "the minimal focused-test project option must exist",
    )


def test_stub_api_table_loads_from_private_library_only():
    require(
        API_H,
        "loadEngineApi",
        "the C API must be resolved from the private engine library",
    )
    require(
        BACKEND_CPP_LOADER,
        "api_version() != NUVIO_ENGINE_API_VERSION",
        "the loader must check the pinned ABI version",
    )


# --- Behavioral package-gate checks (fixture prefixes, real verifier) -------
#
# The verifier subprocess is the same script the CMake gate runs at configure
# time; every rejection case below must exit nonzero, and a self-consistent
# fixture must pass. Fixtures are synthetic: a minimal 64-bit arm64 ELF shared
# object header plus a stamp whose recorded identities match the fixtures.

VERIFIER = ROOT / "scripts" / "android" / "verify_nuvio_hardened_artifact.py"
VENDORED_PATCH = (
    NATIVE / "third_party" / "nuvio-engine" / "patches" / "0001-android-hardening.patch"
)
PINNED_SOURCE_COMMIT = "99a18601fa12858cc8c62576dc72b87edbd81b9a"

ELF_HEADER_ARM64_SO = (
    b"\x7fELF"          # magic
    + bytes([2, 1, 1, 0])  # 64-bit, little endian, sysv
    + b"\x00" * 8       # padding
    + struct.pack("<H", 3)     # e_type = ET_DYN (shared object)
    + struct.pack("<H", 0xB7)  # e_machine = EM_AARCH64
    + b"\x00" * 34
)


def _run_verifier(prefix: pathlib.Path, abi: str = "arm64-v8a") -> subprocess.CompletedProcess:
    return subprocess.run(
        [
            sys.executable,
            str(VERIFIER),
            "--prefix",
            str(prefix),
            "--abi",
            abi,
        ],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def _make_fixture_prefix(
    root: pathlib.Path,
    *,
    library_bytes: bytes = ELF_HEADER_ARM64_SO,
    library_name: str = "libnuvio_engine.so",
    abi: str = "arm64-v8a",
    stamp_overrides: dict = None,
    stamp_without: object = (),
) -> pathlib.Path:
    prefix = root / "prefix"
    lib_dir = prefix / "android" / abi / "lib"
    lib_dir.mkdir(parents=True, exist_ok=True)
    library = lib_dir / library_name
    library.write_bytes(library_bytes)
    import hashlib

    stamp = {
        "kind": "colosseum-nuvio-hardened",
        "source_commit": PINNED_SOURCE_COMMIT,
        "source_branch": "codex/nuvio-android-hardening-20261018",
        "source_tag": "v0.1.5",
        "abi": abi,
        "android_api": 28,
        "api_version": 3,
        "libtorrent_patch_count": 12,
        "library": str(library),
        "library_sha256": hashlib.sha256(library_bytes).hexdigest(),
        "patch": str(VENDORED_PATCH),
        "patch_sha256": hashlib.sha256(VENDORED_PATCH.read_bytes()).hexdigest(),
    }
    if stamp_overrides:
        stamp.update(stamp_overrides)
    for key in stamp_overrides or {}:
        if stamp_overrides[key] is None:
            del stamp[key]
    if stamp_without:
        for key in stamp_without:
            stamp.pop(key, None)
    (prefix / ".colosseum-nuvio-hardened-stamp").write_text(
        json.dumps(stamp, indent=2) + "\n", encoding="utf-8"
    )
    return prefix


def test_package_gate_rejects_self_consistent_header_only_artifact():
    # A forged stamp and 64-bit ELF header are not a genuine hardened library:
    # verify load segments, C API exports and donor build/source provenance.
    with tempfile.TemporaryDirectory(prefix="nuvio-gate-") as temp:
        prefix = _make_fixture_prefix(pathlib.Path(temp))
        result = _run_verifier(prefix)
        assert result.returncode != 0, "header-only ELF/stamp must not pass provenance"
        assert "16 KiB" in result.stderr or "provenance" in result.stderr


def test_package_gate_rejects_missing_stamp():
    with tempfile.TemporaryDirectory(prefix="nuvio-gate-") as temp:
        prefix = _make_fixture_prefix(pathlib.Path(temp))
        (prefix / ".colosseum-nuvio-hardened-stamp").unlink()
        result = _run_verifier(prefix)
        assert result.returncode != 0, "a missing stamp must fail the gate"
        assert "stamp missing" in result.stderr


def test_package_gate_rejects_empty_stamp():
    with tempfile.TemporaryDirectory(prefix="nuvio-gate-") as temp:
        prefix = _make_fixture_prefix(pathlib.Path(temp))
        (prefix / ".colosseum-nuvio-hardened-stamp").write_text("", encoding="utf-8")
        result = _run_verifier(prefix)
        assert result.returncode != 0, "an empty stamp must fail the gate"
        assert "empty" in result.stderr


def test_package_gate_rejects_forged_stamp_fields():
    with tempfile.TemporaryDirectory(prefix="nuvio-gate-") as temp:
        prefix = _make_fixture_prefix(
            pathlib.Path(temp),
            stamp_overrides={"source_commit": "deadbeef" * 5},
        )
        result = _run_verifier(prefix)
        assert result.returncode != 0, "a forged source identity must fail the gate"
        assert "stale or forged" in result.stderr


def test_package_gate_rejects_stale_artifact_bytes():
    with tempfile.TemporaryDirectory(prefix="nuvio-gate-") as temp:
        prefix = _make_fixture_prefix(pathlib.Path(temp))
        library = prefix / "android" / "arm64-v8a" / "lib" / "libnuvio_engine.so"
        library.write_bytes(ELF_HEADER_ARM64_SO + b"stale-extra-bytes")
        result = _run_verifier(prefix)
        assert result.returncode != 0, "a rewritten library must fail the gate"
        assert "stale or forged" in result.stderr


def test_package_gate_rejects_wrong_abi():
    with tempfile.TemporaryDirectory(prefix="nuvio-gate-") as temp:
        prefix = _make_fixture_prefix(
            pathlib.Path(temp),
            library_bytes=b"PK\x03\x04 not-an-elf-archive",
        )
        result = _run_verifier(prefix)
        assert result.returncode != 0, "a zip/aar binary must fail the gate"
        assert "ELF" in result.stderr


def test_package_gate_rejects_foreign_patch_identity():
    with tempfile.TemporaryDirectory(prefix="nuvio-gate-") as temp:
        prefix = _make_fixture_prefix(
            pathlib.Path(temp),
            stamp_overrides={"patch_sha256": "0" * 64},
        )
        result = _run_verifier(prefix)
        assert result.returncode != 0, "an artifact built from a foreign patch must fail"
        assert "patch" in result.stderr


def test_package_gate_rejects_missing_provenance_fields():
    with tempfile.TemporaryDirectory(prefix="nuvio-gate-") as temp:
        prefix = _make_fixture_prefix(
            pathlib.Path(temp), stamp_without=["libtorrent_patch_count"]
        )
        result = _run_verifier(prefix)
        assert result.returncode != 0, "missing provenance fields must fail the gate"


def test_prepare_script_sentinel_lists_are_contradiction_free():
    namespace: dict = {}
    source = (ROOT / "scripts" / "android" / "prepare_nuvio_engine.py").read_text(
        encoding="utf-8"
    )
    prepare = compile(source, "prepare_nuvio_engine.py", "exec")
    # Execute only the constant block: stop before main() machinery.
    head = source.split("def die(")[0]
    exec(compile(head, "prepare_constants", "exec"), namespace)  # noqa: S102
    present = namespace["PATCH_SENTINELS"]
    absent = namespace["REQUIRED_ABSENT"]
    for relative, sentinels in present.items():
        overlap = set(sentinels) & set(absent.get(relative, []))
        assert not overlap, (
            f"{relative} requires {sorted(overlap)} both present and absent: "
            "no source tree could ever verify"
        )
    # The removed speculative helper must stay on the absent list.
    assert "finished_run_end" in absent.get(
        "src/torrent/libtorrent_stream_bridge.cpp", []
    ), "the speculative block_finished path helper must stay banned"


def test_prepare_script_pins_the_commit_not_just_the_tag():
    prepare = PREPARE
    assert "PINNED_SOURCE_COMMIT" in prepare, "the exact commit must be pinned"
    assert PINNED_SOURCE_COMMIT in prepare, "the pinned commit value must be recorded"
    match = re.search(r'PINNED_SOURCE_COMMIT = "([0-9a-f]{40})"', prepare)
    assert match is not None and match.group(1) == PINNED_SOURCE_COMMIT
    assert "{PINNED_TAG}^{{commit}}" in prepare, "the tag must be peeled to its commit"


def test_libtorrent_patch_sources_allow_windows_crlf_but_reject_bare_cr():
    """All twelve patches survive checkout EOL conversion, but not malformed CR."""
    donor = _required_donor()
    import importlib.util
    import shutil
    import contextlib
    import io
    spec = importlib.util.spec_from_file_location(
        "nuvio_prepare_patch_eol", ROOT / "scripts/android/prepare_nuvio_engine.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    table = module.validate_libtorrent_patch_sources(donor)
    assert len(table) == 12
    with tempfile.TemporaryDirectory(prefix="nuvio-patch-eol-") as temp:
        fixture = pathlib.Path(temp)
        dest = fixture / "cmake"
        (dest / "patches").mkdir(parents=True)
        shutil.copyfile(donor / "cmake/Dependencies.cmake", dest / "Dependencies.cmake")
        for patch, _target, _sentinel in table:
            relative = pathlib.Path(patch.split("}/", 1)[-1])
            shutil.copyfile(donor / "cmake" / relative, dest / relative)
        assert len(module.validate_libtorrent_patch_sources(fixture)) == 12
        first = dest / table[0][0].split("}/", 1)[-1]
        canonical = module.portable_libtorrent_patch_bytes(first)
        assert b"\r" not in canonical and b"\n" in canonical
        first.write_bytes(canonical.replace(b"\n", b"\r\n"))
        assert len(module.validate_libtorrent_patch_sources(fixture)) == 12, (
            "CRLF checkout must retain the exact twelve patched source declarations"
        )
        first.write_bytes(canonical.replace(b"\n", b"\r"))
        output = io.StringIO()
        with contextlib.redirect_stderr(output):
            try:
                module.validate_libtorrent_patch_sources(fixture)
            except SystemExit as error:
                assert error.code != 0
            else:
                raise AssertionError("malformed bare-CR patch was accepted")
        assert "invalid CR" in output.getvalue()


def test_vendored_patch_applies_to_pristine_pinned_donor():
    """Prove the vendored patch applies to a pristine extraction of the pin.

    Uses the real donor worktree when NUVIO_DONOR_SOURCE (or the task default)
    is present; otherwise reports a skip so CI-style runs stay honest.
    """
    import os
    import shutil

    donor = os.environ.get("NUVIO_DONOR_SOURCE", "")
    if not donor or not pathlib.Path(donor, ".git").exists():
        import unittest
        raise unittest.SkipTest("no donor worktree available")
    script = ROOT / "scripts" / "android" / "prepare_nuvio_engine.py"
    result = subprocess.run(
        [sys.executable, str(script), "--source", donor, "--verify-only"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    assert result.returncode == 0, (
        f"preparation provenance verification failed: {result.stderr}"
    )
    assert "pristine" in result.stdout or "pristine" in result.stderr



def _required_donor():
    import os
    import unittest
    donor = pathlib.Path(os.environ.get("NUVIO_DONOR_SOURCE", ""))
    if not os.environ.get("NUVIO_DONOR_SOURCE") or not (donor / ".git").exists():
        raise unittest.SkipTest("requires real NUVIO_DONOR_SOURCE")
    return donor


def test_prepare_rejects_wrong_pinned_commit():
    # Exercise the script's real verification function, with a deliberately
    # incorrect expected pin, without mutating the donor repository.
    import contextlib
    import importlib.util
    import io
    source = _required_donor()
    script = ROOT / "scripts/android/prepare_nuvio_engine.py"
    spec = importlib.util.spec_from_file_location("nuvio_prepare_wrong_pin", script)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.PINNED_SOURCE_COMMIT = "0" * 40
    errors = io.StringIO()
    with contextlib.redirect_stderr(errors):
        try:
            module.verify_source(source, VENDORED_PATCH)
        except SystemExit as error:
            assert error.code != 0
        else:
            raise AssertionError("wrong source pin was accepted")
    assert "not the pinned hardening base" in errors.getvalue()


def test_prepare_rejects_modified_patch_drift():
    source = _required_donor()
    with tempfile.TemporaryDirectory(prefix="nuvio-drift-") as temp:
        altered = pathlib.Path(temp) / "altered.patch"
        altered.write_bytes(VENDORED_PATCH.read_bytes() + b"\n# untrusted drift\n")
        cmd = [sys.executable, str(ROOT / "scripts/android/prepare_nuvio_engine.py"),
               "--source", str(source), "--patch", str(altered), "--verify-only"]
        result = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8")
        assert result.returncode != 0, "modified patch must fail verification"
        assert "patch" in result.stderr.lower() or "drift" in result.stderr.lower()


def test_android_glue_uses_resolvable_applicationinfo_signature():
    # 2026-10-19 device crash (pid 8494): the unqualified ApplicationInfo
    # signature made GetMethodID fail (NoSuchMethodError) and the unguarded
    # follow-up GetFieldID(NULL jclass) aborted the app under CheckJNI.
    require(
        STREAMSERVER_ANDROID,
        '()Landroid/content/pm/ApplicationInfo;',
        'getApplicationInfo must use the real android.content.pm.ApplicationInfo signature',
    )
    without_right = STREAMSERVER_ANDROID.replace('()Landroid/content/pm/ApplicationInfo;', '')
    assert '()Landroid/content/ApplicationInfo;' not in without_right, \
        'the unqualified ApplicationInfo signature must never return'
    require(
        STREAMSERVER_ANDROID,
        'if (!info.isValid())',
        'the nativeLibraryDir lookup must guard a null ApplicationInfo object',
    )


def main():
    tests = [value for name, value in sorted(globals().items()) if name.startswith("test_")]
    failures = 0
    skipped = 0
    import unittest
    for test in tests:
        try:
            test()
            print(f"PASS {test.__name__}")
        except unittest.SkipTest as error:
            skipped += 1
            print(f"SKIP {test.__name__}: {error}")
        except AssertionError as error:
            failures += 1
            print(f"FAIL {test.__name__}: {error}")
    print(f"EXECUTED_CASES={len(tests)} SKIPPED_CASES={skipped}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
