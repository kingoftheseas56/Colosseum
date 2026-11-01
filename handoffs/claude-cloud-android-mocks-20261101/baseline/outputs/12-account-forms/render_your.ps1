$root='D:\Colosseum-Android-Phone-Mocks-20261101\outputs\12-account-forms'
$chrome='C:\Program Files\Google\Chrome\Application\chrome.exe'
$jobs=@(
 @{Label='portrait';Preset='phone412';Width=412;Height=900;Extra=''},
 @{Label='landscape';Preset='landscape900';Width=900;Height=412;Extra=''},
 @{Label='desktop';Preset='desktop';Width=1280;Height=700;Extra=''},
 @{Label='long-small';Preset='phone354';Width=354;Height=792;Extra='&state=long&font=1.3'}
)
foreach($j in $jobs){
 $fresh=Join-Path $root ('captures\account-your-colosseum-'+$j.Label+'-verified.png')
 $target=Join-Path $root ('captures\account-your-colosseum-'+$j.Label+'.png')
 if(Test-Path $fresh){Remove-Item -LiteralPath $fresh -Force}
 $profile=Join-Path $root ('.headless-your-'+[guid]::NewGuid().ToString('N'))
 $url='file:///D:/Colosseum-Android-Phone-Mocks-20261101/outputs/12-account-forms/account-your-colosseum.html?capture=1&preset='+$j.Preset+$j.Extra
 $args=@('--headless=new','--disable-gpu','--disable-extensions','--no-first-run','--hide-scrollbars','--force-device-scale-factor=1',
  ('--user-data-dir='+$profile),('--window-size='+$j.Width+','+$j.Height),'--virtual-time-budget=1500',('--screenshot='+$fresh),$url)
 & $chrome @args | Out-Null
 for($n=0;$n -lt 120;$n++){
  if((Test-Path $fresh) -and (Get-Item $fresh).Length -gt 12000){break}
  Start-Sleep -Milliseconds 100
 }
 if((Test-Path $fresh) -and (Get-Item $fresh).Length -gt 12000){
   Copy-Item -LiteralPath $fresh -Destination $target -Force
   Write-Output ('PASS-YOUR '+$j.Label)
 }else{Write-Output ('FAIL-YOUR '+$j.Label)}
}
