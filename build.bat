@echo off
setlocal
cd /d "%~dp0"

set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo [ERROR] vcvars64.bat not found at:
  echo   %VCVARS%
  echo Please install Visual Studio with "Desktop development with C++".
  exit /b 1
)

call "%VCVARS%" >nul 2>&1
if errorlevel 1 (
  echo [ERROR] Failed to initialize build environment.
  exit /b 1
)

cl /nologo /std:c++17 /EHsc /utf-8 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 /O2 src\main.cpp src\config.cpp src\login.cpp /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /OUT:campus-login.exe user32.lib gdi32.lib comctl32.lib ole32.lib oleaut32.lib uuid.lib advapi32.lib shell32.lib shlwapi.lib version.lib winhttp.lib bcrypt.lib

if errorlevel 1 (
  echo [ERROR] Build failed.
  exit /b 1
)

echo [OK] Built campus-login.exe
endlocal
