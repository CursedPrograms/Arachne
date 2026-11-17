@echo off
setlocal
cd /d "%~dp0"
rem Installs Arachne for this user: %LOCALAPPDATA%\Programs\Arachne, with Start menu and Desktop shortcuts named Atelier - Arachne.
rem Run build.bat first (or unzip a release and run this beside it).
set "SRC=dist\Arachne"
if exist "Arachne.exe" set "SRC=."
if not exist "%SRC%\Arachne.exe" ( echo Build it first: build.bat & exit /b 1 )
tasklist /fi "imagename eq Arachne.exe" | find /i "Arachne.exe" >nul
if not errorlevel 1 ( echo Arachne is open: close it and run this again. & exit /b 1 )
set "INST=%LOCALAPPDATA%\Programs\Arachne"
if not exist "%INST%" mkdir "%INST%"
xcopy /e /i /q /y "%SRC%\*" "%INST%\" >nul || ( echo Couldn't copy to %INST% & exit /b 1 )
powershell -NoProfile -Command "$s=New-Object -ComObject WScript.Shell; foreach($d in @([Environment]::GetFolderPath('Programs'),[Environment]::GetFolderPath('Desktop'))){ Remove-Item (Join-Path $d 'Arachne.lnk') -Force -ErrorAction SilentlyContinue; $l=$s.CreateShortcut((Join-Path $d 'Atelier - Arachne.lnk')); $l.TargetPath='%INST%\Arachne.exe'; $l.WorkingDirectory='%INST%'; $l.IconLocation='%INST%\Arachne.exe,0'; $l.Description='Arachne: pictures into clean vector art'; $l.Save() }"
echo Installed: %INST%
if /i not "%~1"=="--quiet" echo Start it from the Start menu or the Desktop.
exit /b 0
