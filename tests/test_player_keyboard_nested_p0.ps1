$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Need([string]$Rel, [string]$Needle, [string]$Why) {
    $path = Join-Path $root $Rel
    if (-not (Test-Path $path)) { throw "missing $Rel" }
    $text = Get-Content -Raw $path
    if (-not $text.Contains($Needle)) { throw "${Rel}: $Why" }
}
function Forbid([string]$Rel, [string]$Needle, [string]$Why) {
    $path = Join-Path $root $Rel
    if (-not (Test-Path $path)) { throw "missing $Rel" }
    $text = Get-Content -Raw $path
    if ($text.Contains($Needle)) { throw "${Rel}: $Why" }
}

# Player 1 global semantics + nested controls.
Need 'qml\PlayerHotkeys.js' 'm[K.E] = "E"' 'E must reach Episodes & sources.'
Need 'qml\PlayerHotkeys.js' 'm[K.Menu] = "Menu"' 'Menu must reach More controls.'
Need 'qml\PlayerHotkeys.js' 'K.F10' 'Shift+F10 must be represented.'
Need 'qml\PlayerPage.qml' 'case "contextMenu"' 'semantic context action missing.'
Need 'qml\PlayerPage.qml' 'KeyboardAction {' 'player chrome needs semantic keyboard buttons.'
Need 'qml\PlayerPage.qml' 'KeyboardCollectionController {' 'player collections need composite navigation.'
Need 'qml\PlayerPage.qml' 'focusPolicy: enabled ? Qt.TabFocus : Qt.NoFocus' 'seek slider must be a Tab stop.'
Need 'qml\AudioMenu.qml' 'KeyboardCollectionController {' 'audio tracks need arrow navigation.'
Need 'qml\AudioMenu.qml' 'focusReturnItem' 'audio menu must restore its invoker.'
Need 'qml\SubtitleMenu.qml' 'KeyboardCollectionController {' 'subtitle track/result lists need composite navigation.'
Need 'qml\SubtitleMenu.qml' 'movePanelFocus' 'subtitle menu must support arrow movement between local controls.'
Need 'qml\SubStyleBar.qml' 'Keys.onPressed' 'subtitle style bar must support arrows and Esc.'
Need 'qml\BrowserDrawer.qml' 'KeyboardCollectionController {' 'episode/source lists need composite navigation.'
Need 'qml\BrowserDrawer.qml' 'Qt.ControlModifier' 'source-copy must have a keyboard path.'
Need 'qml\ShortcutsSheet.qml' 'KeyboardScrollController {' 'help sheet must scroll from keyboard.'

# Player 2 semantics + nested controls.
Need 'qml\player2\Player2Shell.qml' 'Qt.Key_Menu' 'Player 2 needs Menu-key More controls.'
Need 'qml\player2\Player2Shell.qml' 'Qt.Key_F10' 'Player 2 needs Shift+F10 More controls.'
Need 'qml\player2\controls\Player2Shortcuts.js' '"Shift+F10"' 'shortcut sheet must document Shift+F10.'
Need 'qml\player2\controls\TopBar.qml' 'KeyboardAction {' 'title bar buttons need keyboard activation.'
Need 'qml\player2\controls\TransportBar.qml' 'KeyboardAction {' 'transport buttons need keyboard activation.'
Need 'qml\player2\controls\TransportBar.qml' 'Accessible.Slider' 'volume must expose slider semantics.'
Need 'qml\player2\controls\SeekBar.qml' 'Accessible.Slider' 'seek must expose slider semantics.'
Need 'qml\player2\controls\TrackMenu.qml' 'KeyboardCollectionController {' 'track lists need composite navigation.'
Need 'qml\player2\controls\SourceDrawer.qml' 'KeyboardCollectionController {' 'source rows need composite navigation.'
Need 'qml\player2\controls\EpisodeBrowser.qml' 'KeyboardCollectionController {' 'episode rows need composite navigation.'
Need 'qml\player2\controls\OverflowMenu.qml' 'Keys.onPressed' 'overflow menu needs arrows and Esc.'
Need 'qml\player2\controls\CloseConfirm.qml' 'KeyboardAction {' 'confirmation buttons need keyboard activation.'
Need 'qml\player2\controls\SkipButton.qml' 'KeyboardAction {' 'skip pill needs keyboard activation.'
Need 'qml\player2\controls\ShortcutsSheet.qml' 'Keys.onEscapePressed' 'shortcuts sheet must dismiss with Esc.'

