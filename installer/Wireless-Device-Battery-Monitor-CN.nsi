; 无线设备电量监视器 CN —— NSIS 安装脚本
; 生成：dist\Wireless-Device-Battery-Monitor-CN-Setup-<版本>.exe
; 免管理员、按用户安装（与上游安装器一致）。

Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"

!define APP_NAME        "无线设备电量监视器 CN"
!define APP_ID          "Wireless-Device-Battery-Monitor-CN"
!define APP_VERSION     "1.0.0"
!define APP_EXE         "WirelessBattery.exe"
!define APP_PUBLISHER   "Wireless-Device-Battery-Monitor-CN contributors"
!define APP_URL         "https://github.com/krisgfx9/Wireless-Device-Battery-Monitor"
!define ROOT            ".."

Name "${APP_NAME}"
OutFile "${ROOT}\dist\Wireless-Device-Battery-Monitor-CN-Setup-${APP_VERSION}.exe"
InstallDir "$LOCALAPPDATA\Programs\${APP_ID}"
InstallDirRegKey HKCU "Software\${APP_ID}" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 32
BrandingText "${APP_ID}"

VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey /LANG=2052 "ProductName"     "${APP_NAME}"
VIAddVersionKey /LANG=2052 "FileDescription" "无线设备电量监视器 CN 安装程序"
VIAddVersionKey /LANG=2052 "FileVersion"     "${APP_VERSION}"
VIAddVersionKey /LANG=2052 "ProductVersion"  "${APP_VERSION}"
VIAddVersionKey /LANG=2052 "CompanyName"     "${APP_PUBLISHER}"
VIAddVersionKey /LANG=2052 "LegalCopyright"  "GPLv3+"

!define MUI_ICON          "${ROOT}\res\app.ico"
!define MUI_UNICON        "${ROOT}\res\app.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "欢迎安装 ${APP_NAME}"
!define MUI_WELCOMEPAGE_TEXT "本程序把无线鼠标、耳机、键盘和手柄的电量直接显示在 Windows 任务栏上。$\r$\n$\r$\n这是上游项目 Wireless Battery 的简体中文本地化版本，按用户安装、无需管理员权限。$\r$\n$\r$\n点击“下一步”继续。"
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "立即启动 ${APP_NAME}"
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\README.txt"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "查看使用说明"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

; ---------------------------------------------------------------- 安装

Function QuitRunningApp
  IfFileExists "$INSTDIR\${APP_EXE}" 0 done
  DetailPrint "正在关闭已在运行的程序…"
  ExecWait '"$INSTDIR\${APP_EXE}" --quit'
  Sleep 600
  done:
FunctionEnd

Section "主程序（必需）" SecMain
  SectionIn RO
  Call QuitRunningApp
  SetOutPath "$INSTDIR"
  File "${ROOT}\WirelessBattery.exe"
  File "${ROOT}\third_party\headsetcontrol\headsetcontrol.exe"
  File /oname=LICENSE.txt "${ROOT}\LICENSE"
  File "${ROOT}\installer\THIRD_PARTY_NOTICES.txt"
  File "${ROOT}\installer\README.txt"

  CreateDirectory "$SMPROGRAMS"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}"

  WriteRegStr   HKCU "Software\${APP_ID}" "InstallDir" "$INSTDIR"
  WriteRegStr   HKCU "Software\${APP_ID}" "Version" "${APP_VERSION}"

  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegStr   HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}" "DisplayName"     "${APP_NAME}"
  WriteRegStr   HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}" "DisplayIcon"     "$INSTDIR\${APP_EXE}"
  WriteRegStr   HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}" "DisplayVersion"  "${APP_VERSION}"
  WriteRegStr   HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}" "Publisher"       "${APP_PUBLISHER}"
  WriteRegStr   HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}" "HelpLink"        "${APP_URL}"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}" "NoRepair" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}" "EstimatedSize" $0
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr   HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
SectionEnd

Section /o "开机自动启动" SecAutostart
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "WirelessBattery" "$INSTDIR\${APP_EXE}"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "任务栏电量挂件本体，以及用于部分耳机的 HeadsetControl 组件。"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecAutostart} "登录 Windows 时自动启动（也可以稍后在托盘菜单里开关）。"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ---------------------------------------------------------------- 卸载

Section "Uninstall"
  ExecWait '"$INSTDIR\${APP_EXE}" --quit'
  Sleep 600

  Delete "$SMPROGRAMS\${APP_NAME}.lnk"
  Delete "$INSTDIR\${APP_EXE}"
  Delete "$INSTDIR\headsetcontrol.exe"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\THIRD_PARTY_NOTICES.txt"
  Delete "$INSTDIR\README.txt"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir  "$INSTDIR"

  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "WirelessBattery"
  DeleteRegKey  HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}"
  DeleteRegKey  HKCU "Software\${APP_ID}"
  DeleteRegKey  HKCU "Software\WirelessBattery"
SectionEnd
