[CmdletBinding()]
param(
    [string]$QmlTestRunner,
    [string]$EvidenceDir
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($QmlTestRunner)) {
    $runnerCommand = Get-Command qmltestrunner.exe -ErrorAction SilentlyContinue
    if (-not $runnerCommand) {
        $runnerCommand = Get-Command qmltestrunner -ErrorAction SilentlyContinue
    }
    if ($runnerCommand) {
        $runner = $runnerCommand.Source
    } elseif (-not [string]::IsNullOrWhiteSpace($env:QTDIR)) {
        $candidate = Join-Path (Join-Path $env:QTDIR 'bin') 'qmltestrunner.exe'
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            throw "QTDIR is set, but qmltestrunner was not found at $candidate"
        }
        $runner = (Resolve-Path -LiteralPath $candidate).Path
    } else {
        throw 'qmltestrunner was not found on PATH; pass -QmlTestRunner or set QTDIR.'
    }
} else {
    $runner = (Resolve-Path -LiteralPath $QmlTestRunner -ErrorAction Stop).Path
}
if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) {
    throw "qmltestrunner is not a file: $runner"
}
$qtBin = Split-Path -Parent $runner

if ([string]::IsNullOrWhiteSpace($EvidenceDir)) {
    $EvidenceDir = Join-Path ([System.IO.Path]::GetTempPath()) (
        'theatre-episode-origin-' + [guid]::NewGuid().ToString('N')
    )
}
$EvidenceDir = [System.IO.Path]::GetFullPath($EvidenceDir)
$sourceFiles = @(
    [pscustomobject]@{ Name = 'TheatreSeries'; Path = Join-Path $repo 'qml\TheatreSeries.qml'; IsView = $false },
    [pscustomobject]@{ Name = 'TheatreSeriesView'; Path = Join-Path $repo 'qml\TheatreSeriesView.qml'; IsView = $true }
)
$fixtureDir = Join-Path $EvidenceDir 'generated'
if (Test-Path -LiteralPath $EvidenceDir) {
    throw "Refusing to overwrite existing evidence directory: $EvidenceDir"
}
New-Item -ItemType Directory -Path $fixtureDir -Force | Out-Null

function Get-QmlExpression([string]$Source, [string]$Pattern, [string]$Description) {
    $match = [regex]::Match($Source, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline)
    if (-not $match.Success) { throw "Could not extract $Description" }
    return $match.Groups['expr'].Value.Trim()
}

function Get-QmlFunction([string]$Source, [string]$FunctionName) {
    $escaped = [regex]::Escape($FunctionName)
    $match = [regex]::Match($Source, "(?m)^\s*function\s+$escaped\s*\([^)]*\)\s*\{")
    if (-not $match.Success) { throw "Could not find function $FunctionName" }
    $open = $Source.IndexOf('{', $match.Index)
    $depth = 0
    for ($i = $open; $i -lt $Source.Length; $i++) {
        if ($Source[$i] -eq '{') { $depth++ }
        elseif ($Source[$i] -eq '}') {
            $depth--
            if ($depth -eq 0) { return $Source.Substring($match.Index, $i - $match.Index + 1).Trim() }
        }
    }
    throw "Unbalanced function body: $FunctionName"
}

$template = @"
import QtQuick 2.15
import QtTest 1.2

