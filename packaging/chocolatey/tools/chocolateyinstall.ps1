$ErrorActionPreference = 'Stop'

$packageName  = 'colosseum'
$url          = 'https://github.com/kingoftheseas56/Colosseum/releases/download/v1.1.3/Colosseum-1.1.3-setup.exe'

# SHA-256 of Colosseum-1.1.3-setup.exe (269,338,695 bytes) from the official v1.1.3 release.
$checksum     = '140cefc39a47b932558cabb15adfacd7e991fda7e1762c2681c48a0be9272ee7'
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