# Arc 41 composite-region and modal-containment acceptance.
Need 'qml\PlayerFocusContainment.js' 'function move(windowObject, container, forward)' 'shared player modal focus containment helper missing.'
Need 'qml\BrowserDrawer.qml' 'id: tabStrip' 'browser tabs must be one composite region.'
Need 'qml\BrowserDrawer.qml' 'id: seasonStrip' 'browser seasons must be one composite region.'
Need 'qml\SubtitleMenu.qml' 'id: languageCollection' 'subtitle languages must be one composite region.'
Need 'qml\SubtitleMenu.qml' 'id: sourceStrip' 'subtitle source tabs must be one composite region.'
Need 'qml\SubStyleBar.qml' 'swatches.activeFocus' 'subtitle colour palette must be one composite region.'
Need 'qml\PlayerPage.qml' 'speedChoiceRepeater.activeFocus' 'Player 1 speed choices must be composite.'
Need 'qml\PlayerPage.qml' 'sleepChoiceRepeater.activeFocus' 'Player 1 sleep choices must be composite.'
Need 'qml\PlayerPage.qml' 'skipStepRepeater.activeFocus' 'Player 1 skip-step choices must be composite.'
Need 'qml\PlayerPage.qml' 'fillChoiceRepeater.activeFocus' 'Player 1 fill choices must be composite.'
Need 'qml\player2\controls\SourceDrawer.qml' 'id: tabStrip' 'Player 2 drawer tabs must be composite.'
Need 'qml\player2\controls\TransportBar.qml' 'fillMenu.activeFocus' 'Player 2 fill choices must be composite.'
Need 'qml\player2\controls\TransportBar.qml' 'speedMenu.activeFocus' 'Player 2 speed choices must be composite.'
Forbid 'qml\BrowserDrawer.qml' 'accessibleName: tabPill.modelData' 'browser tab delegates must not each be Tab stops.'
Forbid 'qml\player2\controls\SourceDrawer.qml' 'accessibleName: tabPill.modelData' 'Player 2 tab delegates must not each be Tab stops.'

$contained = @(
    'qml\AudioMenu.qml','qml\SubtitleMenu.qml','qml\SubStyleBar.qml','qml\BrowserDrawer.qml','qml\ShortcutsSheet.qml',
    'qml\player2\controls\SourceDrawer.qml','qml\player2\controls\TrackMenu.qml','qml\player2\controls\OverflowMenu.qml',
    'qml\player2\controls\ShortcutsSheet.qml','qml\player2\controls\CloseConfirm.qml'
)
foreach ($rel in $contained) {
    Need $rel 'Keys.onTabPressed' 'open modal must contain forward Tab traversal.'
    Need $rel 'Keys.onBacktabPressed' 'open modal must contain Shift+Tab traversal.'
    Need $rel 'FocusContainment.move' 'modal containment must not depend on implicit focus-chain leakage.'
}
Need 'qml\PlayerPage.qml' 'FocusContainment.move(root.Window.window, overflowPanel' 'Player 1 overflow must contain Tab traversal.'
Need 'qml\PlayerPage.qml' 'FocusContainment.move(root.Window.window, closeConfirmPanel' 'Player 1 confirmation must contain Tab traversal.'
Need 'qml\PlayerPage.qml' 'FocusContainment.move(root.Window.window, liveGuide' 'Player 1 live guide must contain Tab traversal.'
Need 'qml\PlayerPage.qml' 'FocusContainment.move(root.Window.window, dvrPanel' 'Player 1 DVR must contain Tab traversal.'

Write-Host 'PLAYER_KEYBOARD_NESTED_OK'

