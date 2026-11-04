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
; Test installers must never touch the real per-user protocol registration.
!ifdef COLOSSEUM_UPDATE_TEST_PROTOCOL_REGKEY
  !define COLOSSEUM_PROTOCOL_REGKEY "${COLOSSEUM_UPDATE_TEST_PROTOCOL_REGKEY}"
!else
  !ifdef COLOSSEUM_UPDATE_TEST_ROOT
    !error "test installers require COLOSSEUM_UPDATE_TEST_PROTOCOL_REGKEY"
  !endif
  !ifdef COLOSSEUM_UPDATE_TEST_REGKEY
    !error "test installers require COLOSSEUM_UPDATE_TEST_PROTOCOL_REGKEY"
  !endif
  !ifdef COLOSSEUM_UPDATE_TEST_MODE
    !error "test installers require COLOSSEUM_UPDATE_TEST_PROTOCOL_REGKEY"
  !endif
  !ifdef COLOSSEUM_UPDATE_TEST_SHORTCUT_PREFIX
    !error "test installers require COLOSSEUM_UPDATE_TEST_PROTOCOL_REGKEY"
  !endif
  !ifdef COLOSSEUM_UPDATE_TEST_RENAME_FAIL
    !error "test installers require COLOSSEUM_UPDATE_TEST_PROTOCOL_REGKEY"
  !endif
  !define COLOSSEUM_PROTOCOL_REGKEY "Software\Classes\colosseum"
!endif
!define COLOSSEUM_PROTOCOL_OWNER "Colosseum.CollectionLink.v1"

!ifdef COLOSSEUM_UPDATE_TEST_SHORTCUT_PREFIX
  !define COLOSSEUM_DESKTOP_LINK "$DESKTOP\${COLOSSEUM_UPDATE_TEST_SHORTCUT_PREFIX}.lnk"
  !define COLOSSEUM_START_LINK "$SMPROGRAMS\${COLOSSEUM_UPDATE_TEST_SHORTCUT_PREFIX}.lnk"
!else
  !define COLOSSEUM_DESKTOP_LINK "$DESKTOP\Colosseum.lnk"
  !define COLOSSEUM_START_LINK "$SMPROGRAMS\Colosseum.lnk"
!endif

Name "Colosseum"
Caption "Colosseum"
UninstallCaption "Colosseum"
OutFile "${OUTFILE}"
Unicode true
RequestExecutionLevel user
; Programs\Colosseum, NOT $LOCALAPPDATA\Colosseum — that exact folder is the app's own
; Qt data dir (cover cache lives there); installing into it mixes payload with user data
; and the uninstaller would wipe the cache. Found the hard way testing 0.1.
InstallDir "${COLOSSEUM_INSTALL_ROOT}"
SetCompressor /SOLID lzma
; Keep the full Qt runtime and atlas artwork within the release size gate.
SetCompressorDictSize 64

!define MUI_ICON "${STAGE}\assets\icons\colosseum.ico"
!define MUI_UNICON "${STAGE}\assets\icons\colosseum.ico"
!define MUI_WELCOMEPAGE_TITLE "Colosseum"
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
Var ProtocolConflict
Var ProtocolOwner
Var ProtocolRoot
Var ProtocolCommand
Var ProtocolRegisteredRoot
Var ProtocolKeyHandle
Var ProtocolOpenResult
Var ProtocolPreviousRoot
Var ProtocolPreviousCommand
Var ProtocolDescription
Var ProtocolUrlMarker

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

