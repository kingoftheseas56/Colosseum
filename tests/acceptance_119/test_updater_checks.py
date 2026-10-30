"""Acceptance-runner tests, including deliberate red signature/process controls.

These tests validate evidence collection; real updater behavior is recorded by
updater_checks.py as separate production-component/native cases.
"""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
MODULE = Path(__file__).with_name("updater_checks.py")


class UpdaterChecksTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(MODULE.is_file(), "updater evidence implementation is missing")
        spec = importlib.util.spec_from_file_location("acceptance_updater_checks", MODULE)
        self.impl = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.impl)
        self.temp = tempfile.TemporaryDirectory(prefix="acceptance-updater-test-")
        self.addCleanup(self.temp.cleanup)
        self.out = Path(self.temp.name)

    def fixture_source(self):
        root = self.out / "source"
        for relative in ("native/update/UpdatePublicKey.h", "native/main.cpp",
                         "resources/installed-chronicle/installed-manifest.json",
                         "resources/installed-chronicle/installed-manifest.json.sig"):
            target = root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, target)
        return root

    def test_real_production_signed_chronicle_verifies_version_and_six_chapters(self):
        result = self.impl.check_production_chronicle(ROOT, self.out)
        self.assertEqual(result["version"], "1.1.9")
        self.assertEqual(result["highlights"], 6)
        self.assertEqual(result["public_key_der_sha256"], self.impl.PRODUCTION_DER_SHA256)

    def test_tampered_chronicle_is_rejected_by_actual_signature_verifier(self):
        root = self.fixture_source()
        path = root / "resources/installed-chronicle/installed-manifest.json"
        path.write_bytes(path.read_bytes().replace(b'Colosseum', b'Colosxeum', 1))
        with self.assertRaises(subprocess.CalledProcessError):
            self.impl.check_production_chronicle(root, self.out)

    def test_tampered_signature_is_rejected_by_actual_signature_verifier(self):
        root = self.fixture_source()
        path = root / "resources/installed-chronicle/installed-manifest.json.sig"
        raw = bytearray(path.read_bytes()); raw[0] ^= 1; path.write_bytes(raw)
        with self.assertRaises(subprocess.CalledProcessError):
            self.impl.check_production_chronicle(root, self.out)

    def test_changed_public_key_fails_continuity(self):
        root = self.fixture_source()
        path = root / "native/update/UpdatePublicKey.h"
        path.write_text(path.read_text().replace("0x19", "0x18", 1))
        with self.assertRaisesRegex(ValueError, "public-key continuity"):
            self.impl.check_production_chronicle(root, self.out)

    def test_discovery_check_is_explicitly_static_and_requires_exact_assignment(self):
        self.assertEqual(self.impl.check_discovery_url(ROOT)["layer"], "static-source")
        root = self.fixture_source()
        path = root / "native/main.cpp"
        path.write_text(path.read_text().replace(
            "https://getcolosseum.world/updates/stable.json", "https://evil.example/updates/stable.json"))
        with self.assertRaisesRegex(ValueError, "discovery URL"):
            self.impl.check_discovery_url(root)

    def test_nonzero_process_cannot_be_green_even_with_success_sentinel(self):
        case = self.impl.run_command("negative-exit", [sys.executable, "-c",
            "print('COMPONENT_OK'); raise SystemExit(3)"], self.out, ROOT, "COMPONENT_OK")
        self.assertEqual(case["status"], "FAIL")
        self.assertEqual(case["exit_code"], 3)
        self.assertTrue(Path(case["evidence"][0]).is_file())

    def test_zero_process_without_sentinel_cannot_be_green(self):
        case = self.impl.run_command("negative-sentinel", [sys.executable, "-c", "print('nothing')"],
                                     self.out, ROOT, "COMPONENT_OK")
        self.assertEqual(case["status"], "FAIL")

    def test_skipped_component_tests_are_not_a_pass(self):
        case = self.impl.run_command("skipped", [sys.executable, "-c",
            "print('OK (skipped=1)')"], self.out, ROOT, "OK")
        self.assertEqual(case["status"], "BLOCKED")
        self.assertFalse(case["assertions"][-1]["pass"])

    def test_tampered_update_becomes_a_failed_machine_readable_case(self):
        root = self.fixture_source()
        path = root / "resources/installed-chronicle/installed-manifest.json.sig"
        raw = bytearray(path.read_bytes()); raw[0] ^= 1; path.write_bytes(raw)
        report = self.impl.run_checks(root, self.out, cases=["UPD-PROD-CHRONICLE"])
        self.assertEqual(report["counts"]["FAIL"], 1)
        self.assertFalse(report["qualified"])
        self.assertEqual(report["cases"][0]["exit_code"], 1)

    def test_missing_native_component_is_blocked(self):
        case = self.impl.run_command("missing-native", [str(self.out / "does-not-exist")],
                                     self.out, ROOT, "COMPONENT_OK")
        self.assertEqual(case["status"], "BLOCKED")
        self.assertIsNone(case["exit_code"])

    def test_timeout_is_failure_and_preserves_partial_output(self):
        case = self.impl.run_command("timeout", [sys.executable, "-u", "-c",
            "import time; print('started'); time.sleep(3)"], self.out, ROOT, "OK", timeout=.05)
        self.assertEqual(case["status"], "FAIL")
        self.assertIn("started", Path(case["evidence"][0]).read_text())
        self.assertTrue(case["timed_out"])

    def test_existing_check_output_is_machine_readable_without_qualifying_windows(self):
        report = self.impl.run_checks(ROOT, self.out, cases=["UPD-PROD-CHRONICLE", "WIN-AUTO-HANDOFF"])
        indexed = {case["case_id"]: case for case in report["cases"]}
        self.assertEqual(indexed["UPD-PROD-CHRONICLE"]["status"], "PASS")
        self.assertEqual(indexed["WIN-AUTO-HANDOFF"]["status"], "BLOCKED")
        self.assertFalse(report["qualified"])
        loaded = json.loads((self.out / "updater-results.json").read_text())
        self.assertEqual(loaded["counts"]["BLOCKED"], 1)
        self.assertEqual(loaded["source_sha"], self.impl.SOURCE_SHA)

    def test_unknown_case_is_not_silently_ignored(self):
        with self.assertRaisesRegex(ValueError, "unknown case"):
            self.impl.run_checks(ROOT, self.out, cases=["typo-case"])


