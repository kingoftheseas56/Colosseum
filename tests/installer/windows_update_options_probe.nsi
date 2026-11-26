Unicode true
RequestExecutionLevel user
SilentInstall silent
OutFile "${OUTFILE}"
!include "FileFunc.nsh"

Section
  StrCpy $0 '/UPDATE=1 /WAITPID=5160 /TARGETVERSION=1.5.1 /RESTART=1 /LOG=C:/Users/runneradmin/AppData/Local/Colosseum/updates/update-1.5.1.log'
  ${GetOptions} "$0" '/LOG=' $1
  StrCpy $0 '/UPDATE=1 /WAITPID=5160 /TARGETVERSION=1.5.1 /RESTART=1 /LOG=C:\Users\runneradmin\AppData\Local\Colosseum\updates\update-1.5.1.log'
  ${GetOptions} "$0" '/LOG=' $2
  FileOpen $3 '${REPORT}' w
  FileWrite $3 'forward-slash parse=<$1>$\r$\n'
  FileWrite $3 'native-backslash parse=<$2>$\r$\n'
  FileClose $3
SectionEnd
