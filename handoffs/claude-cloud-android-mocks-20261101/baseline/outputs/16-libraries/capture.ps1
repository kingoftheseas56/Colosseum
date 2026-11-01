# Isolated headless Chrome screenshots on Laptop 1. Never uses the live GUI browser.
param([string]$World="all",[string]$Mode="all",[switch]$Full)
$ErrorActionPreference="Continue"
$root=Split-Path -Parent $MyInvocation.MyCommand.Path
$out=Join-Path $root "captures"
$chrome="C:\Program Files\Google\Chrome\Application\chrome.exe"
$profile=Join-Path $out "_one-use-chrome-profile"
$cases=@(
  @{Name="portrait";W=412;H=900;Platform="android";State="normal";Scale=1;Top=0;Bottom=0},
  @{Name="landscape";W=900;H=412;Platform="android";State="normal";Scale=1;Top=0;Bottom=0},
  @{Name="desktop";W=1280;H=700;Platform="desktop";State="normal";Scale=1;Top=0;Bottom=0},
  @{Name="menu";W=412;H=900;Platform="android";State="menu";Scale=1;Top=0;Bottom=0},
  @{Name="selection";W=412;H=900;Platform="android";State="selection";Scale=1;Top=0;Bottom=0},
  @{Name="confirm";W=412;H=900;Platform="android";State="confirm";Scale=1;Top=0;Bottom=0},
  @{Name="small";W=354;H=792;Platform="android";State="normal";Scale=1;Top=0;Bottom=0},
  @{Name="small-landscape";W=792;H=354;Platform="android";State="normal";Scale=1;Top=0;Bottom=0},
  @{Name="tablet";W=800;H=1280;Platform="android";State="normal";Scale=1;Top=0;Bottom=0},
  @{Name="tablet-landscape";W=1280;H=800;Platform="android";State="normal";Scale=1;Top=0;Bottom=0},
  @{Name="scaled";W=354;H=792;Platform="android";State="normal";Scale=1.3;Top=24;Bottom=24}
)
$worlds=if($World -eq "all"){@("theatre","tankoban","biblio")}else{@($World)}
New-Item -ItemType Directory -Path $out -Force | Out-Null
foreach($w in $worlds){
 foreach($c in $cases){
  if($Mode -ne "all" -and $Mode -ne $c.Name){continue}
  if(-not $Full -and $Mode -eq "all" -and @("confirm","small","small-landscape","tablet","tablet-landscape","scaled") -contains $c.Name){continue}
  $profile=Join-Path $out ("_capture_"+$w+"_"+$c.Name+"_"+$PID)
  $file=Join-Path $out ($w+"-"+$c.Name+".png")
  if(Test-Path -LiteralPath $file){Remove-Item -LiteralPath $file -Force}
  $url="file:///D:/Colosseum-Android-Phone-Mocks-20261101/outputs/16-libraries/$w-library.html?capture=1&platform=$($c.Platform)&width=$($c.W)&height=$($c.H)&state=$($c.State)&scale=$($c.Scale)&top=$($c.Top)&bottom=$($c.Bottom)"
  $args=@("--headless=new","--disable-gpu","--disable-extensions","--no-first-run","--no-default-browser-check","--allow-file-access-from-files","--virtual-time-budget=8000",("--user-data-dir="+$profile),("--screenshot="+$file),("--window-size="+$c.W+","+$c.H),$url)
  & $chrome @args 2>&1 | Out-Null
  $size=0
  for($tries=0;$tries -lt 150;$tries++){
   if(Test-Path -LiteralPath $file){$size=(Get-Item -LiteralPath $file).Length}
   if($size -gt 4096){break}
   Start-Sleep -Milliseconds 500
  }
  if($size -gt 4096){Write-Output ("CAPTURE_OK "+$w+"-"+$c.Name+".png "+$c.W+"x"+$c.H+" bytes="+$size)}
  else{Write-Output ("CAPTURE_FAIL "+$w+"-"+$c.Name+".png size="+$size)}
 }
}
# The profile belongs only to this capture job. Ignore locked files if Chrome still closes.
try{if(Test-Path -LiteralPath $profile){Remove-Item -LiteralPath $profile -Recurse -Force -ErrorAction Stop}}catch{Write-Output ("TEMP_PROFILE_STILL_OPEN "+$profile)}
