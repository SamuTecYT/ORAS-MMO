# 🖥️ Pokémon ORAS MMO — TCP Relay Server

Ultra-lightweight, zero-dependency asynchronous TCP relay server for **Pokémon Omega Ruby & Alpha Sapphire MMO v11.0 (Definitive Edition)**.

---

## ⚡ Quick Start

### Windows (1-Click):
Double-click `run_server.bat`. The script will:
1. Verify that Python 3 is installed.
2. Display your **Local IP** (for friends playing on the same Wi-Fi).
3. Display instructions for **ZeroTier** (for friends playing over the Internet).
4. Start listening on TCP port `9000`.

### Linux / macOS:
```bash
python3 server.py
```
*(Optionally set a custom port with `export PORT=9000`)*.

---

## ⚙️ Architecture & Protocol

* **Port:** `9000` (TCP, bidirectional).
* **Memory Footprint:** < 20 MB RAM, near 0% CPU usage.
* **Packet Structure:** Fixed 64-byte little-endian binary packets.
* **Rooms:** Multi-room architecture. Players in the same room code (e.g. `AZAH` or `1234`) are relayed to each other. Rooms are dynamically created and cleaned up when empty.
* **Capacity:** Up to 6 simultaneous players per room, with unlimited concurrent rooms.

---

## 🌐 Playing Over the Internet (ZeroTier)

For cross-network play without port forwarding:
1. Install and join a private virtual network using **ZeroTier One** (installer included in the `ZeroTier/` folder).
2. Authorize connected peers in the ZeroTier Central web dashboard.
3. Share the Host's **Managed IP** (e.g., `10.147.x.x`) with all players.
4. Each player inputs this Managed IP in the in-game MMO SELECT menu.
