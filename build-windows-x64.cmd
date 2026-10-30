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
if not defined QT_X64_ROOT set "QT_X64_ROOT=%CD%\.tools\Qt\6.10.0\mingw_64"
if not defined QT_HOST_ROOT set "QT_HOST_ROOT=%CD%\.tools\Qt\6.10.0\msvc2022_64"
if not defined MINGW_ROOT set "MINGW_ROOT=%CD%\.tools\Qt\Tools\mingw1310_64"
if not exist "%QT_X64_ROOT%\bin\qmake.exe" (
  echo Qt 6 MinGW x64 was not found: "%QT_X64_ROOT%"
  exit /b 1
)
if not exist "%MINGW_ROOT%\bin\gcc.exe" (
  echo MinGW GCC x64 was not found: "%MINGW_ROOT%"
  exit /b 1
)
if not exist "%QT_HOST_ROOT%\bin\qsb.exe" (
  echo Qt Shader Tools host executable was not found: "%QT_HOST_ROOT%\bin\qsb.exe"
  exit /b 1
)
set "PATH=%MINGW_ROOT%\bin;%QT_X64_ROOT%\bin;%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
cmake -S . -B build-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_X64_ROOT%" -DQT_HOST_PATH="%QT_HOST_ROOT%" -DCMAKE_C_COMPILER="%MINGW_ROOT%\bin\gcc.exe" -DCMAKE_CXX_COMPILER="%MINGW_ROOT%\bin\g++.exe"
if not "%errorlevel%"=="0" exit /b 1
cmake --build build-x64 --parallel
if not "%errorlevel%"=="0" exit /b 1
if /i "%~1"=="build" exit /b 0
if /i "%~1"=="package" goto package
ctest --test-dir build-x64 --output-on-failure
if not "%errorlevel%"=="0" exit /b 1
:package
cmake --install build-x64 --prefix "%CD%\build-x64\dist"
if not "%errorlevel%"=="0" exit /b 1
if not exist "%CD%\build-x64\dist\bin\sameboy-qt.exe" (
  echo The installed x64 executable was not found.
  exit /b 1
)
for %%D in (libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll) do (
  if not exist "%MINGW_ROOT%\bin\%%D" (
    echo Required MinGW runtime was not found: "%%D"
    exit /b 1
  )
  copy /y "%MINGW_ROOT%\bin\%%D" "%CD%\build-x64\dist\bin\%%D" >nul
  if errorlevel 1 exit /b 1
)
exit /b 0

:initialize_vs
if not exist "%VS_BASE%\Common7\Tools\VsDevCmd.bat" (
  echo Visual Studio CMake and Ninja tools were not found.
  exit /b 1
)
call "%VS_BASE%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
exit /b %errorlevel%
