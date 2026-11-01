$ErrorActionPreference = 'Continue'
$root = 'D:\Colosseum-Android-Phone-Mocks-20261101\outputs\12-account-forms'
$chrome = 'C:\Program Files\Google\Chrome\Application\chrome.exe'
$pages = @('account-profile','account-security','account-recovery','account-privacy','account-devices','account-your-colosseum')
foreach ($page in $pages) {
  $canonical = Join-Path $root ('captures\' + $page + '-portrait.png')
  $out = Join-Path $root ('captures\' + $page + '-portrait-fixed.png')
  if (Test-Path $out) { Remove-Item -LiteralPath $out -Force }
  $profile = Join-Path $root ('.headless-refresh-' + [guid]::NewGuid().ToString('N'))
  $url = 'file:///D:/Colosseum-Android-Phone-Mocks-20261101/outputs/12-account-forms/' + $page + '.html?capture=1&preset=phone412'
  $args = @('--headless=new','--disable-gpu','--disable-extensions','--no-first-run','--hide-scrollbars','--force-device-scale-factor=1',
    ('--user-data-dir=' + $profile),'--window-size=412,900','--virtual-time-budget=1500',('--screenshot=' + $out),$url)
  & $chrome @args | Out-Null
  for ($n=0; $n -lt 60; $n++) {
    if ((Test-Path $out) -and (Get-Item $out).Length -gt 100) { break }
    Start-Sleep -Milliseconds 100
  }
  if ((Test-Path $out) -and (Get-Item $out).Length -gt 100) {
    Copy-Item -LiteralPath $out -Destination $canonical -Force
    Write-Output ('PASS-REFRESH ' + $page)
  } else { Write-Output ('FAIL-REFRESH ' + $page) }
}
