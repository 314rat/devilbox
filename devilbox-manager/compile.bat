@echo off
setlocal EnableDelayedExpansion

:: Set variables
set "SOURCE=main.cpp"
set "OUTPUT=DevilboxManager.exe"
set "COMPILER=g++"
set "LIBS=-lole32 -lcomctl32 -lshell32"
set "FLAGS=-mwindows"

:: Check if source exists
if not exist "%SOURCE%" (
    echo Error: Source file %SOURCE% not found!
    pause
    exit /b 1
)

:: Try to kill running process
taskkill /F /IM "%OUTPUT%" >nul 2>&1

:: Wait a moment to ensure process is terminated
timeout /t 1 /nobreak >nul

:: Remove old executable if exists
if exist "%OUTPUT%" (
    del "%OUTPUT%" 2>nul
    if exist "%OUTPUT%" (
        echo Error: Could not delete old executable! Make sure the program is not running.
        echo Try manually closing DevilboxManager from the system tray.
        pause
        exit /b 1
    )
)

echo Building %SOURCE% ...
%COMPILER% %SOURCE% -o %OUTPUT% %LIBS% %FLAGS%

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Compilation successful!
    echo Output file: %OUTPUT%
    echo.
    echo Starting application...
    start "" "%OUTPUT%"
) else (
    echo.
    echo Compilation failed with error code %ERRORLEVEL%!
    pause
    exit /b 1
)

endlocal