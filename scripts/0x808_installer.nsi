; 0x808 NSIS Installer Script
; Builds a Windows installer from Linux via makensis
;
; Usage: update VER below, then run: makensis 0x808_installer.nsi

!include "MUI2.nsh"

!define VER "1.3.0"
!define VERFULL "1.3.0.0"

; --- General ---
Name "0x808 v${VER}"
OutFile "../release/0x808-${VER}-windows-x64-setup.exe"
InstallDir "$PROGRAMFILES64\0x808"
InstallDirRegKey HKLM "Software\0x808" "InstallDir"
RequestExecutionLevel admin

; --- Version info ---
VIProductVersion "${VERFULL}"
VIAddVersionKey "ProductName" "0x808"
VIAddVersionKey "CompanyName" "Dan Michael"
VIAddVersionKey "FileDescription" "0x808 Drum Machine & Synth Workstation"
VIAddVersionKey "FileVersion" "${VER}"
VIAddVersionKey "LegalCopyright" "MIT License"

; --- Interface ---
!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"

; --- Pages ---
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; --- Upgrade detection: uninstall previous version silently ---

Function .onInit
    ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\0x808" "UninstallString"
    StrCmp $0 "" done

    MessageBox MB_OKCANCEL|MB_ICONINFORMATION \
        "0x808 is already installed.$\n$\nClick OK to uninstall the previous version and continue, or Cancel to abort." \
        IDOK uninst
    Abort

uninst:
    ; Run the existing uninstaller silently
    ExecWait '"$0" /S _?=$INSTDIR'
    Delete "$INSTDIR\uninstall.exe"

done:
FunctionEnd

; --- Sections ---

Section "0x808 Standalone (required)" SecMain
    SectionIn RO

    SetOutPath "$INSTDIR"
    File "..\build_win\0x808.exe"
    File "..\README.md"
    File "..\LICENSE"

    ; Samples
    SetOutPath "$INSTDIR\samples"
    File /r "..\samples\*.*"

    ; Themes
    SetOutPath "$INSTDIR\themes"
    File /r "..\themes\*.*"

    ; Start Menu
    CreateDirectory "$SMPROGRAMS\0x808"
    CreateShortCut "$SMPROGRAMS\0x808\0x808.lnk" "$INSTDIR\0x808.exe"
    CreateShortCut "$SMPROGRAMS\0x808\Uninstall.lnk" "$INSTDIR\uninstall.exe"

    ; Desktop shortcut
    CreateShortCut "$DESKTOP\0x808.lnk" "$INSTDIR\0x808.exe"

    ; Registry
    WriteRegStr HKLM "Software\0x808" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\0x808" \
        "DisplayName" "0x808 - Drum Machine & Synth"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\0x808" \
        "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\0x808" \
        "DisplayVersion" "${VER}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\0x808" \
        "Publisher" "Dan Michael"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\0x808" \
        "DisplayIcon" '"$INSTDIR\0x808.exe",0'
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\0x808" \
        "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\0x808" \
        "NoRepair" 1

    ; Uninstaller
    WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "VST3 Plugin" SecVST3
    SetOutPath "$COMMONFILES64\VST3\0x808.vst3\Contents\x86_64-win"
    File "..\build_win\0x808.vst3\Contents\x86_64-win\0x808.vst3"
SectionEnd

Section "CLAP Plugin" SecCLAP
    SetOutPath "$COMMONFILES64\CLAP"
    File "..\build_win\0x808.clap"
SectionEnd

; --- Descriptions ---
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "0x808 standalone application with drum samples and themes."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecVST3} "VST3 plugin for use in DAWs (installs to Common Files\VST3)."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecCLAP} "CLAP plugin for use in DAWs (installs to Common Files\CLAP)."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; --- Uninstaller ---

Section "Uninstall"
    Delete "$INSTDIR\0x808.exe"
    Delete "$INSTDIR\README.md"
    Delete "$INSTDIR\LICENSE"
    Delete "$INSTDIR\uninstall.exe"

    RMDir /r "$INSTDIR\samples"
    RMDir /r "$INSTDIR\themes"
    RMDir "$INSTDIR"

    ; Plugins
    Delete "$COMMONFILES64\VST3\0x808.vst3\Contents\x86_64-win\0x808.vst3"
    RMDir "$COMMONFILES64\VST3\0x808.vst3\Contents\x86_64-win"
    RMDir "$COMMONFILES64\VST3\0x808.vst3\Contents"
    RMDir "$COMMONFILES64\VST3\0x808.vst3"
    Delete "$COMMONFILES64\CLAP\0x808.clap"

    ; Shortcuts
    Delete "$SMPROGRAMS\0x808\0x808.lnk"
    Delete "$SMPROGRAMS\0x808\Uninstall.lnk"
    RMDir "$SMPROGRAMS\0x808"
    Delete "$DESKTOP\0x808.lnk"

    ; Registry
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\0x808"
    DeleteRegKey HKLM "Software\0x808"
SectionEnd
