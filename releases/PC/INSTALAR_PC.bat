@echo off
color 0B
title Project ORAS MMO — Configuración Express

echo ========================================================
echo       PROJECT ORAS MMO — Instalador Automático v3
echo ========================================================
echo.
echo Este script instala el plugin y configura tu IP.
echo.

:: ── Detectar emulador ──────────────────────────────────────
set "EMU_PATH="
set "EMU_NAME="

if exist "%APPDATA%\Azahar" (
    set "EMU_PATH=%APPDATA%\Azahar"
    set "EMU_NAME=Azahar"
)
if exist "%APPDATA%\Citra" (
    set "EMU_PATH=%APPDATA%\Citra"
    set "EMU_NAME=Citra"
)

if "%EMU_PATH%"=="" (
    color 0C
    echo [ERROR] No se encontró Citra ni Azahar.
    echo Instala el emulador primero.
    pause & exit /b
)

echo [OK] Emulador: %EMU_NAME%
echo.

:: ── Instalar plugin ────────────────────────────────────────
set "SDMC=%EMU_PATH%\sdmc"
set "PLUG_PATH=%SDMC%\luma\plugins"

mkdir "%PLUG_PATH%\000400000011C400" 2>nul
mkdir "%PLUG_PATH%\000400000011C500" 2>nul

if not exist "%~dp0OrasMmo.3gx" (
    color 0C
    echo [ERROR] No se encontro OrasMmo.3gx junto a este archivo.
    echo.
    echo ^> Parece que estas abriendo esto directamente desde el .zip
    echo ^> Por favor, EXTRAE TODO EL ARCHIVO ZIP en una carpeta normal primero.
    echo ^> Dale clic derecho al .zip -^> Extraer todo... y luego abre el instalador.
    pause & exit /b
)

copy /y "%~dp0OrasMmo.3gx" "%PLUG_PATH%\000400000011C400\OrasMmo.3gx" >nul
copy /y "%~dp0OrasMmo.3gx" "%PLUG_PATH%\000400000011C400\plugin.3gx" >nul
copy /y "%~dp0OrasMmo.3gx" "%PLUG_PATH%\000400000011C500\OrasMmo.3gx" >nul
copy /y "%~dp0OrasMmo.3gx" "%PLUG_PATH%\000400000011C500\plugin.3gx" >nul
echo [OK] Plugin instalado.

:: ── Configurar IP ──────────────────────────────────────────
echo.
echo ════════════════════════════════════════
echo  CONFIGURACIÓN DE IP
echo ════════════════════════════════════════
echo.
echo OPCIONES:
echo   [1] Jugar en la MISMA red Wi-Fi (red local)
echo   [2] Jugar por INTERNET con ZeroTier (Recomendado)
echo   [3] Soy el HOST (servidor en mi PC)
echo.
set /p OPCION="Elige una opción (1/2/3): "

if "%OPCION%"=="3" (
    set "SERVER_IP=127.0.0.1"
    echo.
    echo [INFO] Configurado como HOST (127.0.0.1)
    echo [INFO] Ejecuta run_server.bat para iniciar el servidor.
    goto save_ip
)

if "%OPCION%"=="1" (
    echo.
    echo [INFO] Pide al HOST su IP local (aparece en run_server.bat)
    set /p SERVER_IP="IP del HOST en tu red Wi-Fi: "
    goto save_ip
)

if "%OPCION%"=="2" (
    echo.
    echo [INFO] ZeroTier: únete a la misma red ZeroTier que el HOST.
    echo        Pídele al HOST su "Managed IP" de ZeroTier (ej. 10.147.x.x o 192.168.19x.x)
    set /p SERVER_IP="IP del HOST en ZeroTier: "
    goto save_ip
)

:: Opción inválida
echo [WARN] Opción inválida, usando localhost.
set "SERVER_IP=127.0.0.1"

:save_ip
if not defined SERVER_IP set "SERVER_IP=127.0.0.1"
if "%SERVER_IP%"=="" set "SERVER_IP=127.0.0.1"
echo %SERVER_IP%> "%SDMC%\mmo_ip.txt"
echo %SERVER_IP%> "%PLUG_PATH%\000400000011C400\mmo_ip.txt"
echo %SERVER_IP%> "%PLUG_PATH%\000400000011C500\mmo_ip.txt"
echo.
echo [OK] IP guardada: %SERVER_IP%

:: ── Finalizar ─────────────────────────────────────────────
color 0A
echo.
echo ========================================================
echo  ¡Instalación completa!
echo ========================================================
echo.
echo Próximos pasos:
echo  1. Abre %EMU_NAME% y carga Pokémon ORAS
echo  2. Presiona SELECT para abrir el menú MMO
echo  3. Puedes cambiar la IP dentro del menú en cualquier
echo     momento sin reinstalar nada.
echo.
echo NOTA: También puedes cambiar la IP directamente
echo       en el menú del juego (SELECT > Configuración > IP)
echo.
pause
