$ErrorActionPreference = 'Stop'

$packageName  = 'colosseum'
$url          = 'https://github.com/kingoftheseas56/Colosseum/releases/download/v1.1.7/Colosseum-1.1.7-setup.exe'

# SHA-256 of Colosseum-1.1.7-setup.exe (254,834,672 bytes) from the official v1.1.7 release.
$checksum     = '66c5dfcc9607176cd890c14dc65a3ecdf72bc9e1b3852d968b87b6fae09b37a7'
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
