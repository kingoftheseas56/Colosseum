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
    @('qml/account/AccountSignIn.qml', 'accountSignInError'),
    @('qml/account/AccountSignIn.qml', 'accountSignInContinueLocal'),
    @('qml/account/AccountRecoveryKey.qml', 'accountRecoveryKeyValue'),
    @('qml/account/AccountRecoveryKey.qml', 'accountRecoveryKeySaved'),
    @('qml/account/AccountFlyout.qml', 'accountFlyoutUsername'),
    @('qml/account/AccountFlyout.qml', 'accountFlyoutLocalIdentity'),
    @('qml/account/AccountFlyout.qml', 'accountFlyoutLocalDeviceLabel'),
    @('qml/account/AccountFlyout.qml', 'accountFlyoutLocalYourColosseum'),
    @('qml/account/AccountFlyout.qml', 'accountFlyoutLocalPrivacy'),
    @('qml/account/AccountFlyout.qml', 'accountFlyoutLocalSignIn'),
    @('qml/account/AccountFlyout.qml', 'accountFlyoutLocalCreateAccount'),
    @('qml/TopBar.qml', 'colosseumTopbarAccountButton'),
    @('qml/TopBar.qml', 'colosseumTopbarDeviceLabel'),
    @('qml/account/AccountCenter.qml', 'accountCenter'),
    @('qml/account/AccountCenter.qml', 'accountCenterUsername'),
    @('qml/account/AccountYourColosseumPage.qml', 'yourColosseumPage'),
    @('qml/account/AccountYourColosseumPage.qml', 'yourColosseumLocalAccountBlock'),
    @('qml/account/AccountYourColosseumPage.qml', 'yourColosseumLocalSignIn'),
    @('qml/account/AccountYourColosseumPage.qml', 'yourColosseumLocalCreateAccount'),
    @('qml/account/AccountDataPrivacyPage.qml', 'privacyCrossDeviceHistoryGroup'),
    @('qml/account/AccountDataPrivacyPage.qml', 'privacyAccountDataGroup'),
    @('qml/account/AccountDataPrivacyPage.qml', 'privacyDangerZone'),
    @('qml/Main.qml', 'accountHost')
)

foreach ($item in $selectors) {
    Assert-Selector $item[0] $item[1]
}

Write-Host 'ACCOUNT_LANISTA_SELECTOR_CONTRACT_OK'