; An empty key also counts as an existing registration. Names alone never
; establish ownership: require our marker, ARP root and exact launch command.
Function CheckCollectionProtocol
  StrCpy $ProtocolConflict "1"
  StrCpy $ProtocolRoot ""
  StrCpy $ProtocolCommand ""
  System::Call 'advapi32::RegOpenKeyEx(p 0x80000001, t "${COLOSSEUM_PROTOCOL_REGKEY}", i 0, i 0x20019, *p .rProtocolKeyHandle) i .rProtocolOpenResult'
  ${If} $ProtocolOpenResult == 2
    ; HKCR falls back to HKLM when the per-user key is absent. Do not shadow
    ; an existing machine registration with our new per-user registration.
    System::Call 'advapi32::RegOpenKeyEx(p 0x80000002, t "${COLOSSEUM_PROTOCOL_REGKEY}", i 0, i 0x20019, *p .rProtocolKeyHandle) i .rProtocolOpenResult'
    ${If} $ProtocolOpenResult == 0
      System::Call 'advapi32::RegCloseKey(p rProtocolKeyHandle)'
    ${ElseIf} $ProtocolOpenResult == 2
      StrCpy $ProtocolConflict "0"
    ${EndIf}
    Return
  ${EndIf}
  ${If} $ProtocolOpenResult != 0
    Return
  ${EndIf}
  System::Call 'advapi32::RegCloseKey(p rProtocolKeyHandle)'
  ReadRegStr $ProtocolOwner HKCU "${COLOSSEUM_PROTOCOL_REGKEY}" "ColosseumOwner"
  ReadRegStr $ProtocolRoot HKCU "${COLOSSEUM_PROTOCOL_REGKEY}" "ColosseumInstallRoot"
  ReadRegStr $ProtocolCommand HKCU "${COLOSSEUM_PROTOCOL_REGKEY}\shell\open\command" ""
  ReadRegStr $ProtocolRegisteredRoot HKCU "${COLOSSEUM_REGKEY}" "InstallLocation"
  ReadRegStr $ProtocolDescription HKCU "${COLOSSEUM_PROTOCOL_REGKEY}" ""
  ClearErrors
  ReadRegStr $ProtocolUrlMarker HKCU "${COLOSSEUM_PROTOCOL_REGKEY}" "URL Protocol"
  ${If} ${Errors}
    Return
  ${EndIf}
  ${If} $ProtocolOwner == "${COLOSSEUM_PROTOCOL_OWNER}"
  ${AndIf} $ProtocolRoot != ""
  ${AndIf} $ProtocolRoot == $ProtocolRegisteredRoot
  ${AndIf} $ProtocolDescription == "URL:Colosseum Collection"
  ${AndIf} $ProtocolUrlMarker == ""
  ${AndIf} $ProtocolCommand == '$\"$ProtocolRoot\native\build-msvc\colosseum.exe$\" --collection-url $\"%1$\"'
    StrCpy $ProtocolConflict "0"
  ${EndIf}
FunctionEnd

Function RequireCollectionProtocol
  Call CheckCollectionProtocol
  ${If} $ProtocolConflict != "0"
    ${If} $UpdateMode == "1"
      StrCpy $UpdateError "collection_protocol_conflict"
      Call UpdateAbort
    ${Else}
      SetErrorLevel 1
      Abort "The colosseum link handler is owned by another installation or application. Resolve that registration before installing Colosseum."
    ${EndIf}
  ${EndIf}
FunctionEnd

Function RegisterCollectionProtocol
  ; Final payload path only. The previous ARP InstallLocation still exists
  ; here, so manual relocation does not invalidate its own ownership guard.
  Call CheckCollectionProtocol
  ${If} $ProtocolConflict != "0"
    StrCpy $UpdateError "collection_protocol_conflict"
    SetErrors
    Return
  ${EndIf}
  StrCpy $ProtocolPreviousRoot $ProtocolRoot
  StrCpy $ProtocolPreviousCommand $ProtocolCommand
  ClearErrors
  ${If} $ProtocolPreviousRoot == ""
    WriteRegStr HKCU "${COLOSSEUM_PROTOCOL_REGKEY}" "" "URL:Colosseum Collection"
    WriteRegStr HKCU "${COLOSSEUM_PROTOCOL_REGKEY}" "URL Protocol" ""
    WriteRegStr HKCU "${COLOSSEUM_PROTOCOL_REGKEY}" "ColosseumOwner" "${COLOSSEUM_PROTOCOL_OWNER}"
  ${EndIf}
  WriteRegStr HKCU "${COLOSSEUM_PROTOCOL_REGKEY}" "ColosseumInstallRoot" "$INSTDIR"
  WriteRegStr HKCU "${COLOSSEUM_PROTOCOL_REGKEY}\shell\open\command" "" '$\"$INSTDIR\native\build-msvc\colosseum.exe$\" --collection-url $\"%1$\"'
  ${If} ${Errors}
    ; The checked existing registration already has these exact fixed values;
    ; only its root and command can change. Leave all other values untouched.
    ; Do not undo a different process's replacement while recovering our own
    ; failed write. A partial new key without our marker is not safe to delete.
    ReadRegStr $ProtocolOwner HKCU "${COLOSSEUM_PROTOCOL_REGKEY}" "ColosseumOwner"
    ReadRegStr $ProtocolRoot HKCU "${COLOSSEUM_PROTOCOL_REGKEY}" "ColosseumInstallRoot"
    ReadRegStr $ProtocolCommand HKCU "${COLOSSEUM_PROTOCOL_REGKEY}\shell\open\command" ""
    ${If} $ProtocolOwner != "${COLOSSEUM_PROTOCOL_OWNER}"
      StrCpy $UpdateError "collection_protocol_restore_conflict"
      SetErrors
      Return
    ${EndIf}
    ${If} $ProtocolRoot != $ProtocolPreviousRoot
    ${AndIf} $ProtocolRoot != $INSTDIR
      StrCpy $UpdateError "collection_protocol_restore_conflict"
      SetErrors
      Return
    ${EndIf}
    ${If} $ProtocolCommand != $ProtocolPreviousCommand
    ${AndIf} $ProtocolCommand != '$\"$INSTDIR\native\build-msvc\colosseum.exe$\" --collection-url $\"%1$\"'
      StrCpy $UpdateError "collection_protocol_restore_conflict"
      SetErrors
      Return
    ${EndIf}
    ClearErrors
    ${If} $ProtocolPreviousRoot == ""
      DeleteRegKey HKCU "${COLOSSEUM_PROTOCOL_REGKEY}"
    ${Else}
      WriteRegStr HKCU "${COLOSSEUM_PROTOCOL_REGKEY}" "ColosseumInstallRoot" "$ProtocolPreviousRoot"
      WriteRegStr HKCU "${COLOSSEUM_PROTOCOL_REGKEY}\shell\open\command" "" "$ProtocolPreviousCommand"
    ${EndIf}
    ${If} ${Errors}
      StrCpy $UpdateError "collection_protocol_restore_failed"
    ${Else}
      StrCpy $UpdateError "collection_protocol_write_failed"
    ${EndIf}
    SetErrors
    Return
  ${EndIf}
  ClearErrors
