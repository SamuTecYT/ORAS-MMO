@echo off
chcp 65001 >nul
setlocal
color 0B
title Pokemon ORAS MMO - Instalador / Installer

echo ========================================================
echo        POKEMON ORAS MMO  -  Instalador / Installer (PC)
echo ========================================================
echo.

:: ---- Buscar el emulador / Find the emulator (Azahar, then Citra) ----------
set "EMU_PATH="
set "EMU_NAME="
if exist "%APPDATA%\Citra" (
    set "EMU_PATH=%APPDATA%\Citra"
    set "EMU_NAME=Citra"
)
if exist "%APPDATA%\Azahar" (
    set "EMU_PATH=%APPDATA%\Azahar"
    set "EMU_NAME=Azahar"
)
if "%EMU_PATH%"=="" (
    color 0C
    echo [ERROR] No se encontro Azahar / Azahar not found.
    echo         Instala Azahar y abrelo una vez / Install Azahar and open it once.
    echo.
    pause
    exit /b 1
)
echo [OK] %EMU_NAME%: %EMU_PATH%

if not exist "%~dp0OrasMmo.3gx" (
    color 0C
    echo.
    echo [ERROR] Falta OrasMmo.3gx junto a este archivo / OrasMmo.3gx missing next to this file.
    echo         Extrae el .zip primero / Extract the .zip first ^(right click - Extract all^).
    echo.
    pause
    exit /b 1
)

:: ---- Servidor / Server --------------------------------------------------------
echo.
echo Direccion del servidor / Server address: IP o/or IP:puerto/port  (ej./e.g. 203.0.113.10:9000)
echo Vacio = 127.0.0.1 (servidor en este PC) / Empty = 127.0.0.1 (server on this PC)
set "SERVER="
set /p "SERVER=> "
:: Solo numeros, puntos y ":" (nada que Windows pueda interpretar como una orden)
:: Digits, dots and ":" only (nothing Windows could run as a command)
if defined SERVER set "SERVER=%SERVER:"=%"
if defined SERVER for /f "delims=0123456789.:" %%A in ("%SERVER%") do (
    echo [!] Direccion no valida, se usara 127.0.0.1 / Invalid address, using 127.0.0.1
    set "SERVER="
)

:: ---- Instalar / Install -------------------------------------------------------
set "PLUG=%EMU_PATH%\sdmc\luma\plugins"
mkdir "%PLUG%\000400000011C400" 2>nul
mkdir "%PLUG%\000400000011C500" 2>nul

set "FAIL=0"
for %%D in ("%PLUG%\000400000011C400" "%PLUG%\000400000011C500") do (
    copy /y "%~dp0OrasMmo.3gx" "%%~D\OrasMmo.3gx" >nul || set "FAIL=1"
    if exist "%%~D\plugin.3gx" copy /y "%~dp0OrasMmo.3gx" "%%~D\plugin.3gx" >nul
    if exist "%~dp0mmo_pokes_3d.bin" copy /y "%~dp0mmo_pokes_3d.bin" "%%~D\mmo_pokes_3d.bin" >nul
    if not "%SERVER%"=="" (echo %SERVER%)>"%%~D\mmo_server.txt"
)
if "%FAIL%"=="1" (
    color 0C
    echo [ERROR] No se pudo copiar el plugin. Cierra Azahar y repite / Could not copy: close Azahar and retry.
    pause
    exit /b 1
)
echo [OK] Plugin instalado / installed: Omega Ruby + Alpha Sapphire.
if not "%SERVER%"=="" echo [OK] Servidor / Server: %SERVER%

if exist "%~dp0shaders\ORAS_MMO_HD.glsl" (
    mkdir "%EMU_PATH%\shaders" 2>nul
    copy /y "%~dp0shaders\ORAS_MMO_HD.glsl" "%EMU_PATH%\shaders\ORAS_MMO_HD.glsl" >nul && echo [OK] Shader ORAS_MMO_HD ^(opcional / optional^).
)

color 0A
echo.
echo ========================================================
echo   Listo / Done
echo ========================================================
echo  1. Azahar: Emulacion/Emulation ^> Configurar/Configure ^> Sistema/System:
echo     activa el cargador de plugins 3GX / enable the 3GX plugin loader.
echo  2. Abre el juego y carga tu partida / Start the game and load your save.
echo  3. SELECT = menu del MMO / MMO menu. Sala y servidor / Room and server: SELECT ^> Conexion/Connection.
echo  4. Combates e intercambios / Battles and trades: Multiplayer ^> Direct connect (sala de Azahar del servidor).
echo.
pause
endlocal
