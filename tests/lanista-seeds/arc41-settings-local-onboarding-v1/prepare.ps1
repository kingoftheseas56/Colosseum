[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^arc41-wave5-[A-Za-z0-9-]+$')]
    [string]$Tag,

    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$key = "HKCU\Software\Brotherhood\Colosseum-dltest-$Tag"

if ($Clean) {
    & reg.exe delete $key /f | Out-Null
    Write-Output "Removed $key"
    exit 0
}

$accountKey = "$key\account"
& reg.exe add $accountKey /v onboardingCompleted /t REG_SZ /d true /f | Out-Null
& reg.exe add $accountKey /v localOnlyChosen /t REG_SZ /d true /f | Out-Null
Write-Output "Prepared $accountKey (onboardingCompleted=true, localOnlyChosen=true)"
