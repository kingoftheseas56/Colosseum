$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

function Assert-Selector([string]$relativePath, [string]$selector) {
    $path = Join-Path $root $relativePath
    if (!(Test-Path -LiteralPath $path)) {
        throw "Missing QML file: $relativePath"
    }
    $text = Get-Content -LiteralPath $path -Raw
    $needle = '"' + $selector + '"'
    $count = ([regex]::Matches($text, [regex]::Escape($needle))).Count
    if ($count -ne 1) {
        throw "$relativePath must expose selector $selector exactly once; found $count"
    }
}

$selectors = @(
    @('qml/account/AccountWelcome.qml', 'accountWelcomeCreateAccount'),
    @('qml/account/AccountWelcome.qml', 'accountWelcomeSignIn'),
    @('qml/account/AccountCreate.qml', 'accountCreateUsername'),
    @('qml/account/AccountCreate.qml', 'accountCreatePassword'),
    @('qml/account/AccountCreate.qml', 'accountCreateConfirmPassword'),
    @('qml/account/AccountCreate.qml', 'accountCreateSubmit'),
    @('qml/account/AccountSignIn.qml', 'accountSignInUsername'),
    @('qml/account/AccountSignIn.qml', 'accountSignInPassword'),
    @('qml/account/AccountSignIn.qml', 'accountSignInSubmit'),
    @('qml/account/AccountSignIn.qml', 'accountSignInError')
)
$selectors += @(
    @('qml/account/AccountRecoveryKey.qml', 'accountRecoveryKeyValue'),
    @('qml/account/AccountRecoveryKey.qml', 'accountRecoveryKeySaved'),
    @('qml/account/AccountFlyout.qml', 'accountFlyoutUsername'),
    @('qml/TopBar.qml', 'colosseumTopbarAccountButton'),
    @('qml/Main.qml', 'accountHost')
)

foreach ($item in $selectors) {
    Assert-Selector $item[0] $item[1]
}

Write-Host 'ACCOUNT_LANISTA_SELECTOR_CONTRACT_OK'