TestCase {
    id: page
    name: "__NAME__"
    when: windowShown
    width: 640
    height: 686

    property string variantName: "__VARIANT__"
    property bool phoneNarrow: false
    property int totalEpisodes: 60
    property int nextUpEpisodeIndex: -1
    readonly property int compactEpisodeRowHeight: __COMPACT_ROW__
    readonly property int nextUpEpisodeRowHeight: __NEXT_UP_ROW__
    readonly property real episodeVirtualContentY: __EPISODE_ORIGIN__
    property real stickyHeaderHeight: 304

__OFFSET_FUNCTION__

__SCROLL_FUNCTION__

    property real totalSpacerHeight: 1000
    property real targetSectionOffset: 0
    property bool reflowComplete: false
    property real initialContentHeight: 0

    Item {
        id: scene
        width: 640
        height: 686

        Flickable {
            id: flick
            x: 0
            y: 0
            width: scene.width
            height: scene.height
            clip: true
            contentWidth: width
            contentHeight: pageCol.implicitHeight

            Column {
                id: pageCol
                width: flick.width
                spacing: 0

                Item {
                    id: leadingSpacer
                    width: pageCol.width
                    height: 0
                }

                Item {
                    id: episodesSection
                    width: pageCol.width
                    height: episodesCol.implicitHeight

                    Column {
                        id: episodesCol
                        width: episodesSection.width
                        spacing: 0

                        Item {
                            id: episodeVirtualSpace
                            width: episodesCol.width
                            height: page.totalEpisodes * page.compactEpisodeRowHeight

                            Item {
                                id: episode30
                                width: 80
                                height: 40
                                y: page.episodeOffsetForIndex(30) + 24
                            }
                        }
                    }
                }

                Item {
                    id: trailingSpacer
                    width: pageCol.width
                    height: page.totalSpacerHeight
                }
            }
        }

    }

    Timer {
        id: reflowTimer
        interval: 30
        repeat: false
        onTriggered: {
            leadingSpacer.height = page.targetSectionOffset
            trailingSpacer.height = page.totalSpacerHeight - page.targetSectionOffset
            page.reflowComplete = true
        }
    }

    function init() {
        reflowTimer.stop()
        reflowComplete = false
        targetSectionOffset = 0
        leadingSpacer.height = 0
        trailingSpacer.height = totalSpacerHeight
        flick.contentY = 0
        stickyHeaderHeight = 304
        wait(25)
        tryCompare(episodesSection, "y", 0, 1000)
        initialContentHeight = flick.contentHeight
    }

    function setSectionOffsetAsync(offset) {
        targetSectionOffset = offset
        reflowComplete = false
        reflowTimer.restart()
        tryCompare(page, "reflowComplete", true, 1000)
        tryCompare(episodesSection, "y", offset, 1000)
        tryCompare(flick, "contentHeight", initialContentHeight, 1000)
    }

    function currentSectionOrigin() {
        // Read Qt's current geometry at assertion time, independently of the binding.
        return episodeVirtualSpace.mapToItem(pageCol, 0, 0).y
    }

    function expectedScrollY(index) {
        var target = currentSectionOrigin() + page.episodeOffsetForIndex(index)
                     - page.stickyHeaderHeight - 8
        return Math.max(0, Math.min(flick.contentHeight - flick.height, target))
    }

    function test_delayedColumnReflowTracksOrigin() {
        var beforeHeight = flick.contentHeight
        setSectionOffsetAsync(634)
        compare(flick.contentHeight, beforeHeight, "Column reflow should preserve total page content height")
        compare(currentSectionOrigin(), 634, "fixture ancestors should move the episode space to y=634")
        compare(page.episodeVirtualContentY, currentSectionOrigin(),
                "episodeVirtualContentY must follow the delayed ancestor/Column reflow")
    }

    function test_jumpUsesReflowedOrigin() {
        setSectionOffsetAsync(634)
        var expected = expectedScrollY(30)
        page.scrollToEpisodeIndex(30)
        var playY = episode30.mapToItem(scene, 0, 0).y
        console.log("JUMP_ORIGIN variant=" + page.variantName
                    + " expectedOrigin=" + currentSectionOrigin()
                    + " boundOrigin=" + page.episodeVirtualContentY
                    + " expectedContentY=" + expected
                    + " jumpContentY=" + flick.contentY
                    + " playY=" + playY
                    + " stickyHeader=" + page.stickyHeaderHeight
                    + " viewportHeight=" + scene.height)
        compare(flick.contentY, expected,
                "scrollToEpisodeIndex must use the current virtual content origin")
    }

    function test_playControlStaysVisibleAfterJump() {
        setSectionOffsetAsync(634)
        page.scrollToEpisodeIndex(30)
        var playY = episode30.mapToItem(scene, 0, 0).y
        console.log("JUMP_PLAY variant=" + page.variantName
                    + " expectedOrigin=" + currentSectionOrigin()
                    + " boundOrigin=" + page.episodeVirtualContentY
                    + " jumpContentY=" + flick.contentY
                    + " playY=" + playY
                    + " stickyHeader=" + page.stickyHeaderHeight
                    + " viewportHeight=" + scene.height)
        verify(playY >= page.stickyHeaderHeight,
               "episode 30 play control should land below sticky header")
        verify(playY + episode30.height <= scene.height,
               "episode 30 play control should fit inside the viewport")
    }

    function test_sectionAndHeaderChangeTogether() {
        setSectionOffsetAsync(634)
        stickyHeaderHeight = 320
        setSectionOffsetAsync(710)
        compare(currentSectionOrigin(), 710, "second reflow should move the section to y=710")
        compare(page.stickyHeaderHeight, 320, "header height should reflect the changed chrome geometry")
        compare(page.episodeVirtualContentY, currentSectionOrigin(),
                "episode origin must follow reflow even when total content height is unchanged")
    }

    function test_jumpUsesChangedHeaderAndSection() {
        setSectionOffsetAsync(634)
        stickyHeaderHeight = 320
        setSectionOffsetAsync(710)
        var expected = expectedScrollY(30)
        page.scrollToEpisodeIndex(30)
        var playY = episode30.mapToItem(scene, 0, 0).y
        console.log("JUMP_ORIGIN_HEADER variant=" + page.variantName
                    + " expectedOrigin=" + currentSectionOrigin()
                    + " boundOrigin=" + page.episodeVirtualContentY
                    + " expectedContentY=" + expected
                    + " jumpContentY=" + flick.contentY
                    + " playY=" + playY
                    + " stickyHeader=" + page.stickyHeaderHeight
                    + " viewportHeight=" + scene.height)
        compare(flick.contentY, expected,
                "jump target must combine the new section origin and current sticky header")
    }

    function test_scrollClampsRemainEffective() {
        page.scrollToEpisodeIndex(0)
        compare(flick.contentY, 0, "negative top target should clamp to zero")
        page.scrollToEpisodeIndex(-1)
        compare(flick.contentY, 0, "negative index should leave scroll unchanged")

        setSectionOffsetAsync(634)
        page.scrollToEpisodeIndex(999)
        compare(flick.contentY, flick.contentHeight - flick.height,
                "oversized target should clamp to maximum content scroll")
    }
}
"@

