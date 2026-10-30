@echo off
setlocal
cd /d "%~dp0"
set "VS_BASE=%VSINSTALLDIR%"
if not defined VS_BASE (
  set "VS_BASE=C:\Program Files\Microsoft Visual Studio\18\Community"
  if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
    for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set "VS_BASE=%%i"
  )
)
call :initialize_vs
if not "%errorlevel%"=="0" exit /b 1
if not defined QT_ARM64_ROOT set "QT_ARM64_ROOT=%CD%\.tools\Qt\6.10.0\msvc2022_arm64"
if not defined QT_HOST_ROOT set "QT_HOST_ROOT=%CD%\.tools\Qt\6.10.0\msvc2022_64"
set "PATH=%VSINSTALLDIR%VC\Tools\Llvm\ARM64\bin;%VSINSTALLDIR%VC\Tools\Llvm\x64\bin;%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
cmake --preset windows-arm64
if not "%errorlevel%"=="0" exit /b 1
cmake --build --preset windows-arm64
if not "%errorlevel%"=="0" exit /b 1
if /i "%~1"=="build" exit /b 0
if /i "%~1"=="package" goto package
set "PATH=%QT_ARM64_ROOT%\bin;%PATH%"
ctest --preset windows-arm64
if not "%errorlevel%"=="0" exit /b 1
:package
cmake --install build-arm64 --prefix "%CD%\build-arm64\dist"
exit /b %errorlevel%

:initialize_vs
if not exist "%VS_BASE%\Common7\Tools\VsDevCmd.bat" (
  echo Visual Studio C++ ARM64 tools were not found.
  exit /b 1
)
call "%VS_BASE%\Common7\Tools\VsDevCmd.bat" -arch=arm64 -host_arch=arm64
exit /b %errorlevel%
