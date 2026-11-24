@echo off
setlocal EnableExtensions DisableDelayedExpansion

rem Double-click: Release VST3 and Standalone. Optional: build.bat debug nopause
set "PROJECT_DIR=%~dp0"
if "%PROJECT_DIR:~-1%"=="\" set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
set "BUILD_DIR=%PROJECT_DIR%\build"
set "CONFIG=Release"
set "DO_PAUSE=1"

:arguments
if "%~1"=="" goto :configure
if /i "%~1"=="debug" (
    set "CONFIG=Debug"
    shift
    goto :arguments
)
if /i "%~1"=="release" (
    set "CONFIG=Release"
    shift
    goto :arguments
)
if /i "%~1"=="nopause" (
    set "DO_PAUSE=0"
    shift
    goto :arguments
)
echo [ERROR] Unknown argument: %~1
echo Usage: build.bat [release^|debug] [nopause]
goto :failed

:configure
echo.
echo ===== HPDG 808: VST3 + Standalone [%CONFIG%] =====
where cmake >nul 2>nul
if errorlevel 1 (
    echo [ERROR] CMake is missing from PATH. Install CMake and Visual Studio with C++ tools.
    goto :failed
)
if not exist "%PROJECT_DIR%\JUCE\CMakeLists.txt" (
    echo [ERROR] JUCE is missing from the project folder. Check the repository checkout.
    goto :failed
)
if exist "%BUILD_DIR%\CMakeCache.txt" goto :configure_existing
cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -A x64
if errorlevel 1 goto :failed
goto :build

:configure_existing
cmake -S "%PROJECT_DIR%" -B "%BUILD_DIR%"
if errorlevel 1 goto :failed

:build
cmake --build "%BUILD_DIR%" --config %CONFIG% --target HPDG808_VST3 HPDG808_Standalone --parallel 4
if errorlevel 1 goto :failed
if not exist "%BUILD_DIR%\HPDG808_artefacts\%CONFIG%\VST3\HPDG 808.vst3\Contents\x86_64-win\HPDG 808.vst3" goto :failed
if not exist "%BUILD_DIR%\HPDG808_artefacts\%CONFIG%\Standalone\HPDG 808.exe" goto :failed
echo.
echo ===== BUILD SUCCEEDED =====
echo VST3 folder: "%BUILD_DIR%\HPDG808_artefacts\%CONFIG%\VST3\HPDG 808.vst3"
echo Standalone:  "%BUILD_DIR%\HPDG808_artefacts\%CONFIG%\Standalone\HPDG 808.exe"
echo Copy the entire VST3 folder to your DAW's VST3 location when installing.
set "BUILD_RESULT=0"
goto :finish

:failed
echo.
echo ===== BUILD FAILED - see the error above =====
set "BUILD_RESULT=1"

:finish
if "%DO_PAUSE%"=="1" pause
exit /b %BUILD_RESULT%
