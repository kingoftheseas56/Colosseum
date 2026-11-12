$ErrorActionPreference = 'Stop'

$packageName  = 'colosseum'
$url          = 'https://github.com/kingoftheseas56/Colosseum/releases/download/v1.3.0/Colosseum-1.3.0-setup.exe'

# SHA-256 of Colosseum-1.3.0-setup.exe (231,444,357 bytes) from the official v1.3.0 release.
$checksum     = '665fdcc923f6ab70f32377a77082a2821af2ef5bd0d8c055c529df9b69675013'
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
