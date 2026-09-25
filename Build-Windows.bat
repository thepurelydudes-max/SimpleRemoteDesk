@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo Checking .NET 8 SDK...
where dotnet >nul 2>nul
if errorlevel 1 goto :nosdk
set "HAS_SDK="
for /f "tokens=*" %%i in ('dotnet --list-sdks 2^>nul ^| findstr /R /B "8\."') do set "HAS_SDK=1"
if not defined HAS_SDK goto :nosdk

echo .NET 8 SDK found.
echo.

if exist publish rmdir /s /q publish
if exist .stage rmdir /s /q .stage
mkdir publish\SimpleRemoteDesk\Runtime
mkdir .stage\Runtime

echo Restoring packages...
dotnet restore "SimpleRemoteDesk\SimpleRemoteDesk.csproj"
if errorlevel 1 goto :fail

echo.
echo Building structured portable layout...
echo Root: small SimpleRemoteDesk.exe
echo Runtime: all .NET 8 / WinForms / NAudio / application DLL files
echo Target computers DO NOT need .NET installed.
echo.

dotnet publish "SimpleRemoteDesk\SimpleRemoteDesk.csproj" -c Release -r win-x64 --self-contained true ^
  -p:PublishSingleFile=false ^
  -p:PublishTrimmed=false ^
  -p:PublishReadyToRun=false ^
  -p:DebugType=None ^
  -p:DebugSymbols=false ^
  -p:SatelliteResourceLanguages=ru ^
  -o ".stage\Runtime"
if errorlevel 1 goto :fail

if not exist ".stage\Runtime\SimpleRemoteDesk.exe" goto :fail
if not exist ".stage\Runtime\SimpleRemoteDesk.dll" goto :fail

copy /y ".stage\Runtime\SimpleRemoteDesk.exe" "publish\SimpleRemoteDesk\SimpleRemoteDesk.exe" >nul
xcopy /e /i /y /q ".stage\Runtime\*" "publish\SimpleRemoteDesk\Runtime\" >nul
if errorlevel 1 goto :fail

del /q "publish\SimpleRemoteDesk\Runtime\SimpleRemoteDesk.exe" >nul 2>nul
for /r "publish\SimpleRemoteDesk\Runtime" %%F in (*.pdb) do del /q "%%F" >nul 2>nul

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0BuildTools\Patch-AppHost.ps1" ^
  -ExePath "%~dp0publish\SimpleRemoteDesk\SimpleRemoteDesk.exe" ^
  -ManagedRelativePath "Runtime\SimpleRemoteDesk.dll"
if errorlevel 1 goto :fail

(
  echo Simple Remote Desk 2.11 - structured portable build
  echo.
  echo START: SimpleRemoteDesk.exe
  echo.
  echo The root folder is intentionally clean.
  echo All .NET 8 Desktop Runtime, WinForms, NAudio and application dependencies are stored in the Runtime folder.
  echo Do NOT move SimpleRemoteDesk.exe away from the Runtime folder.
  echo The build is self-contained: target PCs do not need .NET installed.
  echo Host and Viewer run in ONE SimpleRemoteDesk.exe process.
) > "publish\SimpleRemoteDesk\README.txt"

rmdir /s /q .stage

echo.
echo ========================================
echo BUILD SUCCESSFUL
echo ========================================
echo Folder: publish\SimpleRemoteDesk\
echo.
echo   SimpleRemoteDesk.exe   ^<-- small root EXE / one process
 echo  Runtime\               ^<-- all runtime and application files
 echo  README.txt
 echo.
echo Target computers do NOT need .NET installed.
echo Copy the WHOLE SimpleRemoteDesk folder.
echo.
pause
exit /b 0

:nosdk
echo.
echo ========================================
echo .NET 8 SDK NOT FOUND
echo ========================================
echo The SDK is needed only on this BUILD computer.
echo Target computers do not need the SDK or Runtime installed.
echo.
pause
exit /b 1

:fail
echo.
echo ========================================
echo BUILD FAILED
echo ========================================
echo Read the error above. Output was not completed.
echo.
pause
exit /b 1
