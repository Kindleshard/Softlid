; The BlinkBuddy installer (NSIS 3, chosen over Inno Setup for its size: its own part of the setup and the uninstaller
; are each about 100 KB rather than megabytes). Built by release.ps1, which passes the version and the size of each
; language's voice: /DVERSION=1.0.0 /DKB_en=310 /DKB_tr=330 ...
; Installs for the current user only (no administrator rights), into %LocalAppData%\Programs\BlinkBuddy, which also
; lets the built-in updater replace the exe in place. Settings in %AppData%\BlinkBuddy are kept on uninstall.
; The voice comes in just one language (the Windows one, unless another is picked) or in all of them; unattended (/S),
; /VOICES=all or /VOICES=<code> chooses, and an update keeps what was chosen before.

Unicode true
ManifestDPIAware true
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 8

!ifndef VERSION
  !define VERSION 0.0.0
!endif
!define UNINSTALL "Software\Microsoft\Windows\CurrentVersion\Uninstall\BlinkBuddy"
!define RUN "Software\Microsoft\Windows\CurrentVersion\Run"

Name "BlinkBuddy"
OutFile "..\dist\BlinkBuddy-${VERSION}-setup.exe"
InstallDir "$LOCALAPPDATA\Programs\BlinkBuddy"
InstallDirRegKey HKCU "${UNINSTALL}" "InstallLocation"
BrandingText " "
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "BlinkBuddy"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "BlinkBuddy setup"
VIAddVersionKey "CompanyName" "BlinkBuddy"
VIAddVersionKey "LegalCopyright" "MIT licence"

!include MUI2.nsh
!include nsDialogs.nsh
!include LogicLib.nsh
!include FileFunc.nsh
!include WinMessages.nsh

; Each language with a voice: its folder (as BlinkBuddy names it), its name, and Windows' primary language number.
!macro ForEachLanguage m
  !insertmacro ${m} en "English" 0x09
  !insertmacro ${m} tr "Turkish" 0x1F
  !insertmacro ${m} de "German" 0x07
  !insertmacro ${m} fr "French" 0x0C
  !insertmacro ${m} es "Spanish" 0x0A
  !insertmacro ${m} pt "Portuguese (Brazil)" 0x16
  !insertmacro ${m} it "Italian" 0x10
  !insertmacro ${m} nl "Dutch" 0x13
  !insertmacro ${m} pl "Polish" 0x15
  !insertmacro ${m} cs "Czech" 0x05
  !insertmacro ${m} hu "Hungarian" 0x0E
  !insertmacro ${m} ro "Romanian" 0x18
  !insertmacro ${m} sv "Swedish" 0x1D
  !insertmacro ${m} da "Danish" 0x06
  !insertmacro ${m} fi "Finnish" 0x0B
  !insertmacro ${m} el "Greek" 0x08
  !insertmacro ${m} ru "Russian" 0x19
  !insertmacro ${m} uk "Ukrainian" 0x22
  !insertmacro ${m} ja "Japanese" 0x11
  !insertmacro ${m} ko "Korean" 0x12
  !insertmacro ${m} zh "Chinese (Simplified)" 0x04
!macroend

