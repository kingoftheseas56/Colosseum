$ErrorActionPreference = 'Continue'
$root = 'D:\Colosseum-Android-Phone-Mocks-20261101\outputs\12-account-forms'
$chrome = 'C:\Program Files\Google\Chrome\Application\chrome.exe'
$pages = @('account-profile','account-security','account-recovery','account-privacy','account-devices','account-your-colosseum')
$presets = @(
  @{ Label='portrait'; Preset='phone412'; W=412; H=900 },
  @{ Label='landscape'; Preset='landscape900'; W=900; H=412 },
  @{ Label='desktop'; Preset='desktop'; W=1280; H=700 }
)
$jobs = @()
foreach ($page in $pages) {
  foreach ($p in $presets) {
    $jobs += @{ Page=$page; Label=$p.Label; Preset=$p.Preset; W=$p.W; H=$p.H; State='default'; Other='' }
  }
}
$jobs += @{ Page='account-profile'; Label='long-small'; Preset='phone354'; W=354; H=792; State='long'; Other='&font=1.3' }
$jobs += @{ Page='account-security'; Label='approval'; Preset='phone412'; W=412; H=900; State='approval'; Other='' }
$jobs += @{ Page='account-security'; Label='password-ime'; Preset='landscape900'; W=900; H=412; State='password'; Other='&keyboard=1&top=24&bottom=24' }
$jobs += @{ Page='account-recovery'; Label='replace-ime'; Preset='phone412'; W=412; H=900; State='replace'; Other='&keyboard=1&top=24&bottom=24' }
$jobs += @{ Page='account-privacy'; Label='delete-ime'; Preset='phone412'; W=412; H=900; State='delete'; Other='&keyboard=1&top=24&bottom=24' }
$jobs += @{ Page='account-devices'; Label='revoke'; Preset='phone412'; W=412; H=900; State='revoke'; Other='' }
$jobs += @{ Page='account-your-colosseum'; Label='long-small'; Preset='phone354'; W=354; H=792; State='long'; Other='&font=1.3' }
$receipts = @()
$i = 0
foreach ($job in $jobs) {
  $i++
  $out = Join-Path $root ('captures\' + $job.Page + '-' + $job.Label + '.png')
  $profile = Join-Path $root ('.headless-ps-' + $i)
  $url = 'file:///D:/Colosseum-Android-Phone-Mocks-20261101/outputs/12-account-forms/' + $job.Page + '.html?capture=1&preset=' + $job.Preset + '&state=' + $job.State + $job.Other
  $args = @('--headless=new','--disable-gpu','--disable-extensions','--no-first-run','--hide-scrollbars','--force-device-scale-factor=1',
            ('--user-data-dir=' + $profile), ('--window-size=' + $job.W + ',' + $job.H),
            '--virtual-time-budget=1500', ('--screenshot=' + $out), $url)
  & $chrome @args | Out-Null
  for ($count=0; $count -lt 70; $count++) {
    if ((Test-Path $out) -and (Get-Item $out).Length -gt 100) { break }
    Start-Sleep -Milliseconds 100
  }
  $exists = (Test-Path $out) -and (Get-Item $out).Length -gt 100
  if ($exists) {
    $receipts += @{ file=$job.Page; label=$job.Label; preset=$job.Preset; state=$job.State; viewport=($job.W.ToString() + 'x' + $job.H.ToString()); capture=('captures/' + $job.Page + '-' + $job.Label + '.png') }
    Write-Output ('PASS ' + $i + '/' + $jobs.Count + ' ' + $job.Page + '/' + $job.Label)
  } else { Write-Output ('FAIL ' + $i + '/' + $jobs.Count + ' ' + $job.Page + '/' + $job.Label) }
}
$manifest = @{ tool='Installed Google Chrome headless=new launched by PowerShell within isolated profiles'; attempted=$jobs.Count; verified=$receipts.Count; images=$receipts }
$manifest | ConvertTo-Json -Depth 6 | Set-Content -Path (Join-Path $root 'captures\capture-manifest.json') -Encoding UTF8
Write-Output ('CAPTURE_TOTAL ' + $receipts.Count + ' OF ' + $jobs.Count)
