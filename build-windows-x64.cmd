@echo off
setlocal
rem cmd cannot use a UNC path (\\server\share) as its current directory; pushd maps it to a
rem temporary drive letter, which popd releases once the build has finished.
pushd "%~dp0" || exit /b 1
call :main %*
set "RESULT=%errorlevel%"
popd
exit /b %RESULT%

:main
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
rem ld cannot open the UNC paths gcc derives when MinGW sits on a mapped network or shared-folder drive, so prefer a local copy.
if not defined MINGW_ROOT if exist "C:\mingw1310_64\bin\gcc.exe" set "MINGW_ROOT=C:\mingw1310_64"
if not defined MINGW_ROOT set "MINGW_ROOT=%CD%\.tools\Qt\Tools\mingw1310_64"
set "DIST_DIR=%CD%\dist\windows-x64"
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
rem Test programs are compiled only with "test".
set "BUILD_TESTING=OFF"
if /i "%~1"=="test" set "BUILD_TESTING=ON"
rem A build tree records the absolute path it was configured from (CMake caches, ninja and moc
rem dependency files). Reached through another path (a different drive letter, or the temporary
rem letter pushd gives a UNC path), CMake refuses it, so start it again from scratch.
if exist "build-windows-x64\CMakeCache.txt" findstr /i /l /c:"CMAKE_CACHEFILE_DIR:INTERNAL=%CD:\=/%/build-windows-x64" "build-windows-x64\CMakeCache.txt" >nul || if exist "build-windows-x64\CMakeCache.txt" rmdir /s /q "build-windows-x64"
cmake -S . -B build-windows-x64 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_X64_ROOT%" -DQT_HOST_PATH="%QT_HOST_ROOT%" -DCMAKE_C_COMPILER="%MINGW_ROOT%\bin\gcc.exe" -DCMAKE_CXX_COMPILER="%MINGW_ROOT%\bin\g++.exe" -DBUILD_TESTING=%BUILD_TESTING%
if not "%errorlevel%"=="0" exit /b 1
cmake --build build-windows-x64 --parallel
if not "%errorlevel%"=="0" exit /b 1
if /i "%~1"=="build" exit /b 0
rem Tests run only with "test"; by default the build is packaged directly.
set "TESTS_FAILED="
if /i not "%~1"=="test" goto package
rem A failing test is reported at the end; it must not block packaging.
ctest --test-dir build-windows-x64 --output-on-failure
if not "%errorlevel%"=="0" set "TESTS_FAILED=1"
:package
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
if exist "%DIST_DIR%" exit /b 1
cmake --install build-windows-x64 --prefix "%DIST_DIR%"
if not "%errorlevel%"=="0" exit /b 1
if not exist "%DIST_DIR%\bin\sameboy-qt.exe" (
  echo The installed x64 executable was not found.
  exit /b 1
)
for %%D in (libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll) do (
  if not exist "%MINGW_ROOT%\bin\%%D" (
    echo Required MinGW runtime was not found: "%%D"
    exit /b 1
  )
  copy /y "%MINGW_ROOT%\bin\%%D" "%DIST_DIR%\bin\%%D" >nul
  if errorlevel 1 exit /b 1
)
echo Built: %DIST_DIR%\bin\sameboy-qt.exe
if defined TESTS_FAILED (
  echo WARNING: some tests FAILED, but the distribution was still produced. Review the test output above.
)
exit /b 0

:initialize_vs
if not exist "%VS_BASE%\Common7\Tools\VsDevCmd.bat" (
  echo Visual Studio CMake and Ninja tools were not found.
  exit /b 1
)
call "%VS_BASE%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
exit /b %errorlevel%