$manifestEntries = @()
foreach ($sourceEntry in $sourceFiles) {
    $source = Get-Content -Raw -LiteralPath $sourceEntry.Path
    $origin = Get-QmlExpression $source '(?m)^\s*readonly\s+property\s+real\s+episodeVirtualContentY\s*:\s*(?<expr>[^\r\n]+)' 'episodeVirtualContentY expression'
    $compact = Get-QmlExpression $source '(?m)^\s*readonly\s+property\s+int\s+compactEpisodeRowHeight\s*:\s*(?<expr>[^\r\n]+)' 'compactEpisodeRowHeight expression'
    $nextUp = Get-QmlExpression $source '(?m)^\s*readonly\s+property\s+int\s+nextUpEpisodeRowHeight\s*:\s*(?<expr>[^\r\n]+)' 'nextUpEpisodeRowHeight expression'
    $offsetFunction = Get-QmlFunction $source 'episodeOffsetForIndex'
    $scrollFunction = Get-QmlFunction $source 'scrollToEpisodeIndex'

    $qml = $template.Replace('__NAME__', "JumpOrigin_$($sourceEntry.Name)")
    $qml = $qml.Replace('__VARIANT__', $sourceEntry.Name)
    $qml = $qml.Replace('__COMPACT_ROW__', $compact)
    $qml = $qml.Replace('__NEXT_UP_ROW__', $nextUp)
    $qml = $qml.Replace('__EPISODE_ORIGIN__', $origin)
    $qml = $qml.Replace('__OFFSET_FUNCTION__', $offsetFunction)
    $qml = $qml.Replace('__SCROLL_FUNCTION__', $scrollFunction)

    $fixturePath = Join-Path $fixtureDir ("tst_" + $sourceEntry.Name + ".qml")
    Set-Content -LiteralPath $fixturePath -Value $qml -Encoding utf8
    $manifestEntries += [pscustomobject]@{
        variant = $sourceEntry.Name
        sourceFile = $sourceEntry.Path
        sourceSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $sourceEntry.Path).Hash
        generatedFixture = $fixturePath
        episodeVirtualContentYExpression = $origin
        initialFixtureStickyHeaderHeight = 304
        changedFixtureStickyHeaderHeight = 320
        compactEpisodeRowHeightExpression = $compact
        nextUpEpisodeRowHeightExpression = $nextUp
        episodeOffsetForIndexBody = $offsetFunction
        scrollToEpisodeIndexBody = $scrollFunction
    }
}

$gitHead = (& git -C $repo rev-parse HEAD).Trim()
$sourceManifest = [pscustomobject]@{
    runMode = 'source-derived-runtime'
    generatedAtLocal = (Get-Date).ToString('o')
    repositoryHead = $gitHead
    host = $env:COMPUTERNAME
    qtBinaryDirectory = $qtBin
    qmltestrunner = $runner
    fixtures = $manifestEntries
}
$sourceManifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $EvidenceDir 'extraction-manifest.json') -Encoding utf8

$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QUICK_BACKEND = 'software'
$env:PATH = "$qtBin;$env:PATH"
$runnerLog = Join-Path $EvidenceDir 'qmltestrunner.log'
$lines = & $runner -input $fixtureDir -o "$runnerLog,txt" -v1 2>&1
$testExit = $LASTEXITCODE
if ($lines) { $lines | Add-Content -LiteralPath $runnerLog -Encoding utf8 }
"process_exit=$testExit" | Set-Content -LiteralPath (Join-Path $EvidenceDir 'qmltestrunner.exit') -Encoding ascii
Get-Content -LiteralPath $runnerLog
"EVIDENCE_DIR=$EvidenceDir"
"QMLTESTRUNNER_EXIT=$testExit"
exit $testExit
