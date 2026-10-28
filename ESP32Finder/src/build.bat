@echo off
rem Builds ..\ESP32Finder.exe with the C# compiler that ships with .NET Framework 4 (Windows 10/11).
setlocal
set CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe
if not exist "%CSC%" set CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe
set SRC="%~dp0Program.cs" "%~dp0Finder.cs" "%~dp0Ui.cs" "%~dp0MainForm.cs"
set REFS=/r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.ServiceProcess.dll /r:System.Web.Extensions.dll /r:System.Core.dll
set OPTS=/nologo /target:winexe /codepage:65001 /optimize+ %REFS%

rem The icon is drawn by the program itself (Theme.DrawLogo): build once, export app.ico, build again.
if not exist "%~dp0app.ico" (
  "%CSC%" %OPTS% /out:"%TEMP%\ESP32Finder-icon.exe" %SRC% || exit /b 1
  "%TEMP%\ESP32Finder-icon.exe" --make-icon "%~dp0app.ico" || exit /b 1
  del "%TEMP%\ESP32Finder-icon.exe"
)
"%CSC%" %OPTS% /win32icon:"%~dp0app.ico" /out:"%~dp0..\ESP32Finder.exe" %SRC%
