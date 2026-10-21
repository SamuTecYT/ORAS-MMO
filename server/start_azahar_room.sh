#!/usr/bin/env bash
# ==============================================================================
# Sala de red de Azahar/Citra 24/7 (combates e intercambios reales del juego) con Docker.
# Azahar/Citra network room 24/7 (real in-game battles and trades) with Docker.
#
# Uso / Usage:
#   AZAHAR_ROOM_PASSWORD='tu-clave' ./start_azahar_room.sh
# Opcional / Optional: AZAHAR_ROOM_NAME, AZAHAR_ROOM_DESC, AZAHAR_ROOM_PORT (24872), AZAHAR_ROOM_MAX_MEMBERS (16)
# ==============================================================================
set -euo pipefail

ROOM_NAME="${AZAHAR_ROOM_NAME:-Pokemon ORAS MMO}"
ROOM_DESC="${AZAHAR_ROOM_DESC:-Combates e intercambios / Battles and trades}"
ROOM_PORT="${AZAHAR_ROOM_PORT:-24872}"
MAX_MEMBERS="${AZAHAR_ROOM_MAX_MEMBERS:-16}"
ROOM_PASS="${AZAHAR_ROOM_PASSWORD:-}"

if [ -z "$ROOM_PASS" ]; then
    echo "[ERROR] Falta la clave de la sala / Room password missing:"
    echo "        AZAHAR_ROOM_PASSWORD='tu-clave' $0"
    exit 1
fi
if ! command -v docker >/dev/null 2>&1; then
    echo "[ERROR] Docker no está instalado / Docker is not installed: https://docs.docker.com/engine/install/"
    exit 1
fi

if command -v ufw >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
    sudo ufw allow "${ROOM_PORT}/udp" >/dev/null 2>&1 || true
fi

docker rm -f azahar-room >/dev/null 2>&1 || true
docker run -d \
  --name azahar-room \
  --restart unless-stopped \
  -p "${ROOM_PORT}:${ROOM_PORT}/udp" \
  -e CITRA_ROOM_NAME="${ROOM_NAME}" \
  -e CITRA_ROOM_DESC="${ROOM_DESC}" \
  -e CITRA_ROOM_PORT="${ROOM_PORT}" \
  -e CITRA_ROOM_MAX_MEMBERS="${MAX_MEMBERS}" \
  -e CITRA_ROOM_PASSWORD="${ROOM_PASS}" \
  k4rian/citra-room:latest >/dev/null

echo "[OK] Sala en marcha / Room running: UDP ${ROOM_PORT}."
echo "     En Azahar / In Azahar: Multiplayer > Direct Connect > <IP de tu servidor / your server IP>:${ROOM_PORT}"
echo "     Abre el puerto ${ROOM_PORT}/udp en el panel de tu hosting / Open ${ROOM_PORT}/udp in your hosting panel."
