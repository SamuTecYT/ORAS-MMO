# ORAS MMO — relay server / servidor

`server.py` is a single-file **asyncio TCP relay** (Python 3.8+, standard library only). It groups players into rooms
(up to 12), relays their 64-byte packets and keeps a ranking per room on disk. Protocol: [docs/PROTOCOL.md](../docs/PROTOCOL.md).

*Español más abajo → [Guía en español](#guía-en-español).*

## Run it

```bash
python3 server.py                     # TCP 9000
PORT=9100 python3 server.py           # another port
```
On Windows you can double-click `run_server.bat` (it also shows your local IP).

Players then point the plugin to you: **SELECT → Connection → Server**, or a `mmo_server.txt` next to the plugin with
`IP` or `IP:port`. On the same PC as Azahar the default `127.0.0.1` already works.

## Make it reachable

- **Same Wi-Fi**: use the PC's local IP (shown by `run_server.bat`).
- **Over the Internet**: forward **TCP 9000** on your router to that PC, or run it on a VPS / cloud VM and open
  TCP 9000 in its firewall (e.g. `sudo ufw allow 9000/tcp`) and in the provider's panel.
- The plugin connects to an **IPv4 address** (no domain names).
- **No VPS and no open ports?** Run it on the host's PC and join everyone through a free virtual network
  (**ZeroTier**, **Tailscale**, Radmin VPN): step by step in the main README →
  [Play online without a rented server](../README.md#play-online-without-a-rented-server).
  Allow Python (or TCP 9000) in the Windows firewall on **public** networks too: virtual networks often count as public.

## Docker

```bash
cp .env.example .env                          # edit it
docker compose up -d                          # relay only
docker compose --profile azahar-room up -d    # + Azahar network room (battles/trades), needs AZAHAR_ROOM_PASSWORD
```
The ranking is kept in the `oras-rank` volume.

## Keep it running (systemd example)

```ini
# /etc/systemd/system/oras-mmo.service
[Unit]
Description=ORAS MMO relay
After=network-online.target

[Service]
User=oras
WorkingDirectory=/opt/oras-mmo
Environment=PORT=9000 ORAS_RANK_DIR=/var/lib/oras-mmo
ExecStart=/usr/bin/python3 /opt/oras-mmo/server.py
Restart=always

[Install]
WantedBy=multi-user.target
```
`sudo systemctl enable --now oras-mmo` (PM2 works too: `pm2 start server.py --interpreter python3 --name oras-mmo`).
Run it as an unprivileged user, never as root.

## Settings (environment variables)

| Variable | Default | What it does |
|---|---|---|
| `PORT` | `9000` | TCP port |
| `ORAS_MAX_PLAYERS` | `12` | players per room (the plugin supports 12) |
| `ORAS_IDLE_TIMEOUT` | `60` | seconds without a packet before freeing a slot |
| `ORAS_QUEUE_MAX` | `256` | pending packets per client (old positions are dropped first) |
| `ORAS_MAX_CLIENTS` | `300` | open connections in total |
| `ORAS_MAX_PER_IP` | `16` | open connections from one IP |
| `ORAS_MAX_ROOMS` | `100` | rooms open at the same time |
| `ORAS_RANK_MAX_ROOMS` | `200` | different rooms with a saved ranking |
| `ORAS_MAX_PPS` | `120` | packets per second one player may send (the plugin sends ~30); extra ones are dropped |
| `ORAS_BURST` | `240` | short burst allowed above that rate |
| `ORAS_ROOMS` | *(empty)* | comma-separated allow-list of room codes (private server) |
| `ORAS_RANK_DIR` | server folder | where `ranking_<hex room>.json` files are saved |
| `ORAS_RANK_SAVE` | `30` | seconds between ranking saves |

## Azahar network room (optional)

Real in-game battles and trades go through Azahar's own multiplayer room (UDP 24872). Host one with
`AZAHAR_ROOM_PASSWORD='your-password' ./start_azahar_room.sh` (Docker) or the `azahar-room` compose profile, open
**UDP 24872**, and players use **Multiplayer → Direct connect → your IP : 24872**.

## Security notes

- No accounts: the **room code** is the only thing that separates groups. Use a private one, or `ORAS_ROOMS`.
- Ranking files contain the nicknames and stats of your players: don't publish them.
- The server logs connecting IP addresses (useful to spot abuse); treat logs as private.
- Tests: `python -m unittest test_server_ranking.py test_server_limits.py`.

---

## Guía en español

`server.py` es un **servidor TCP** de un solo archivo (Python 3.8+, sin dependencias). Agrupa a los jugadores en salas
(hasta 12), reenvía sus paquetes de 64 bytes y guarda el ranking de cada sala en disco.

**Arrancar:** `python3 server.py` (TCP 9000) o `PORT=9100 python3 server.py`. En Windows, doble clic en
`run_server.bat` (te enseña también tu IP local). Los jugadores ponen tu dirección en **SELECT → Conexión → Servidor**
o en un `mmo_server.txt` junto al plugin (`IP` o `IP:puerto`). En el mismo PC que Azahar ya vale `127.0.0.1`.

**Que te encuentren:**
- Misma Wi-Fi: la IP local del PC.
- Por Internet: abre el **TCP 9000** en tu router hacia ese PC, o usa un VPS y abre el TCP 9000 en su firewall
  (`sudo ufw allow 9000/tcp`) y en el panel del proveedor.
- El plugin se conecta a una **dirección IPv4** (no a nombres de dominio).
- **¿Sin VPS ni puertos abiertos?** Ábrelo en el PC del anfitrión y uníos con una red virtual gratuita (**ZeroTier**,
  **Tailscale**, Radmin VPN), paso a paso en el README en español →
  [Jugar por Internet sin servidor contratado](../README_ES.md#jugar-por-internet-sin-servidor-contratado).
  Permite Python (o el TCP 9000) en el firewall de Windows también en redes **públicas**: las redes virtuales suelen
  contar como públicas.

**Docker:** `cp .env.example .env`, edítalo y `docker compose up -d` (solo el servidor) o
`docker compose --profile azahar-room up -d` (también la sala de Azahar; necesita `AZAHAR_ROOM_PASSWORD`).

**Siempre encendido:** usa el ejemplo de systemd de arriba o PM2. Nunca como root.

**Ajustes:** la tabla de variables de arriba (puerto, jugadores por sala, límites contra saturación, paquetes por
segundo de cada jugador con `ORAS_MAX_PPS`, salas permitidas con `ORAS_ROOMS`, carpeta del ranking).

**Sala de Azahar (opcional):** combates e intercambios reales por la sala de red de Azahar (UDP 24872):
`AZAHAR_ROOM_PASSWORD='tu-clave' ./start_azahar_room.sh`, abre el **UDP 24872**, y los jugadores entran por
**Multijugador → Conectarse directamente → tu IP : 24872**.

**Seguridad:** no hay cuentas, así que el **código de sala** es lo que separa a cada grupo: usa uno propio o
`ORAS_ROOMS`. Los archivos `ranking_*.json` llevan apodos y datos de tus jugadores, y el registro guarda sus IP:
no los publiques.
