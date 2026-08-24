$ErrorActionPreference = 'Stop'

$packageName  = 'colosseum'
$softwareName = 'Colosseum*'

# Colosseum registers its uninstaller in the PER-USER hive (HKCU) under
# ...\Uninstall\Colosseum, pointing at "<INSTDIR>\uninstall.exe" (an NSIS uninstaller).
[array]$key = Get-UninstallRegistryKey -SoftwareName $softwareName

if ($key.Count -eq 1) {
  $key | ForEach-Object {
    # UninstallString is quoted, e.g. "<INSTDIR>\uninstall.exe" — strip the quotes.
    $file = "$($_.UninstallString)".Trim('"')

    $packageArgs = @{
      packageName    = $packageName
      fileType       = 'exe'
      silentArgs     = '/S'
      validExitCodes = @(0)
      file           = $file
    }

    Uninstall-ChocolateyPackage @packageArgs
  }
} elseif ($key.Count -eq 0) {
  Write-Warning "$packageName has already been uninstalled by other means, or its registry entry is under a different user hive than the one running Chocolatey (Colosseum registers per-user under HKCU)."
} elseif ($key.Count -gt 1) {
  Write-Warning "$($key.Count) matches found for '$softwareName'. Uninstall was NOT run automatically; remove the extra entries or uninstall manually."
  $key | ForEach-Object { Write-Warning "- $($_.DisplayName) - $($_.UninstallString)" }
}
