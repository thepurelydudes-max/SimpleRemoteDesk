@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo Checking .NET 8 SDK...
where dotnet >nul 2>nul
if errorlevel 1 goto :nosdk
set "HAS_SDK="
for /f "tokens=*" %%i in ('dotnet --list-sdks 2^>nul ^| findstr /R /B "8\."') do set "HAS_SDK=1"
if not defined HAS_SDK goto :nosdk

if exist publish-small rmdir /s /q publish-small
mkdir publish-small\SimpleRemoteDesk

echo Restoring packages...
dotnet restore "SimpleRemoteDesk\SimpleRemoteDesk.csproj"
if errorlevel 1 goto :fail

echo Building EXTRA SMALL framework-dependent version...
dotnet publish "SimpleRemoteDesk\SimpleRemoteDesk.csproj" -c Release -r win-x64 --self-contained false ^
  -p:PublishSingleFile=false ^
  -p:PublishTrimmed=false ^
  -p:PublishReadyToRun=false ^
  -p:DebugType=None ^
  -p:DebugSymbols=false ^
  -o "publish-small\SimpleRemoteDesk"
if errorlevel 1 goto :fail

echo.
echo ========================================
echo BUILD SUCCESSFUL
 echo ========================================
echo Folder: publish-small\SimpleRemoteDesk\
echo Main file: SimpleRemoteDesk.exe
echo.
echo IMPORTANT: .NET 8 Desktop Runtime x64 must be installed on the target PC.
echo.
pause
exit /b 0

:nosdk
echo .NET 8 SDK NOT FOUND
pause
exit /b 1

:fail
echo BUILD FAILED
pause
exit /b 1
