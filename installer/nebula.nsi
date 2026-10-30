Unicode true
ManifestDPIAware true
RequestExecutionLevel user

!include "MUI2.nsh"
!include "WinVer.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"

!define APPNAME "Nebula Media"
!define APPEXE "NebulaMedia.exe"
!ifndef APPVERSION
  !define APPVERSION "1.0.0"
!endif
!define PUBLISHER "Nebula"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\NebulaMedia"

Name "${APPNAME}"
OutFile "..\dist\NebulaMedia-Setup-${APPVERSION}.exe"
InstallDir "$LOCALAPPDATA\Programs\Nebula Media"
InstallDirRegKey HKCU "Software\NebulaMedia" "InstallDir"
SetCompressor /SOLID lzma
SetCompressorDictSize 64
XPStyle on
CRCCheck on
ShowInstDetails nevershow
ShowUninstDetails nevershow
BrandingText "${APPNAME} ${APPVERSION}"

VIProductVersion "${APPVERSION}.0"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "FileDescription" "Instalador de Nebula Media"
VIAddVersionKey "FileVersion" "${APPVERSION}.0"
VIAddVersionKey "ProductVersion" "${APPVERSION}.0"
VIAddVersionKey "LegalCopyright" "Copyright (C) 2026 Nebula"

!define MUI_ICON "..\res\nebula.ico"
!define MUI_UNICON "..\res\nebula.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APPEXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Abrir Nebula Media"
!define MUI_FINISHPAGE_NOREBOOTSUPPORT

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Spanish"

Function CheckNebulaRunning
  System::Call 'user32::FindWindowA(t "NebulaMediaWindow", p 0) p .r0'
  ${If} $0 != 0
    MessageBox MB_OK|MB_ICONSTOP "Cierra Nebula Media antes de continuar."
    Abort
  ${EndIf}
FunctionEnd

Function .onInit
  ${IfNot} ${AtLeastWin10}
    MessageBox MB_OK|MB_ICONSTOP "Nebula Media requiere Windows 10 u 11 de 64 bits."
    Abort
  ${EndIf}
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "Nebula Media requiere Windows de 64 bits."
    Abort
  ${EndIf}
  Call CheckNebulaRunning
FunctionEnd

Function un.onInit
  System::Call 'user32::FindWindowA(t "NebulaMediaWindow", p 0) p .r0'
  ${If} $0 != 0
    MessageBox MB_OK|MB_ICONSTOP "Cierra Nebula Media antes de desinstalar."
    Abort
  ${EndIf}
FunctionEnd

Section "Nebula Media"
  SetShellVarContext current
  SetOutPath "$INSTDIR"
  File "..\dist\NebulaMedia.exe"
  File "..\LEEME.txt"
  File "..\LICENSE.txt"

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  CreateDirectory "$SMPROGRAMS\Nebula Media"
  CreateShortcut "$SMPROGRAMS\Nebula Media\Nebula Media.lnk" "$INSTDIR\${APPEXE}" "" "$INSTDIR\${APPEXE}" 0
  CreateShortcut "$SMPROGRAMS\Nebula Media\Desinstalar Nebula Media.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortcut "$DESKTOP\Nebula Media.lnk" "$INSTDIR\${APPEXE}" "" "$INSTDIR\${APPEXE}" 0

  WriteRegStr HKCU "${UNINSTKEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayVersion" "${APPVERSION}"
  WriteRegStr HKCU "${UNINSTKEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKCU "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\${APPEXE},0"
  WriteRegStr HKCU "${UNINSTKEY}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegStr HKCU "${UNINSTKEY}" "QuietUninstallString" "$\"$INSTDIR\Uninstall.exe$\" /S"
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1
  WriteRegDWORD HKCU "${UNINSTKEY}" "EstimatedSize" 4200

  WriteRegStr HKCU "Software\NebulaMedia" "InstallDir" "$INSTDIR"
SectionEnd

Section "Uninstall"
  SetShellVarContext current

  Delete "$DESKTOP\Nebula Media.lnk"
  Delete "$SMPROGRAMS\Nebula Media\Nebula Media.lnk"
  Delete "$SMPROGRAMS\Nebula Media\Desinstalar Nebula Media.lnk"
  RMDir "$SMPROGRAMS\Nebula Media"

  Delete "$INSTDIR\NebulaMedia.exe"
  Delete "$INSTDIR\LEEME.txt"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"

  DeleteRegKey HKCU "${UNINSTKEY}"
  DeleteRegValue HKCU "Software\NebulaMedia" "InstallDir"
SectionEnd
