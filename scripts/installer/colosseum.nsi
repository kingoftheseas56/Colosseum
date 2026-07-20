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

Name "Colosseum ${VERSION}"
OutFile "${OUTFILE}"
Unicode true
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Colosseum"
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

Section "Colosseum"
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*.*"

  ; shortcuts — straight to the exe (it anchors its own working directory)
  CreateShortCut "$DESKTOP\Colosseum.lnk" "$INSTDIR\native\build-msvc\colosseum.exe" "" "" 0
  CreateShortCut "$SMPROGRAMS\Colosseum.lnk" "$INSTDIR\native\build-msvc\colosseum.exe" "" "" 0

  ; uninstaller + Apps-list entry (per-user hive)
  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Colosseum" \
                   "DisplayName" "Colosseum"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Colosseum" \
                   "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Colosseum" \
                   "Publisher" "Colosseum"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Colosseum" \
                   "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Colosseum" \
                   "InstallLocation" "$INSTDIR"
SectionEnd

Section "Uninstall"
  ; NOTE: the library (downloads, reading progress, catalogs) lives inside the install
  ; folder in 0.1 — uninstalling removes it all. The confirm page says as much.
  RMDir /r "$INSTDIR"
  Delete "$DESKTOP\Colosseum.lnk"
  Delete "$SMPROGRAMS\Colosseum.lnk"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Colosseum"
SectionEnd
