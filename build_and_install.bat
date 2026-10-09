@echo off
rem Builds MinecraftSA.asi and copies it with its textures and sounds into the game folder.
rem Usage: build_and_install.bat "C:\Games\GTA San Andreas"
rem        (or set the GTA_SA_DIR environment variable and run it without an argument)
setlocal
cd /d "%~dp0"
set "GTA=%~1"
if "%GTA%"=="" set "GTA=%GTA_SA_DIR%"
if "%GTA%"=="" (
    echo Usage: build_and_install.bat "path\to\GTA San Andreas"
    exit /b 2
)
if not exist "%GTA%\gta_sa.exe" (
    echo No gta_sa.exe in "%GTA%"
    exit /b 2
)

python tools\gen_assets.py || goto :err
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 || goto :err
cmake --build build --config Release --target MinecraftSA || goto :err

copy /Y build\Release\MinecraftSA.asi "%GTA%\" || goto :err
if not exist "%GTA%\MinecraftSA" mkdir "%GTA%\MinecraftSA"
copy /Y assets\*.png "%GTA%\MinecraftSA\" || goto :err
if not exist "%GTA%\MinecraftSA\sounds" mkdir "%GTA%\MinecraftSA\sounds"
copy /Y assets\sounds\*.ogg "%GTA%\MinecraftSA\sounds\" >nul || goto :err
copy /Y OKUBENI.txt "%GTA%\MinecraftSA\" >nul
echo.
echo Installed: %GTA%
exit /b 0

:err
echo.
echo ERROR: the build or the copy failed.
exit /b 1