class WindowsReceivingSourceContracts(unittest.TestCase):
    """Static guardrails only; these never qualify a Windows run."""
    def script(self):
        path = Path(__file__).with_name("windows_receiving.ps1")
        self.assertTrue(path.is_file(), "Windows receiving runner is missing")
        return path.read_text(encoding="utf-8")

    def test_requires_disposable_account_and_explicit_candidate_identity(self):
        text = self.script()
        for token in ("ConfirmDisposableWindowsAccount", "ExpectedAppSha256", "SessionFile",
                      "Baseline118", "After119", "Restart119", "COLOSSEUM_APPDATA_TAG",
                      "COLOSSEUM_LANISTA_PIPE", "Get-FileHash", "--version"):
            self.assertIn(token, text)

    def test_synthetic_fixture_binding_and_seed_are_fresh_baseline_only(self):
        text = self.script()
        for token in ("fixture-manifest.json", "SeedName", "Seed is allowed only for a new Baseline118",
                      "Unbound scenario fixture token", "fixture_manifest_sha256"):
            self.assertIn(token, text)

    def test_only_trailing_quit_is_normalized_and_runner_owns_shutdown(self):
        text = self.script()
        for token in ("function Prepare-Scenario", "Ctrl+Q is permitted only as the final command",
                      "presentation_wait_ms", "normal_quit_reply"):
            self.assertIn(token, text)

    def test_all_colosseum_overrides_are_cleared_and_write_gate_is_off(self):
        text = self.script()
        self.assertIn("Get-ChildItem Env:", text)
        self.assertIn("COLOSSEUM_*", text)
        self.assertIn("$answer.gates.write -eq $false", text)
        self.assertIn("$answer.pipe -eq $script:pipe", text)

    def test_shutdown_is_identity_bound_and_forced_recovery_is_red(self):
        text = self.script()
        self.assertIn("function Test-OwnedProcess", text)
        self.assertIn("function Classify-JourneyFailure", text)
        self.assertIn("Stock 1.1.8 lacks readerCommand_layout", text)
        self.assertIn("if ($report.status -eq 'BLOCKED') { exit 2 }", text)
        self.assertIn("function Recover-OwnedApp", text)
        self.assertIn("$script:appHandle = $script:app.SafeHandle", text)
        self.assertIn("$report.cleanup_recovery = $recovery", text)
        self.assertIn("CreationDate", text)
        self.assertIn("CloseMainWindow", text)
        self.assertIn("forced_recovery", text)
        self.assertNotIn("Stop-Process -Name", text)
        self.assertNotIn("taskkill", text.lower())
        self.assertNotIn("-Recurse -Force", text)

    def test_requires_real_ping_state_and_preserves_manual_update_boundary(self):
        text = self.script()
        for token in ("ping", "get-state", "appDataRoot", "cacheRoot", "WIN-AUTO-HANDOFF",
                      "BLOCKED", "NOT RUN", "installer_sha256", "installer_size"):
            self.assertIn(token, text)
        self.assertNotIn('ArgumentList "/S"', text)
        self.assertNotIn("WriteReg", text)


