@echo off
rem Builds ..\MacCollector.exe with the C# compiler that ships with .NET Framework 4 (Windows 10/11).
setlocal
set CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe
if not exist "%CSC%" set CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe
set SRC="%~dp0Program.cs" "%~dp0Collector.cs" "%~dp0Ui.cs" "%~dp0MainForm.cs"
set REFS=/r:System.Management.dll /r:System.Windows.Forms.dll /r:System.Drawing.dll
set OPTS=/nologo /target:winexe /codepage:65001 /optimize+ %REFS%

rem The icon is drawn by the program itself (Theme.DrawLogo): build once, export app.ico, build again.
if not exist "%~dp0app.ico" (
  "%CSC%" %OPTS% /out:"%TEMP%\MacCollector-icon.exe" %SRC% || exit /b 1
  "%TEMP%\MacCollector-icon.exe" --make-icon "%~dp0app.ico" || exit /b 1
  del "%TEMP%\MacCollector-icon.exe"
)
"%CSC%" %OPTS% /win32icon:"%~dp0app.ico" /out:"%~dp0..\MacCollector.exe" %SRC%
