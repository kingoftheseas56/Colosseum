param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$ffmpeg = 'C:\tools\ffmpeg-master-latest-win64-gpl-shared\bin\ffmpeg.exe'
if (-not (Test-Path $ffmpeg)) {
    $command = Get-Command ffmpeg -ErrorAction SilentlyContinue
    if (-not $command) { throw 'ffmpeg.exe is required to build Player 2 media fixtures' }
    $ffmpeg = $command.Source
}

New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$commonVideo = @('-f', 'lavfi', '-i', 'testsrc2=size=320x180:rate=24', '-t', '2',
                 '-c:v', 'libx264', '-preset', 'ultrafast', '-pix_fmt', 'yuv420p')

function Invoke-Ffmpeg([string[]]$Arguments) {
    & $ffmpeg -hide_banner -loglevel error -y @Arguments
    if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed with exit code $LASTEXITCODE" }
}

$videoOnly = Join-Path $OutputDirectory 'video-only.mp4'
Invoke-Ffmpeg ($commonVideo + @('-an', $videoOnly))

$av = Join-Path $OutputDirectory 'av.mkv'
Invoke-Ffmpeg @('-f', 'lavfi', '-i', 'testsrc2=size=320x180:rate=24',
                '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000',
                '-t', '2', '-map', '0:v:0', '-map', '1:a:0',
                '-c:v', 'libx264', '-preset', 'ultrafast', '-pix_fmt', 'yuv420p',
                '-c:a', 'aac', '-metadata', 'title=Player 2 A/V fixture', $av)

$twoAudio = Join-Path $OutputDirectory 'two-audio.mkv'
Invoke-Ffmpeg @('-f', 'lavfi', '-i', 'testsrc2=size=320x180:rate=24',
                '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000',
                '-f', 'lavfi', '-i', 'sine=frequency=660:sample_rate=48000',
                '-t', '2', '-map', '0:v:0', '-map', '1:a:0', '-map', '2:a:0',
                '-c:v', 'libx264', '-preset', 'ultrafast', '-pix_fmt', 'yuv420p',
                '-c:a', 'aac', '-metadata:s:a:0', 'language=eng',
                '-metadata:s:a:0', 'title=English', '-metadata:s:a:1', 'language=fra',
                '-metadata:s:a:1', 'title=French', $twoAudio)

$subtitlePath = Join-Path $OutputDirectory 'fixture.srt'
@"
1
00:00:00,100 --> 00:00:01,700
Player 2 subtitle fixture
"@ | Set-Content -Encoding ascii $subtitlePath
$subtitleMedia = Join-Path $OutputDirectory 'embedded-subtitle.mkv'
Invoke-Ffmpeg @('-i', $av, '-i', $subtitlePath, '-map', '0:v:0', '-map', '0:a:0',
                '-map', '1:0', '-c', 'copy', '-c:s', 'srt',
                '-metadata:s:s:0', 'language=eng', $subtitleMedia)

$metadataPath = Join-Path $OutputDirectory 'chapters.ffmeta'
@"
;FFMETADATA1
title=Player 2 chapter fixture
[CHAPTER]
TIMEBASE=1/1000
START=0
END=1000
title=First
[CHAPTER]
TIMEBASE=1/1000
START=1000
END=2000
title=Second
"@ | Set-Content -Encoding ascii $metadataPath
$chaptered = Join-Path $OutputDirectory 'chaptered.mkv'
Invoke-Ffmpeg @('-i', $av, '-i', $metadataPath, '-map', '0', '-map_metadata', '1',
                '-map_chapters', '1', '-c', 'copy', $chaptered)

Remove-Item -LiteralPath $subtitlePath, $metadataPath
Set-Content -Encoding ascii (Join-Path $OutputDirectory '.complete') 'Player 2 deterministic fixtures generated'
