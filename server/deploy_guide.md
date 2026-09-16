# Project ORAS MMO — Free Deployment Guide

Deploy your relay server for **free** on Railway or Render — no credit card required for the hobby tier.

---

## Option A: Railway.app (Recommended — fastest, ~30 s deploy)

### Prerequisites
- A [Railway account](https://railway.app) (sign up with GitHub)
- [Railway CLI](https://docs.railway.app/develop/cli) installed **OR** a GitHub repo

---

### Method 1: Railway CLI (no repo needed)

```bash
# 1. Install the CLI (once)
npm install -g @railway/cli

# 2. Login
railway login

# 3. Go to your server folder
cd C:\Users\Tuf Gaming\.gemini\antigravity\brain\aa753652-cac6-4914-a420-dab19cc7b0bd\server

# 4. Create a new Railway project
railway init

# 5. Deploy!
railway up
```

Railway auto-detects Python via `requirements.txt` and starts with:
```
python server.py
```

---

### Method 2: GitHub → Railway (recommended for updates)

1. Push the `server/` folder to a GitHub repository.
2. Go to [railway.app/new](https://railway.app/new) → **Deploy from GitHub Repo**.
3. Select your repo.
4. Railway builds and deploys automatically on every `git push`.

---

### Set Environment Variables on Railway

In your Railway project dashboard → **Variables** tab, add:

| Variable   | Value  | Notes                          |
|------------|--------|--------------------------------|
| `PORT`     | `8765` | Railway auto-assigns, but set anyway |
| `MAX_ROOMS`| `100`  | Increase if needed              |
| `LOG_LEVEL`| `INFO` | Use `DEBUG` for troubleshooting |

> **Note:** Railway provides `$PORT` automatically. The server already reads it from the environment.

---

### Get Your Public URL

After deploy → **Settings** tab → copy the generated domain:
```
wss://your-project-name.up.railway.app
```

Update your game client to connect to that URL instead of `localhost`.

---

## Option B: Render.com

### Steps

1. Push `server/` to GitHub (same as above).
2. Go to [render.com](https://render.com) → **New** → **Web Service**.
3. Connect your GitHub repo.
4. Fill in:

   | Field            | Value                    |
   |------------------|--------------------------|
   | **Runtime**      | Python 3                 |
   | **Build Command**| `pip install -r requirements.txt` |
   | **Start Command**| `python server.py`       |
   | **Plan**         | Free                     |

5. Click **Create Web Service**.

### Environment Variables on Render

Dashboard → **Environment** tab:

| Key        | Value |
|------------|-------|
| `PORT`     | `10000` (Render assigns this automatically) |
| `MAX_ROOMS`| `100` |

> **Important:** Render free tier **spins down after 15 minutes of inactivity**. For a game server, upgrade to the $7/mo Starter plan or use Railway instead.

---

## Procfile (Optional — helps Railway/Render detect start command)

Create a `Procfile` in the same folder:
```
web: python server.py
```

---

## Local LAN Testing (No Cloud)

```bat
# Windows — just double-click:
run_server.bat

# Or manually:
pip install -r requirements.txt
python server.py
```

Then run the test client in a second terminal:
```bat
python test_client.py --host localhost --port 8765 --room TEST
```

---

## Connecting Game Clients

Update your GBA emulator plugin / client to use:

| Environment  | WebSocket URL                              |
|--------------|--------------------------------------------|
| LAN          | `ws://192.168.x.x:8765`                    |
| Railway      | `wss://your-app.up.railway.app`            |
| Render       | `wss://your-app.onrender.com`              |

> Always use **`wss://`** (TLS) for cloud deployments — Railway and Render terminate TLS for you automatically.

---

## Handshake Protocol (Quick Reference)

After the WebSocket connection opens:

1. **Client sends** (text frame): `ROOM:0`  
   *(e.g. `AB12:0` — the `:0` hint is ignored; server assigns IDs)*

2. **Server replies** (text frame):  
   - `OK:AB12:3` — success, you are Player 3  
   - `ERROR:ROOM_FULL` — room has 6 players already  
   - `ERROR:SERVER_FULL` — server at MAX_ROOMS capacity  

3. All subsequent messages are **64-byte binary packets** only.

---

## Packet Type Quick Reference

| Hex  | Name            | Direction      | Notes                              |
|------|-----------------|----------------|------------------------------------|
| 0x01 | POSITION        | Client→Server  | Relayed to all others              |
| 0x02 | BATTLE_START    | Client→Server  | Relayed + LOCK sent to others      |
| 0x03 | BATTLE_END      | Client→Server  | Relayed + UNLOCK sent to others    |
| 0x04 | CUTSCENE_START  | Client→Server  | Relayed + LOCK sent to others      |
| 0x05 | CUTSCENE_END    | Client→Server  | Relayed + UNLOCK sent to others    |
| 0x06 | MAP_CHANGE      | Client→Server  | Relayed to all others              |
| 0xFF | PING            | Client→Server  | Server replies with PONG (0xFF)    |
| 0x10 | DISCONNECT      | Server→Client  | A player left the room             |
| 0x11 | LOCK            | Server→Client  | Freeze the indicated player's NPC  |
| 0x12 | UNLOCK          | Server→Client  | Unfreeze the indicated player's NPC|

---

## Cost Summary

| Platform   | Free Tier Limits                    | Verdict          |
|------------|-------------------------------------|------------------|
| Railway    | $5/mo free credit (≈500 hours)      | ✅ Best for games |
| Render     | Spins down after 15 min idle        | ⚠️ OK for testing |
| Fly.io     | 3 shared-cpu VMs free               | ✅ Good alternative |

---

*Generated by Project ORAS MMO Backend Agent*
