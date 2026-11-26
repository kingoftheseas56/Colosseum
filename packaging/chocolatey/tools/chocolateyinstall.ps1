$ErrorActionPreference = 'Stop'

$packageName  = 'colosseum'
$url          = 'https://github.com/kingoftheseas56/Colosseum/releases/download/v1.4.0/Colosseum-1.4.0-setup.exe'

# SHA-256 of Colosseum-1.4.0-setup.exe (233,684,944 bytes) from the official v1.4.0 release.
$checksum     = 'd8a374ab89818e2e68af53c8d10ca5ffbec7f748e0ee9804ab28504db92b5685'
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
