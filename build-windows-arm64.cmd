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
if not defined QT_ARM64_ROOT set "QT_ARM64_ROOT=%CD%\.tools\Qt\6.10.0\msvc2022_arm64"
if not defined QT_HOST_ROOT set "QT_HOST_ROOT=%CD%\.tools\Qt\6.10.0\msvc2022_64"
set "DIST_DIR=%CD%\dist\windows-arm64"
if not exist "%QT_ARM64_ROOT%\lib\cmake\Qt6\Qt6Config.cmake" (
  echo Qt 6 ARM64 was not found: "%QT_ARM64_ROOT%"
  exit /b 1
)
if not exist "%QT_HOST_ROOT%\bin\qsb.exe" (
  echo Qt Shader Tools host executable was not found: "%QT_HOST_ROOT%\bin\qsb.exe"
  exit /b 1
)
set "PATH=%VSINSTALLDIR%VC\Tools\Llvm\ARM64\bin;%VSINSTALLDIR%VC\Tools\Llvm\x64\bin;%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
set "LRELEASE_ARG="
if not exist "%QT_HOST_ROOT%\bin\lrelease.exe" if exist "%CD%\.tools\Qt\6.10.0\mingw_64\bin\lrelease.exe" set LRELEASE_ARG="-DSAMEBOY_LRELEASE=%CD%\.tools\Qt\6.10.0\mingw_64\bin\lrelease.exe"
rem Test programs are compiled only with "test".
set "BUILD_TESTING=OFF"
if /i "%~1"=="test" set "BUILD_TESTING=ON"
rem A build tree records the absolute path it was configured from (CMake caches, ninja and moc
rem dependency files). Reached through another path (a different drive letter, or the temporary
rem letter pushd gives a UNC path), CMake refuses it, so start it again from scratch.
if exist "build-windows-arm64\CMakeCache.txt" findstr /i /l /c:"CMAKE_CACHEFILE_DIR:INTERNAL=%CD:\=/%/build-windows-arm64" "build-windows-arm64\CMakeCache.txt" >nul || if exist "build-windows-arm64\CMakeCache.txt" rmdir /s /q "build-windows-arm64"
cmake --preset windows-arm64 %LRELEASE_ARG% -DBUILD_TESTING=%BUILD_TESTING%
if not "%errorlevel%"=="0" exit /b 1
cmake --build --preset windows-arm64
if not "%errorlevel%"=="0" exit /b 1
if /i "%~1"=="build" exit /b 0
rem Tests run only with "test"; by default the build is packaged directly.
set "TESTS_FAILED="
if /i not "%~1"=="test" goto package
set "PATH=%QT_ARM64_ROOT%\bin;%PATH%"
rem A failing test is reported at the end; it must not block packaging.
ctest --preset windows-arm64
if not "%errorlevel%"=="0" set "TESTS_FAILED=1"
:package
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
if exist "%DIST_DIR%" exit /b 1
cmake --install build-windows-arm64 --prefix "%DIST_DIR%"
if not "%errorlevel%"=="0" exit /b 1
if not exist "%DIST_DIR%\bin\sameboy-qt.exe" exit /b 1
echo Built: %DIST_DIR%\bin\sameboy-qt.exe
if defined TESTS_FAILED (
  echo WARNING: some tests FAILED, but the distribution was still produced. Review the test output above.
)
exit /b 0

:initialize_vs
if not exist "%VS_BASE%\Common7\Tools\VsDevCmd.bat" (
  echo Visual Studio C++ ARM64 tools were not found.
  exit /b 1
)
call "%VS_BASE%\Common7\Tools\VsDevCmd.bat" -arch=arm64 -host_arch=arm64
exit /b %errorlevel%