class ChronicleScenarioContracts(unittest.TestCase):
    def test_selected_scenario_requires_six_current_chapters_and_real_controls(self):
        path = ROOT / "tests/lanista_scenarios/acceptance_119/update_chronicle_ui.json"
        self.assertTrue(path.is_file(), "Selected 1.1.9 chronicle scenario is missing")
        scenario = json.loads(path.read_text())
        steps = scenario["steps"]
        self.assertFalse({"qml-set", "ui-invoke"} & {step["cmd"] for step in steps})
        titles = {step.get("payload", {}).get("value") for step in steps
                  if step["cmd"] == "ui-wait-for" and step.get("payload", {}).get("object") == "colosseumUpdateChapterTitle"}
        self.assertEqual(titles, {"Your fourth world", "Sail all four seas", "Your local media library",
                                 "Welcome to the House", "Resume and search reliably", "First-party Windows updates"})
        self.assertIn("colosseumUpdateBackAction", {step.get("payload", {}).get("target") for step in steps})
        self.assertIn("Escape", {step.get("payload", {}).get("key") for step in steps})


POWERSHELL_HELPER_CHECK = r'''
param([string]$ScriptPath)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$tokens = $null; $parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($ScriptPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -ne 0) { $parseErrors | Format-List; exit 1 }
Write-Output ('POWERSHELL_SYNTAX_OK version=' + $PSVersionTable.PSVersion)
$function = $ast.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Prepare-Scenario'}, $true)
if ($null -eq $function) { throw 'Prepare-Scenario function is missing' }
. ([scriptblock]::Create($function.Extent.Text))
$normal = Prepare-Scenario ('{"steps":[{"cmd":"ping"},{"cmd":"ui-keypress","payload":{"key":"Ctrl+Q"}},{"presentation_wait_ms":500}]}' | ConvertFrom-Json)
if ($normal.steps.Count -ne 1 -or $normal.steps[0].cmd -ne 'ping') { throw 'Trailing quit/drain was not removed' }
$single = Prepare-Scenario ('{"steps":[{"cmd":"ping"},{"cmd":"ui-keypress","payload":{"key":"Ctrl+Q"}}]}' | ConvertFrom-Json)
if ($single.steps.Count -ne 1) { throw 'Sole trailing quit was not removed' }
$keep = Prepare-Scenario ('{"steps":[{"cmd":"ping"},{"presentation_wait_ms":500}]}' | ConvertFrom-Json)
if ($keep.steps.Count -ne 2) { throw 'Unrelated drain was removed' }
foreach ($text in @(
    '{"steps":[{"cmd":"ping"},{"cmd":"ui-keypress","payload":{"key":"Ctrl+Q"}},{"cmd":"get-state"}]}',
    '{"steps":[{"cmd":"ui-keypress","payload":{"key":"Ctrl+Q"}}]}'
)) {
    $rejected = $false
    try { Prepare-Scenario ($text | ConvertFrom-Json) | Out-Null } catch { $rejected = $true }
    if (!$rejected) { throw 'Invalid quit layout did not reject' }
}
Write-Output 'POWERSHELL_NORMALIZER_OK cases=5 platform=Linux scope=helper-only'

$recoveryFunction = $ast.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Recover-OwnedApp'}, $true)
if ($null -eq $recoveryFunction) { throw 'CIM-failure recovery helper is missing' }
. ([scriptblock]::Create($recoveryFunction.Extent.Text))

$classifierFunction = $ast.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Classify-JourneyFailure'}, $true)
if ($null -eq $classifierFunction) { throw 'Baseline failure classifier is missing' }
. ([scriptblock]::Create($classifierFunction.Extent.Text))
$missing = '  reply layout: {"type":"error","code":"NO_SUCH_ITEM","message":"readerCommand_layout"}'
$blocked = Classify-JourneyFailure 'Baseline118' 1 $false $missing
if ($blocked.status -ne 'BLOCKED' -or $blocked.failed_selector -ne 'readerCommand_layout') { throw 'Known baseline selector absence must be BLOCKED with exact selector' }
foreach ($candidate in @(
    @{phase='After119'; code=1; timeout=$false; text=$missing},
    @{phase='Baseline118'; code=4; timeout=$false; text=$missing},
    @{phase='Baseline118'; code=1; timeout=$true; text=$missing},
    @{phase='Baseline118'; code=1; timeout=$false; text='  reply page: {"type":"reply","props":{"currentPage":1}}'},
    @{phase='Baseline118'; code=1; timeout=$false; text='NO_SUCH_ITEM readerCommand_layout'},
    @{phase='Baseline118'; code=1; timeout=$false; text=$missing.Replace('readerCommand_layout','unexpectedMissingControl')}
)) {
    $classified = Classify-JourneyFailure $candidate.phase $candidate.code $candidate.timeout $candidate.text
    if ($classified.status -ne 'FAIL') { throw 'Non-baseline-selector failure was incorrectly downgraded' }
}
Write-Output 'POWERSHELL_BASELINE_CLASSIFIER_OK cases=7 platform=Linux scope=protocol-fixture-only'

$childInfo = [Diagnostics.ProcessStartInfo]::new()
$childInfo.FileName = (Get-Process -Id $PID).Path
$childInfo.UseShellExecute = $false
$childInfo.CreateNoWindow = $true
$childInfo.ArgumentList.Add('-NoProfile')
$childInfo.ArgumentList.Add('-NonInteractive')
$childInfo.ArgumentList.Add('-Command')
$childInfo.ArgumentList.Add('Start-Sleep -Seconds 30')
$child = [Diagnostics.Process]::Start($childInfo)
$childHandle = $child.SafeHandle
$unrelated = [Diagnostics.Process]::Start($childInfo)
$unrelatedHandle = $unrelated.SafeHandle
try {
    function Observe-Children { throw 'Synthetic CIM failure' }
    $recovered = $null
    try { Observe-Children } catch { $recovered = Recover-OwnedApp $child 100 }
    if ($null -eq $recovered -or !$recovered.forced -or $recovered.surviving_pid) { throw 'CIM failure did not force and reap the exact owned child' }
    if (!$child.HasExited -or $recovered.pid -ne $child.Id) { throw 'Owned child was not stopped' }
    $unrelated.Refresh()
    if ($unrelated.HasExited) { throw 'Recovery affected the unrelated process' }
    $again = Recover-OwnedApp $child 100
    if ($again.forced -or $again.surviving_pid) { throw 'Already exited child was incorrectly killed again' }
    Write-Output 'POWERSHELL_RECOVERY_OK cases=3 platform=Linux scope=synthetic-process-only'
} finally {
    foreach ($testProcess in @($child, $unrelated)) {
        if (!$testProcess.HasExited) { $testProcess.Kill(); $testProcess.WaitForExit(5000) | Out-Null }
        $testProcess.Dispose()
    }
}

'''

def check_powershell(powershell):
    """Parse source and exercise only the real pure normalizer on this host."""
    with tempfile.TemporaryDirectory(prefix="acceptance-powershell-") as folder:
        script = Path(folder) / "check.ps1"
        script.write_text(POWERSHELL_HELPER_CHECK, encoding="utf-8")
        return subprocess.run([powershell, "-NoProfile", "-NonInteractive", "-File", str(script),
            "-ScriptPath", str(Path(__file__).with_name("windows_receiving.ps1"))], check=False).returncode


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--powershell":
        raise SystemExit(check_powershell(sys.argv[2]))
    unittest.main()
