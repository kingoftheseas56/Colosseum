; colosseum.nsi — the Colosseum Windows installer (NSIS / MUI2).
;
; Builds Colosseum-<version>-setup.exe from a staged package tree (git archive of the
; release tag + the windeployqt'd runtime under native\build-msvc). Per-user install —
; NO admin prompt — into %LOCALAPPDATA%\Colosseum, because the app writes beside itself
; (data\ catalogs, download index, disk cache): Program Files would break it.
;
; Compile:
;   makensis /DSTAGE=<staged Colosseum dir> /DVERSION=0.1 /DOUTFILE=<path\setup.exe> colosseum.nsi
;
; The desktop + start-menu shortcuts point at the exe itself — argless it self-locates
; the tree and (in a git clone) self-updates; from an installed copy it just boots.

!ifndef STAGE
  !error "pass /DSTAGE=<staged Colosseum dir>"
!endif
!ifndef VERSION
  !define VERSION "0.1"
!endif
!ifndef OUTFILE
  !define OUTFILE "Colosseum-${VERSION}-setup.exe"
!endif

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"

!ifdef COLOSSEUM_UPDATE_TEST_ROOT
  !define COLOSSEUM_INSTALL_ROOT "${COLOSSEUM_UPDATE_TEST_ROOT}\Programs\Colosseum"
!else
  !define COLOSSEUM_INSTALL_ROOT "$LOCALAPPDATA\Programs\Colosseum"
!endif
!ifdef COLOSSEUM_UPDATE_TEST_REGKEY
  !define COLOSSEUM_REGKEY "${COLOSSEUM_UPDATE_TEST_REGKEY}"
!else
  !define COLOSSEUM_REGKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Colosseum"
!endif
!ifdef COLOSSEUM_UPDATE_TEST_SHORTCUT_PREFIX
  !define COLOSSEUM_DESKTOP_LINK "$DESKTOP\${COLOSSEUM_UPDATE_TEST_SHORTCUT_PREFIX}.lnk"
  !define COLOSSEUM_START_LINK "$SMPROGRAMS\${COLOSSEUM_UPDATE_TEST_SHORTCUT_PREFIX}.lnk"
!else
  !define COLOSSEUM_DESKTOP_LINK "$DESKTOP\Colosseum.lnk"
  !define COLOSSEUM_START_LINK "$SMPROGRAMS\Colosseum.lnk"
!endif

Name "Colosseum ${VERSION}"
OutFile "${OUTFILE}"
Unicode true
RequestExecutionLevel user
; Programs\Colosseum, NOT $LOCALAPPDATA\Colosseum — that exact folder is the app's own
; Qt data dir (cover cache lives there); installing into it mixes payload with user data
; and the uninstaller would wipe the cache. Found the hard way testing 0.1.
InstallDir "${COLOSSEUM_INSTALL_ROOT}"
SetCompressor /SOLID lzma

!define MUI_WELCOMEPAGE_TITLE "Colosseum ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "One app for your movies and shows, manga and comics, and books.$\r$\n$\r$\nThis installs Colosseum for your user account only — no administrator needed."
!define MUI_FINISHPAGE_RUN "$INSTDIR\native\build-msvc\colosseum.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch Colosseum"
!define MUI_UNCONFIRMPAGE_TEXT_TOP "This removes Colosseum AND everything in its folder — including downloaded media and reading progress."

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Var UpdateMode
Var UpdateParams
Var UpdateFlag
Var UpdateWaitPid
Var UpdateTarget
Var UpdateRestart
Var UpdateLog
Var UpdateNew
Var UpdateOld
Var UpdateParent
Var UpdateHandle
Var UpdateWaitResult
Var UpdateError
Var UpdateLogHandle
Var UpdateLaunchHandle

Function .onInit
  StrCpy $UpdateMode "0"
  ${GetParameters} $UpdateParams
  ClearErrors
  ${GetOptions} "$UpdateParams" "/UPDATE=" $UpdateFlag
  ${IfNot} ${Errors}
    ${If} $UpdateFlag == "1"
      StrCpy $UpdateMode "1"
      StrCpy $UpdateLog "$LOCALAPPDATA\Colosseum-update.log"
      ClearErrors
      ${GetOptions} "$UpdateParams" "/WAITPID=" $UpdateWaitPid
      ${If} ${Errors}
        StrCpy $UpdateError "missing_wait_pid"
        Call UpdateAbort
      ${EndIf}
      ClearErrors
      ${GetOptions} "$UpdateParams" "/TARGETVERSION=" $UpdateTarget
      ${If} ${Errors}
        StrCpy $UpdateError "missing_target_version"
        Call UpdateAbort
      ${EndIf}
      ClearErrors
      ${GetOptions} "$UpdateParams" "/RESTART=" $UpdateRestart
      ${If} ${Errors}
        StrCpy $UpdateRestart "0"
      ${EndIf}
      ClearErrors
      ${GetOptions} "$UpdateParams" "/LOG=" $UpdateLog
      SetSilent silent
    ${EndIf}
  ${EndIf}
FunctionEnd

Section "Colosseum"
  ${If} $UpdateMode == "1"
    Call UpdateInstall
  ${Else}
    SetOutPath "$INSTDIR"
  ${EndIf}
  File /r "${STAGE}\*.*"

  ${If} $UpdateMode == "1"
    Call UpdateFinish
  ${Else}
    ; shortcuts — straight to the exe (it anchors its own working directory)
    CreateShortCut "${COLOSSEUM_DESKTOP_LINK}" "$INSTDIR\native\build-msvc\colosseum.exe" "" "" 0
    CreateShortCut "${COLOSSEUM_START_LINK}" "$INSTDIR\native\build-msvc\colosseum.exe" "" "" 0

    ; uninstaller + Apps-list entry (per-user hive)
    WriteUninstaller "$INSTDIR\uninstall.exe"
    WriteRegStr HKCU "${COLOSSEUM_REGKEY}" \
                   "DisplayName" "Colosseum"
    WriteRegStr HKCU "${COLOSSEUM_REGKEY}" \
                   "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "${COLOSSEUM_REGKEY}" \
                   "Publisher" "Colosseum"
    WriteRegStr HKCU "${COLOSSEUM_REGKEY}" \
                   "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
    ; QuietUninstallString: an NSIS uninstaller removes silently with /S, but a
    ; package manager only invokes that when the ARP entry advertises it here.
    ; Without this, `winget uninstall Colosseum.Colosseum` pops the interactive
    ; uninstaller instead of removing quietly. Same quoted exe, plus /S.
    WriteRegStr HKCU "${COLOSSEUM_REGKEY}" \
                   "QuietUninstallString" "$\"$INSTDIR\uninstall.exe$\" /S"
    WriteRegStr HKCU "${COLOSSEUM_REGKEY}" \
                   "InstallLocation" "$INSTDIR"
  ${EndIf}
SectionEnd

Function UpdateFinish
  ; File extraction changes the process current directory to the new sibling;
  ; leave it before any rename so Windows does not reject the swap.
  SetOutPath "$TEMP"
  IfFileExists "$UpdateNew\native\build-msvc\colosseum.exe" 0 update_extract_failed
  Push "VALIDATED $UpdateNew\native\build-msvc\colosseum.exe"
  Call UpdateLogLine

  Push "$UpdateOld"
  Call UpdateCheckPath
  ClearErrors
!ifdef COLOSSEUM_UPDATE_TEST_RENAME_FAIL
  ; Deterministic matrix hook: exercise restoration without relying on a
  ; timing-sensitive Windows file lock.
  Rename "$INSTDIR" "$UpdateOld"
  SetErrors
!else
  Rename "$INSTDIR" "$UpdateOld"
!endif
  IfErrors update_swap_failed
  ClearErrors
  Rename "$UpdateNew" "$INSTDIR"
  IfErrors update_swap_failed_after_old

  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKCU "${COLOSSEUM_REGKEY}" "DisplayVersion" "$UpdateTarget"
  ; keep QuietUninstallString fresh so a self-update also heals silent uninstall
  WriteRegStr HKCU "${COLOSSEUM_REGKEY}" "QuietUninstallString" "$\"$INSTDIR\uninstall.exe$\" /S"
  WriteRegStr HKCU "${COLOSSEUM_REGKEY}" "InstallLocation" "$INSTDIR"
  CreateShortCut "${COLOSSEUM_DESKTOP_LINK}" "$INSTDIR\native\build-msvc\colosseum.exe" "" "" 0
  CreateShortCut "${COLOSSEUM_START_LINK}" "$INSTDIR\native\build-msvc\colosseum.exe" "" "" 0
  Push "SWAP success old=$UpdateOld"
  Call UpdateLogLine
  ${If} $UpdateRestart == "1"
    Push "RELAUNCH --update-result=success --update-from=$UpdateOld --update-backup=$UpdateOld"
    Call UpdateLogLine
!ifdef COLOSSEUM_UPDATE_TEST_MODE
    FileOpen $UpdateLaunchHandle "$UpdateLog.launch" a
    FileWrite $UpdateLaunchHandle "--update-result=success --update-from=$UpdateOld --update-backup=$UpdateOld$\r$\n"
    FileClose $UpdateLaunchHandle
!else
    Exec '"$INSTDIR\native\build-msvc\colosseum.exe" --update-result=success --update-from="$UpdateOld" --update-backup="$UpdateOld"'
!endif
  ${EndIf}
  ${If} $UpdateLogHandle != ""
    FileClose $UpdateLogHandle
  ${EndIf}
  SetErrorLevel 0
  Return

update_extract_failed:
  StrCpy $UpdateError "payload_validation_failed"
  Call UpdateAbort
update_swap_failed_after_old:
  RMDir /r "$INSTDIR"
update_swap_failed:
  ClearErrors
  Rename "$UpdateOld" "$INSTDIR"
  RMDir /r "$UpdateNew"
  StrCpy $UpdateError "swap_failed_rollback"
  Push "RELAUNCH --update-result=rollback --update-target=$UpdateNew"
  Call UpdateLogLine
!ifdef COLOSSEUM_UPDATE_TEST_MODE
  FileOpen $UpdateLaunchHandle "$UpdateLog.launch" a
  FileWrite $UpdateLaunchHandle "--update-result=rollback --update-target=$UpdateNew$\r$\n"
  FileClose $UpdateLaunchHandle
!else
  Exec '"$INSTDIR\native\build-msvc\colosseum.exe" --update-result=rollback --update-target="$UpdateNew"'
!endif
  Call UpdateAbort
FunctionEnd

Function UpdateLogLine
  Exch $0
  ${If} $UpdateLogHandle == ""
    FileOpen $UpdateLogHandle "$UpdateLog" a
  ${EndIf}
  ${If} $UpdateLogHandle != ""
    FileWrite $UpdateLogHandle "$0$\r$\n"
  ${EndIf}
  Pop $0
FunctionEnd

Function UpdateAbort
  ${If} $UpdateError == ""
    StrCpy $UpdateError "update_failed"
  ${EndIf}
  Push "ERROR $UpdateError"
  Call UpdateLogLine
  ${If} $UpdateLogHandle != ""
    FileClose $UpdateLogHandle
  ${EndIf}
  SetErrorLevel 1
  Quit
FunctionEnd

Function UpdateCheckPath
  ; Reparse points are never traversed by the updater.  The two sibling names
  ; are fixed below, so a junction cannot redirect extraction or cleanup.
  Exch $0
  System::Call 'kernel32::GetFileAttributes(t r0) i .r1'
  ${If} $1 != -1
    IntOp $1 $1 & 0x400
    ${If} $1 != 0
      StrCpy $UpdateError "unexpected_reparse_point"
      Pop $0
      Call UpdateAbort
    ${EndIf}
  ${EndIf}
  Pop $0
FunctionEnd

Function UpdateWaitForProcess
  ${If} $UpdateWaitPid == "0"
    Return
  ${EndIf}
  System::Call 'kernel32::OpenProcess(i 0x00100000, i 0, i rUpdateWaitPid) p .rUpdateHandle'
  ${If} $UpdateHandle P<> 0
    System::Call 'kernel32::WaitForSingleObject(p rUpdateHandle, i 120000) i .rUpdateWaitResult'
    System::Call 'kernel32::CloseHandle(p rUpdateHandle)'
    ${If} $UpdateWaitResult != 0
      StrCpy $UpdateError "wait_timeout"
      Call UpdateAbort
    ${EndIf}
  ${EndIf}
FunctionEnd

Function UpdateInstall
  ${GetParent} "$INSTDIR" $UpdateParent
  StrCpy $UpdateNew "$UpdateParent\Colosseum.__update-new"
  StrCpy $UpdateOld "$UpdateParent\Colosseum.__update-old"
  StrCpy $UpdateLogHandle ""
  Push "BEGIN target=$UpdateTarget waitpid=$UpdateWaitPid"
  Call UpdateLogLine
  Call UpdateWaitForProcess

  IfFileExists "$UpdateOld\." 0 +3
    StrCpy $UpdateError "existing_update_backup"
    Call UpdateAbort
  IfFileExists "$UpdateNew\." 0 +4
    Push "$UpdateNew"
    Call UpdateCheckPath
    RMDir /r "$UpdateNew"

  ; The Section's File instruction now lands in this new sibling while N is
  ; still untouched. Validation therefore cannot destroy a working install.
  SetOutPath "$UpdateNew"
FunctionEnd

Section "Uninstall"
  ${If} $UpdateMode == "1"
    Abort
  ${EndIf}
  ; NOTE: the library (downloads, reading progress, catalogs) lives inside the install
  ; folder in 0.1 — uninstalling removes it all. The confirm page says as much.
  RMDir /r "$INSTDIR"
  Delete "${COLOSSEUM_DESKTOP_LINK}"
  Delete "${COLOSSEUM_START_LINK}"
  DeleteRegKey HKCU "${COLOSSEUM_REGKEY}"
SectionEnd
