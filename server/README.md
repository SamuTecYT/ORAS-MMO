<a id="english"></a>
# 🖥️ ORAS MMO server

**English** · [Español](#espanol)

`server.py` is the server that connects the players. It is **one Python file** with **no dependencies**
(Python 3.8+). It puts players into rooms of up to 12, passes their messages along and keeps a ranking per room.
Technical details: [docs/PROTOCOL.md](../docs/PROTOCOL.md).

## 🚀 Start it

| | How |
|:---:|---|
| 🪟 **Windows** | Install [Python 3](https://www.python.org/downloads/) (tick ✅ *Add Python to PATH*) and double-click `run_server.bat`. It also shows your local IP. |
| 🐧 **Linux / macOS** | `python3 server.py` (TCP 9000), or `PORT=9100 python3 server.py` for another port |
| 🐳 **Docker** | `cp .env.example .env`, edit it, then `docker compose up -d` ([more below](#docker)) |

Then every player points the plugin to you: **SELECT → Connection → Server**, or a `mmo_server.txt` next to the
plugin with `IP` or `IP:port`. On the same PC as Azahar, the default `127.0.0.1` already works.

> [!TIP]
> Leave the window open while you play: it shows a line for every player who joins.

## 🌍 Let your friends in

| Where are your friends? | What to do |
|---|---|
| 🏠 **Same Wi-Fi** | Give them the PC's local IP (shown by `run_server.bat`). |
| 🌐 **Anywhere, no paid server** | Use a free virtual network (**ZeroTier**, **Tailscale**, Radmin VPN): step by step in the main README → [Play with friends far away](../README.md#play-with-friends-far-away). |
| 📡 **Anywhere, with your router** | Forward **TCP 9000** on your router to this PC and give them your public IP. |
| ☁️ **On a VPS / cloud server** | Open TCP 9000 in its firewall (for example `sudo ufw allow 9000/tcp`) and in the provider's panel. |

> [!NOTE]
> The plugin connects to an **IPv4 address**, not to domain names. On Windows, allow Python (or TCP 9000) in the
> firewall on **public** networks too: virtual networks often count as public.

<a id="docker"></a>
## 🐳 Docker

```bash
cp .env.example .env                          # edit it
docker compose up -d                          # the server only
docker compose --profile azahar-room up -d    # + Azahar room (battles/trades), needs AZAHAR_ROOM_PASSWORD
```

The ranking is kept in the `oras-rank` volume.

## 🔁 Keep it running

<details>
<summary><b>systemd</b> (Linux)</summary>

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

Then: `sudo systemctl enable --now oras-mmo`

</details>

<details>
<summary><b>PM2</b></summary>

```bash
pm2 start server.py --interpreter python3 --name oras-mmo
```

</details>

> [!WARNING]
> Run it as a normal user, never as root.

## ⚙️ Settings (environment variables)

| Variable | Default | What it does |
|---|---|---|
| `PORT` | `9000` | TCP port |
| `ORAS_MAX_PLAYERS` | `12` | Players per room (the plugin supports 12) |
| `ORAS_IDLE_TIMEOUT` | `60` | Seconds without news before a player's slot is freed |
| `ORAS_QUEUE_MAX` | `256` | Pending packets per player (old positions are dropped first) |
| `ORAS_MAX_CLIENTS` | `300` | Open connections in total |
| `ORAS_MAX_PER_IP` | `16` | Open connections from one IP |
| `ORAS_MAX_PER_IP_ROOM` | `4` | Players from one IP in the same room (so one IP can't fill a room) |
| `ORAS_ROOM_TRIES` | `5` | Different room codes one IP may try per window (stops guessing other groups' codes) |
| `ORAS_ROOM_TRIES_WINDOW` | `600` | That window, in seconds |
| `ORAS_MAX_ROOMS` | `100` | Rooms open at the same time |
| `ORAS_RANK_MAX_ROOMS` | `200` | Different rooms with a saved ranking |
| `ORAS_MAX_PPS` | `120` | Packets per second one player may send (the plugin sends ~30); extra ones are dropped |
| `ORAS_BURST` | `240` | Short burst allowed above that rate |
| `ORAS_ROOMS` | *(empty)* | Comma-separated list of the only room codes allowed (private server) |
| `ORAS_RANK_DIR` | server folder | Where the `ranking_<hex room>.json` files are saved |
| `ORAS_RANK_SAVE` | `30` | Seconds between ranking saves |

## ⚔️ Azahar room (optional)

Real battles and trades go through Azahar's own multiplayer room (UDP 24872). Host one with
`AZAHAR_ROOM_PASSWORD='your-password' ./start_azahar_room.sh` (Docker) or the `azahar-room` compose profile, open
**UDP 24872**, and players use **Multiplayer → Direct Connect → your IP : 24872**.

## 🔒 Security notes

- 🔑 There are no accounts: the **room code** is the only thing that separates groups. Use a private one, or
  `ORAS_ROOMS`.
- 🧱 Connections from this same computer (`127.x`) don't count for the per-room and room-code limits, so you can
  test with local bots.
- 📄 Ranking files contain your players' nicknames and stats: don't publish them.
- 📝 The server logs the IP addresses that connect (useful to spot abuse): keep the logs private.
- 🧪 Tests: `python -m unittest test_server_ranking.py test_server_limits.py`.

---

<a id="espanol"></a>
# 🖥️ Servidor de ORAS MMO

[English](#english) · **Español**

`server.py` es el servidor que conecta a los jugadores. Es **un solo archivo de Python**, **sin dependencias**
(Python 3.8+). Reparte a los jugadores en salas de hasta 12, pasa sus mensajes y guarda un ranking por sala.
Detalles técnicos: [docs/PROTOCOL.md](../docs/PROTOCOL.md).

## 🚀 Arrancarlo

| | Cómo |
|:---:|---|
| 🪟 **Windows** | Instala [Python 3](https://www.python.org/downloads/) (marca ✅ *Add Python to PATH*) y haz doble clic en `run_server.bat`. También te enseña tu IP local. |
| 🐧 **Linux / macOS** | `python3 server.py` (TCP 9000), o `PORT=9100 python3 server.py` para otro puerto |
| 🐳 **Docker** | `cp .env.example .env`, edítalo y `docker compose up -d` ([más abajo](#docker-es)) |

Después, cada jugador pone tu dirección en el plugin: **SELECT → Conexión → Servidor**, o un `mmo_server.txt` junto
al plugin con `IP` o `IP:puerto`. En el mismo PC que Azahar ya vale el `127.0.0.1` por defecto.

> [!TIP]
> Deja la ventana abierta mientras juegan: muestra una línea por cada jugador que entra.

## 🌍 Deja entrar a tus amigos

| ¿Dónde están tus amigos? | Qué hacer |
|---|---|
| 🏠 **En la misma Wi-Fi** | Dales la IP local del PC (la enseña `run_server.bat`). |
| 🌐 **En cualquier parte, sin pagar servidor** | Usa una red virtual gratuita (**ZeroTier**, **Tailscale**, Radmin VPN): paso a paso en el README → [Jugar con amigos lejos](../README_ES.md#amigos-lejos). |
| 📡 **En cualquier parte, con tu router** | Redirige el **TCP 9000** de tu router a este PC y dales tu IP pública. |
| ☁️ **En un VPS o servidor en la nube** | Abre el TCP 9000 en su firewall (por ejemplo `sudo ufw allow 9000/tcp`) y en el panel del proveedor. |

> [!NOTE]
> El plugin se conecta a una **dirección IPv4**, no a nombres de dominio. En Windows, permite Python (o el TCP 9000)
> en el firewall también en redes **públicas**: las redes virtuales suelen contar como públicas.

<a id="docker-es"></a>
## 🐳 Docker

```bash
cp .env.example .env                          # edítalo
docker compose up -d                          # solo el servidor
docker compose --profile azahar-room up -d    # + sala de Azahar (combates), necesita AZAHAR_ROOM_PASSWORD
```

El ranking se guarda en el volumen `oras-rank`.

## 🔁 Que siga encendido

Usa el ejemplo de **systemd** o **PM2** de la [parte en inglés](#english) (los comandos son los mismos).

> [!WARNING]
> Arráncalo con un usuario normal, nunca como root.

## ⚙️ Ajustes (variables de entorno)

| Variable | Por defecto | Qué hace |
|---|---|---|
| `PORT` | `9000` | Puerto TCP |
| `ORAS_MAX_PLAYERS` | `12` | Jugadores por sala (el plugin admite 12) |
| `ORAS_IDLE_TIMEOUT` | `60` | Segundos sin noticias antes de liberar el hueco de un jugador |
| `ORAS_QUEUE_MAX` | `256` | Paquetes pendientes por jugador (primero se descartan las posiciones viejas) |
| `ORAS_MAX_CLIENTS` | `300` | Conexiones abiertas en total |
| `ORAS_MAX_PER_IP` | `16` | Conexiones abiertas desde una misma IP |
| `ORAS_MAX_PER_IP_ROOM` | `4` | Jugadores de una misma IP en la misma sala (así una IP no llena una sala) |
| `ORAS_ROOM_TRIES` | `5` | Códigos de sala distintos que puede probar una IP en esa ventana (evita adivinar el de otros) |
| `ORAS_ROOM_TRIES_WINDOW` | `600` | Esa ventana, en segundos |
| `ORAS_MAX_ROOMS` | `100` | Salas abiertas a la vez |
| `ORAS_RANK_MAX_ROOMS` | `200` | Salas distintas con ranking guardado |
| `ORAS_MAX_PPS` | `120` | Paquetes por segundo que puede mandar un jugador (el plugin manda ~30); los de más se descartan |
| `ORAS_BURST` | `240` | Ráfaga corta permitida por encima de ese ritmo |
| `ORAS_ROOMS` | *(vacío)* | Lista, separada por comas, de los únicos códigos de sala permitidos (servidor privado) |
| `ORAS_RANK_DIR` | carpeta del servidor | Dónde se guardan los archivos `ranking_<sala en hex>.json` |
| `ORAS_RANK_SAVE` | `30` | Segundos entre cada guardado del ranking |

## ⚔️ Sala de Azahar (opcional)

Los combates e intercambios de verdad van por la sala multijugador de Azahar (UDP 24872). Ábrela con
`AZAHAR_ROOM_PASSWORD='tu-clave' ./start_azahar_room.sh` (Docker) o con el perfil `azahar-room` de compose, abre el
**UDP 24872**, y los jugadores entran por **Multijugador → Conectarse directamente → tu IP : 24872**.

## 🔒 Seguridad

- 🔑 No hay cuentas: el **código de sala** es lo único que separa a cada grupo. Usa uno privado, u `ORAS_ROOMS`.
- 🧱 Las conexiones desde este mismo equipo (`127.x`) no cuentan para los límites por sala y de códigos, para que
  puedas probar con bots locales.
- 📄 Los archivos de ranking llevan los apodos y datos de tus jugadores: no los publiques.
- 📝 El servidor apunta las IP que se conectan (sirve para detectar abusos): guarda el registro en privado.
- 🧪 Pruebas: `python -m unittest test_server_ranking.py test_server_limits.py`.
