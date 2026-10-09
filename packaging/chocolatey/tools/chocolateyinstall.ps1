$ErrorActionPreference = 'Stop'

$packageName  = 'colosseum'
$url          = 'https://github.com/kingoftheseas56/Colosseum/releases/download/v1.1.8/Colosseum-1.1.8-setup.exe'

# SHA-256 of Colosseum-1.1.8-setup.exe (255,013,376 bytes) from the official v1.1.8 release.
$checksum     = '34360219e74da238d1c281391396f6e2ff223f27dd7cb221f4e772ae2660382e'
$checksumType = 'sha256'

$packageArgs = @{
  packageName    = $packageName
  fileType       = 'exe'
  url            = $url
  softwareName   = 'Colosseum*'
  checksum       = $checksum
  checksumType   = $checksumType
  # NSIS (MUI2) installer. '/S' runs it silently; the installer is per-user and
  # extracts to %LOCALAPPDATA%\Programs\Colosseum with no elevation prompt.
  silentArgs     = '/S'
  validExitCodes = @(0)
}

Install-ChocolateyPackage @packageArgs