FunctionEnd

Function un.RemoveCollectionProtocol
  ReadRegStr $ProtocolOwner HKCU "${COLOSSEUM_PROTOCOL_REGKEY}" "ColosseumOwner"
  ReadRegStr $ProtocolRoot HKCU "${COLOSSEUM_PROTOCOL_REGKEY}" "ColosseumInstallRoot"
  ReadRegStr $ProtocolCommand HKCU "${COLOSSEUM_PROTOCOL_REGKEY}\shell\open\command" ""
  ${If} $ProtocolOwner == "${COLOSSEUM_PROTOCOL_OWNER}"
  ${AndIf} $ProtocolRoot == $INSTDIR
  ${AndIf} $ProtocolCommand == '$\"$INSTDIR\native\build-msvc\colosseum.exe$\" --collection-url $\"%1$\"'
    DeleteRegKey HKCU "${COLOSSEUM_PROTOCOL_REGKEY}"
  ${EndIf}
FunctionEnd

Section "Colosseum"
  ; Fail before overlay cleanup, extraction, sibling cleanup or process wait.
  Call RequireCollectionProtocol
  ${If} $UpdateMode == "1"
    Call UpdateInstall
  ${Else}
    ; A manual migration overlays the existing payload. Refresh the managed
    ; QML tree in a registered installation so retired pages cannot invalidate
    ; the new executable's exact QML build fingerprint. User profiles live in
    ; AppData and are outside this payload directory.
    ReadRegStr $0 HKCU "${COLOSSEUM_REGKEY}" "InstallLocation"
    ${If} $0 == $INSTDIR
    ${AndIf} ${FileExists} "$INSTDIR\native\build-msvc\colosseum.exe"
    ${AndIf} ${FileExists} "$INSTDIR\qml\Main.qml"
      ClearErrors
      RMDir /r "$INSTDIR\qml"
      ${If} ${Errors}
        Abort "Close Colosseum and run this installer again to replace its interface files."
      ${EndIf}
    ${EndIf}
    SetOutPath "$INSTDIR"
  ${EndIf}
  File /r "${STAGE}\*.*"

  ${If} $UpdateMode == "1"
    Call UpdateFinish
  ${Else}
    Call RegisterCollectionProtocol
    ${If} ${Errors}
      SetErrorLevel 1
      Abort "Colosseum's link handler could not be registered. Resolve the registration permissions or conflict and run the installer again."
    ${EndIf}
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

  ; No handler writes precede the swap. Every rename rollback therefore keeps
  ; the exact previous handler, including absence on pre-sharing releases.
  Call RequireCollectionProtocol
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

  Call RegisterCollectionProtocol
  ${If} ${Errors}
    Push "ERROR $UpdateError"
    Call UpdateLogLine
    Goto update_swap_failed_after_old
  ${EndIf}
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
  Call un.RemoveCollectionProtocol
  ; NOTE: the library (downloads, reading progress, catalogs) lives inside the install
  ; folder in 0.1 — uninstalling removes it all. The confirm page says as much.
  RMDir /r "$INSTDIR"
  Delete "${COLOSSEUM_DESKTOP_LINK}"
  Delete "${COLOSSEUM_START_LINK}"
  DeleteRegKey HKCU "${COLOSSEUM_REGKEY}"
SectionEnd
