; Windows installer for Inkleaf (NSIS). CI runs it on the deployed app folder:
;   makensis /DVERSION=0.8.0 /DSOURCE=dist\Inkleaf /DOUTFILE=Inkleaf_0.8.0_windows_x64_setup.exe inkleaf.nsi
; Installs for the current user (no admin rights), into the folder the
; earlier Inkleaf installer used, so it replaces that install. Notebooks live
; elsewhere (%APPDATA%\app.inkleaf.desktop) and are never touched.
Unicode true
ManifestDPIAware true
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "FileFunc.nsh"

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!define NAME "Inkleaf"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${NAME}"

Name "${NAME}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\${NAME}"
InstallDirRegKey HKCU "Software\${NAME}" "InstallDir"
RequestExecutionLevel user
BrandingText "${NAME} ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${NAME}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${NAME} installer"
VIAddVersionKey "LegalCopyright" "Inkleaf contributors"

!define MUI_ICON "..\inkleaf.ico"
!define MUI_UNICON "..\inkleaf.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\Inkleaf.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Open Inkleaf"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; Updating from inside the app runs this silently with /RELAUNCH: Inkleaf
; opens again once the new version is in place.
Function .onInstSuccess
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/RELAUNCH" $1
  IfErrors +2 0
    Exec '"$INSTDIR\Inkleaf.exe"'
FunctionEnd

Section "Inkleaf"
  ; A running Inkleaf would keep its files locked.
  nsExec::Exec 'taskkill /IM Inkleaf.exe /F'
  Sleep 300
  ; What an earlier version left (Qt files change between versions).
  RMDir /r "$INSTDIR\qml"
  RMDir /r "$INSTDIR\plugins"
  SetOutPath "$INSTDIR"
  File /r "${SOURCE}\*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  CreateShortcut "$SMPROGRAMS\${NAME}.lnk" "$INSTDIR\Inkleaf.exe"
  CreateShortcut "$DESKTOP\${NAME}.lnk" "$INSTDIR\Inkleaf.exe"

  WriteRegStr HKCU "Software\${NAME}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${NAME}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "Inkleaf"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\Inkleaf.exe"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  nsExec::Exec 'taskkill /IM Inkleaf.exe /F'
  Sleep 300
  Delete "$SMPROGRAMS\${NAME}.lnk"
  Delete "$DESKTOP\${NAME}.lnk"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  DeleteRegKey HKCU "Software\${NAME}"
SectionEnd
