; Muralop installer (NSIS 3). Built by `ninja installer` in a Windows build tree,
; which defines VERSION, ARCH (x64 | arm64), EXE, MPV_DLL and OUTFILE.
;
; Installs for the current user into %LOCALAPPDATA%\Programs\Muralop, so there is
; no UAC prompt. The settings in %APPDATA%\Muralop survive an uninstall.

Unicode true
ManifestDPIAware true
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"

!define APP_NAME "Muralop"
!define APP_EXE "muralop.exe"
!define MAIN_CLASS "MuralopMain"   ; the hidden main window, see src/win/main.cpp
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Muralop"
!define LEGACY_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\VideoWallpaper"
!define RUN_KEY "Software\Microsoft\Windows\CurrentVersion\Run"
!define SHORTCUT "$SMPROGRAMS\${APP_NAME}.lnk"

Name "${APP_NAME}"
OutFile "${OUTFILE}"
RequestExecutionLevel user
InstallDir "$LOCALAPPDATA\Programs\Muralop"
InstallDirRegKey HKCU "${UNINST_KEY}" "InstallLocation"

!define MUI_ICON "${__FILEDIR__}/Muralop.ico"
!define MUI_UNICON "${__FILEDIR__}/Muralop.ico"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_NODESC
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"

!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Picked by the system's UI language; English when it is neither.
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "SimpChinese"

LangString SecApp ${LANG_ENGLISH} "Muralop"
LangString SecApp ${LANG_SIMPCHINESE} "Muralop"
LangString SecAutostart ${LANG_ENGLISH} "Start with Windows"
LangString SecAutostart ${LANG_SIMPCHINESE} "开机自动启动"
LangString MsgWrongArch ${LANG_ENGLISH} "This installer is for ${ARCH} Windows. Please download the one that matches this PC."
LangString MsgWrongArch ${LANG_SIMPCHINESE} "这个安装包是给 ${ARCH} 版 Windows 的，请下载和这台电脑匹配的版本。"
LangString MsgStillRunning ${LANG_ENGLISH} "Muralop is still running. Quit it from its tray icon, then click Retry."
LangString MsgStillRunning ${LANG_SIMPCHINESE} "Muralop 还在运行。请先从托盘图标退出，再点“重试”。"

VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=${LANG_ENGLISH} "FileDescription" "${APP_NAME} ${VERSION} (${ARCH}) Setup"
VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalCopyright" "Copyright (C) sdutwsl"

; Ask a running instance to quit (it hands the desktop back to Explorer on the way out),
; then wait until the exe is no longer locked so it can be replaced or deleted.
!macro CLOSE_RUNNING un
Function ${un}CloseRunning
  retry:
    FindWindow $0 "${MAIN_CLASS}"
    IntCmp $0 0 closed
    SendMessage $0 ${WM_CLOSE} 0 0 /TIMEOUT=5000
    StrCpy $1 0
  waitWindow:
    Sleep 200
    FindWindow $0 "${MAIN_CLASS}"
    IntCmp $0 0 closed
    IntOp $1 $1 + 1
    IntCmp $1 50 0 waitWindow   ; about 10 s
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "$(MsgStillRunning)" /SD IDCANCEL IDRETRY retry
    Abort
  closed:
    ; The window goes away before the process has finished shutting down.
    IfFileExists "$INSTDIR\${APP_EXE}" 0 done
    StrCpy $1 0
  waitFile:
    ClearErrors
    FileOpen $2 "$INSTDIR\${APP_EXE}" a
    IfErrors 0 unlocked
    Sleep 200
    IntOp $1 $1 + 1
    IntCmp $1 50 0 waitFile
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "$(MsgStillRunning)" /SD IDCANCEL IDRETRY retry
    Abort
  unlocked:
    FileClose $2
  done:
FunctionEnd
!macroend
!insertmacro CLOSE_RUNNING ""
!insertmacro CLOSE_RUNNING "un."

Function .onInit
!if "${ARCH}" == "arm64"
    ${IfNot} ${IsNativeARM64}
!else
    ${IfNot} ${RunningX64}
!endif
        MessageBox MB_OK|MB_ICONSTOP "$(MsgWrongArch)" /SD IDOK
        Abort
    ${EndIf}
FunctionEnd

Section "!$(SecApp)" SecApp
    SectionIn RO
    ; Upgrade the previous app name in place; its uninstaller preserves the old config.
    ReadRegStr $2 HKCU "${LEGACY_UNINST_KEY}" "InstallLocation"
    StrCmp $2 "" legacyDone
    IfFileExists "$2\uninstall.exe" 0 legacyDone
    ExecWait '"$2\uninstall.exe" /S' $0
    ${If} $0 != 0
        Abort
    ${EndIf}
  legacyDone:
    Call CloseRunning
    SetOutPath "$INSTDIR"
    File "/oname=${APP_EXE}" "${EXE}"
    File "/oname=libmpv-2.dll" "${MPV_DLL}"
    WriteUninstaller "$INSTDIR\uninstall.exe"
    CreateShortcut "${SHORTCUT}" "$INSTDIR\${APP_EXE}"
    DeleteRegValue HKCU "${RUN_KEY}" "Muralop"   ; the autostart section adds it back if selected

    WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\${APP_EXE}"
    WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "sdutwsl"
    WriteRegStr HKCU "${UNINST_KEY}" "URLInfoAbout" "https://github.com/sdutwsl/Muralop"
    WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
    WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" $0
SectionEnd

Section "$(SecAutostart)" SecAutostart
    WriteRegStr HKCU "${RUN_KEY}" "Muralop" '"$INSTDIR\${APP_EXE}"'
SectionEnd

Section "Uninstall"
    Call un.CloseRunning
    Delete "$INSTDIR\${APP_EXE}"
    Delete "$INSTDIR\libmpv-2.dll"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"
    Delete "${SHORTCUT}"
    DeleteRegValue HKCU "${RUN_KEY}" "Muralop"
    DeleteRegKey HKCU "${UNINST_KEY}"
SectionEnd
