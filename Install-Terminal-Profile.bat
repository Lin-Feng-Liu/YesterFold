@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"

set "APP_DIR=%~dp0"
if "%APP_DIR:~-1%"=="\" set "APP_DIR=%APP_DIR:~0,-1%"

set "EXE_PATH=%APP_DIR%\YesterFold.exe"
set "SHADER_PATH="

if exist "%APP_DIR%\VisualPack\crt.hlsl" (
    set "SHADER_PATH=%APP_DIR%\VisualPack\crt.hlsl"
) else if exist "%APP_DIR%\res\shaders\crt.hlsl" (
    set "SHADER_PATH=%APP_DIR%\res\shaders\crt.hlsl"
)

if not exist "%EXE_PATH%" (
    echo ERROR: YesterFold.exe was not found.
    echo Expected: %EXE_PATH%
    echo.
    pause
    exit /b 1
)

if "%SHADER_PATH%"=="" (
    echo ERROR: crt.hlsl was not found.
    echo Expected one of:
    echo   %APP_DIR%\VisualPack\crt.hlsl
    echo   %APP_DIR%\res\shaders\crt.hlsl
    echo.
    pause
    exit /b 1
)

if "%LOCALAPPDATA%"=="" (
    echo ERROR: LOCALAPPDATA is not available.
    echo.
    pause
    exit /b 1
)

set "FRAGMENT_DIR=%LOCALAPPDATA%\Microsoft\Windows Terminal\Fragments\YesterFold"
set "FRAGMENT_FILE=%FRAGMENT_DIR%\YesterFold.json"

if not exist "%FRAGMENT_DIR%" mkdir "%FRAGMENT_DIR%"
if errorlevel 1 (
    echo ERROR: Failed to create Windows Terminal fragment directory.
    echo %FRAGMENT_DIR%
    echo.
    pause
    exit /b 1
)

set "JSON_EXE=%EXE_PATH:\=\\%"
set "JSON_SHADER=%SHADER_PATH:\=\\%"
set "JSON_APP_DIR=%APP_DIR:\=\\%"

> "%FRAGMENT_FILE%" echo {
>> "%FRAGMENT_FILE%" echo     "profiles": [
>> "%FRAGMENT_FILE%" echo         {
>> "%FRAGMENT_FILE%" echo             "name": "YesterFold",
>> "%FRAGMENT_FILE%" echo             "commandline": "%JSON_EXE%",
>> "%FRAGMENT_FILE%" echo             "startingDirectory": "%JSON_APP_DIR%",
>> "%FRAGMENT_FILE%" echo             "experimental.pixelShaderPath": "%JSON_SHADER%",
>> "%FRAGMENT_FILE%" echo             "guid": "{ed2a8050-47c5-4a13-8402-4416cbee81ce}",
>> "%FRAGMENT_FILE%" echo             "hidden": false
>> "%FRAGMENT_FILE%" echo         }
>> "%FRAGMENT_FILE%" echo     ]
>> "%FRAGMENT_FILE%" echo }

if errorlevel 1 (
    echo ERROR: Failed to write Windows Terminal fragment.
    echo %FRAGMENT_FILE%
    echo.
    pause
    exit /b 1
)

echo YesterFold Windows Terminal profile installed.
echo Fragment file:
echo %FRAGMENT_FILE%
echo.
echo Restart Windows Terminal, then open the YesterFold profile.

echo.
pause