; The sizes (0 when release.ps1 didn't pass them), and all of them together.
!define KB_ALL 0
!define LANGUAGE_COUNT 0
!macro Size code name id
  !ifndef KB_${code}
    !define KB_${code} 0
  !endif
  !define /redef /math KB_ALL ${KB_ALL} + ${KB_${code}}
  !define /redef /math LANGUAGE_COUNT ${LANGUAGE_COUNT} + 1
!macroend
!insertmacro ForEachLanguage Size

Var Voices        ; "all", or the one language's folder
Var VoicePage
Var OneButton
Var AllButton
Var LanguageList
Var FullSize      ; the main section's size with every voice, in KB

!define MUI_ICON "..\res\BlinkBuddy.ico"
!define MUI_UNICON "..\res\BlinkBuddy.ico"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_NODESC
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipFolderOnUpdate
!insertmacro MUI_PAGE_DIRECTORY
Page custom VoiceShow VoiceLeave
!define MUI_PAGE_HEADER_TEXT "Options"
!define MUI_PAGE_HEADER_SUBTEXT "Choose how BlinkBuddy starts."
!define MUI_COMPONENTSPAGE_TEXT_TOP " "
!define MUI_COMPONENTSPAGE_TEXT_COMPLIST " "
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\BlinkBuddy.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start BlinkBuddy"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; A running BlinkBuddy holds its files: ask for it to be closed first (unattended, give up).
!macro CloseFirst un
Function ${un}CloseFirst
  retry:
  System::Call 'kernel32::OpenMutexW(i 0x00100000, i 0, w "Local\BlinkBuddy.Instance") p .r0'
  ${If} $0 P<> 0
    System::Call 'kernel32::CloseHandle(p r0)'
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "BlinkBuddy is running. Quit it from its tray icon (right-click, Quit), then click Retry." /SD IDCANCEL IDRETRY retry
    Quit
  ${EndIf}
FunctionEnd
!macroend
!insertmacro CloseFirst ""
!insertmacro CloseFirst "un."

!macro IsKnown code name id
  ${If} $Voices == ${code}
    StrCpy $1 1
  ${EndIf}
!macroend
!macro FromWindows code name id
  ${If} $0 = ${id}
    StrCpy $Voices ${code}
  ${EndIf}
!macroend

Function .onInit
  Call CloseFirst
  Call RememberSize
  ; What was asked for (/VOICES=), else what was chosen last time, else the Windows language, else English.
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/VOICES=" $Voices
  ${If} ${Errors}
    ReadRegStr $Voices HKCU "${UNINSTALL}" "Voices"
  ${EndIf}
  StrCpy $1 0
  !insertmacro ForEachLanguage IsKnown
  ${If} $1 = 0
  ${AndIf} $Voices != "all"
    StrCpy $Voices "en"
    System::Call 'kernel32::GetUserDefaultUILanguage() i .r0'
    IntOp $0 $0 & 0x3FF
    !insertmacro ForEachLanguage FromWindows
  ${EndIf}
FunctionEnd

; An update goes where BlinkBuddy already is.
Function SkipFolderOnUpdate
  ReadRegStr $0 HKCU "${UNINSTALL}" "InstallLocation"
  ${If} $0 != ""
    Abort
  ${EndIf}
FunctionEnd

; " (about 330 KB)" or " (about 6.2 MB)", for $0 KB; nothing when the size isn't known.
Function About
  ${If} $0 = 0
    StrCpy $0 ""
  ${ElseIf} $0 < 1000
    StrCpy $0 " (about $0 KB)"
  ${Else}
    IntOp $1 $0 / 1024
    IntOp $2 $0 % 1024
    IntOp $2 $2 * 10
    IntOp $2 $2 / 1024
    StrCpy $0 " (about $1.$2 MB)"
  ${EndIf}
FunctionEnd

!macro NameToCode code name id
  ${If} $0 == "${name}"
    StrCpy $1 ${code}
    StrCpy $2 ${KB_${code}}
  ${EndIf}
!macroend
; The language picked in the list: its folder in $1, its size in $2.
Function Picked
  ${NSD_GetText} $LanguageList $0
  !insertmacro ForEachLanguage NameToCode
FunctionEnd

Function LanguageChanged
  Pop $0
  Call Picked
  StrCpy $0 $2
  Call About
  SendMessage $OneButton ${WM_SETTEXT} 0 "STR:Just one language$0"
  ${NSD_Check} $OneButton
  ${NSD_Uncheck} $AllButton
FunctionEnd

!macro AddName code name id
  ${NSD_CB_AddString} $LanguageList "${name}"
  ${If} $Voices == ${code}
    StrCpy $3 "${name}"
  ${EndIf}
!macroend

Function VoiceShow
  !insertmacro MUI_HEADER_TEXT "Voice" "Which languages should BlinkBuddy speak?"
  nsDialogs::Create 1018
  Pop $VoicePage
  ${NSD_CreateLabel} 0 0 100% 24u "BlinkBuddy reads the steps of its breaks and its tour aloud. The voice for just your language keeps it small; all of them let you switch freely."
  Pop $0
  ${NSD_CreateRadioButton} 0 34u 100% 12u "Just one language"
  Pop $OneButton
  ${NSD_CreateDropList} 12u 49u 140u 120u ""
  Pop $LanguageList
  StrCpy $3 "English"
  !insertmacro ForEachLanguage AddName
  ${NSD_CB_SelectString} $LanguageList $3
  ${NSD_OnChange} $LanguageList LanguageChanged
  StrCpy $0 ${KB_ALL}
  Call About
  ${NSD_CreateRadioButton} 0 72u 100% 12u "All ${LANGUAGE_COUNT} languages$0"
  Pop $AllButton
  ${NSD_CreateLabel} 0 96u 100% 24u "A language without its voice is read by a Windows voice, if one is installed. Run the installer again to change this."
  Pop $0
  Push $LanguageList
  Call LanguageChanged
  ${If} $Voices == "all"
    ${NSD_Check} $AllButton
    ${NSD_Uncheck} $OneButton
  ${EndIf}
  nsDialogs::Show
FunctionEnd

; Each language's voice: installed when wanted, otherwise removed (after a change to just one).
!macro VoiceFolder code name id
  ${If} $Voices == "all"
  ${OrIf} $Voices == ${code}
    SetOutPath "$INSTDIR\voice\${code}"
    File "..\res\voice\${code}\*.mp3"
  ${Else}
    RMDir /r "$INSTDIR\voice\${code}"
  ${EndIf}
!macroend

Section "-BlinkBuddy" SecMain   ; (always, so not listed)
  SetOutPath "$INSTDIR"
  File "..\bin\BlinkBuddy.exe"
  Delete "$INSTDIR\voice\*.mp3"   ; (clips from before each language had a folder of its own)
  SetOutPath "$INSTDIR\voice"
  File "..\res\voice\CREDITS.txt"
  !insertmacro ForEachLanguage VoiceFolder
  SetOutPath "$INSTDIR"

  WriteUninstaller "$INSTDIR\uninstall.exe"
  CreateShortcut "$SMPROGRAMS\BlinkBuddy.lnk" "$INSTDIR\BlinkBuddy.exe"
  WriteRegStr HKCU "${UNINSTALL}" "DisplayName" "BlinkBuddy"
  WriteRegStr HKCU "${UNINSTALL}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTALL}" "Publisher" "BlinkBuddy"
  WriteRegStr HKCU "${UNINSTALL}" "DisplayIcon" "$INSTDIR\BlinkBuddy.exe"
  WriteRegStr HKCU "${UNINSTALL}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "${UNINSTALL}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegStr HKCU "${UNINSTALL}" "Voices" "$Voices"
  WriteRegDWORD HKCU "${UNINSTALL}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKCU "${UNINSTALL}" "EstimatedSize" $0
SectionEnd

; The same entry BlinkBuddy's own "Start with Windows" option writes.
Section "Start BlinkBuddy when I sign in" SecStartup
  WriteRegStr HKCU "${RUN}" "BlinkBuddy" '"$INSTDIR\BlinkBuddy.exe" --tray'
SectionEnd

Section /o "Create a desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\BlinkBuddy.lnk" "$INSTDIR\BlinkBuddy.exe"
SectionEnd

; (Down here, after the section it resizes.)
Function VoiceLeave
  ${NSD_GetState} $AllButton $0
  ${If} $0 = ${BST_CHECKED}
    StrCpy $Voices "all"
    StrCpy $2 ${KB_ALL}
  ${Else}
    Call Picked
    StrCpy $Voices $1
  ${EndIf}
  ; The space needed counts only the voices chosen (the section holds them all).
  IntOp $0 $FullSize - ${KB_ALL}
  IntOp $0 $0 + $2
  SectionSetSize ${SecMain} $0
FunctionEnd

; How big the main section is with every voice (before VoiceLeave counts just the chosen ones).
Function RememberSize
  SectionGetSize ${SecMain} $FullSize
FunctionEnd

Function un.onInit
  Call un.CloseFirst
FunctionEnd

Section "Uninstall"
  Delete "$INSTDIR\BlinkBuddy.exe"
  Delete "$INSTDIR\BlinkBuddy.exe.old"   ; (left behind by an update or the build)
  Delete "$INSTDIR\BlinkBuddy.exe.new"
  RMDir /r "$INSTDIR\voice"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\BlinkBuddy.lnk"
  Delete "$DESKTOP\BlinkBuddy.lnk"
  ; The start-with-Windows entry goes too, whether this installer or BlinkBuddy's own option made it.
  DeleteRegValue HKCU "${RUN}" "BlinkBuddy"
  DeleteRegKey HKCU "${UNINSTALL}"
SectionEnd
