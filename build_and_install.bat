@echo off
rem Builds MinecraftSA.asi and copies it with its textures into the game folder.
setlocal
cd /d "%~dp0"
set GTA=C:\Users\Anonymous\Desktop\GTA San Andreas

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
echo Kurulum tamam: %GTA%
exit /b 0

:err
echo.
echo HATA: derleme ya da kopyalama basarisiz oldu.
exit /b 1
