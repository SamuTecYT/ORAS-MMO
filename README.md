# 🌟 Pokémon ORAS MMO — v12.6 (Definitive Edition)

<p align="center">
  <img src="https://raw.githubusercontent.com/SamuTecYT/ORAS-MMO/main/docs/banner.png" alt="Project ORAS MMO Banner" width="750" onerror="this.style.display='none'"/>
</p>

<p align="center">
  <b>Real-Time Cross-Platform Multiplayer Synchronization for Pokémon Omega Ruby & Alpha Sapphire</b><br>
  <i>Play seamlessly with friends across PC (Windows) and Android in the Hoenn region at 60 FPS.</i>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Version-12.6%20Definitive-brightgreen.svg" alt="Version 12.6">
  <img src="https://img.shields.io/badge/Platform-PC%20%7C%20Android-blue.svg" alt="Crossplay">
  <img src="https://img.shields.io/badge/Engine-Isometric%203D%20Projection-orange.svg" alt="3D Engine">
  <img src="https://img.shields.io/badge/Safety-Zero--RAM--Writes-success.svg" alt="Zero RAM Writes">
  <img src="https://img.shields.io/badge/License-MIT-lightgrey.svg" alt="License">
</p>

---

## 📖 Table of Contents
1. [The Origin & Vision](#-the-origin--vision)
2. [The Custom 3D Projection Engine](#-the-custom-3d-projection-engine)
3. [Key Features](#-key-features)
4. [AreaNav Hoenn Map Radar](#-areanav-hoenn-map-radar)
5. [Cross-Platform Architecture (PC & Android)](#-cross-platform-architecture-pc--android)
6. [ZeroTier Mesh Networking](#-zerotier-mesh-networking)
7. [High-Performance Relay Server](#-high-performance-relay-server)
8. [Installation & Setup](#-installation--setup)
9. [Repository Structure](#-repository-structure)
10. [Cybersecurity & Safety Statement](#-cybersecurity--safety-statement)
11. [Credits & Disclaimer](#-credits--disclaimer)

---

## 💡 The Origin & Vision

When *Pokémon Omega Ruby* and *Pokémon Alpha Sapphire* (Gen 6) released on the Nintendo 3DS in November 2014, they introduced a breathtaking 3D reimagining of the iconic Hoenn region. For nearly a decade, however, multiplayer interaction was strictly limited to disconnected 2D user interfaces—such as the Player Search System (PSS), Battle Spot, Wonder Trade, and the Global Trade Station (GTS). Players could never physically see each other wandering Route 119 in the rain, exploring Granite Cave, or cycling across the Seaside Cycling Road side-by-side.

**Project ORAS MMO** was born to break that limitation. The goal was ambitious yet uncompromising:
1. **Bring true real-time cooperative multiplayer to Pokémon ORAS.**
2. **Never modify or corrupt the player's saved game or core ROM assets.**
3. **Achieve seamless crossplay between PC emulators (Azahar / Citra) and mobile devices (Android).**
4. **Deliver a plug-and-play experience that requires no complex server infrastructure or router port-forwarding.**

Version 12.6 represents the **Definitive Edition**, bringing years of reverse engineering, network optimization, and 3D rendering breakthroughs into a unified, polished package.

---

## 🔮 The Custom 3D Projection Engine

### The Engineering Challenge: Why Entity Injection Failed
Initial modding attempts in 3DS ROM hacking focused on injecting custom Non-Player Character (NPC) structures into the game's internal overworld actor tables (`FieldEntity`). In Pokémon Generation 6, however, the actor tables are strictly bound to scene scripts, zone event pools, and dynamic memory heaps. Injecting external actor slots caused:
* Catastrophic memory collisions during scene transitions.
* Irreversible save-file corruption when saving near injected actors.
* Fatal camera lockups (softlocks) upon entering doors, Pokémon Centers, and trainer battles.

### The Breakthrough: Passive Zero-RAM-Writes + Isometric 3D Projection (WSEP)
Instead of forcing the game engine to allocate foreign entities, we engineered a completely external **Isometric 3D Projection Engine (World-to-Screen Entity Projector)** operating through a native ARM11 3GX plugin:

```
┌─────────────────────────────────────────────────────────────┐
│                       ORAS MMO ENGINE                       │
├──────────────────────────────┬──────────────────────────────┤
│     PASSIVE RAM SCANNER      │     TCP NETWORK CLIENT       │
│  • Player World X, Y, Z      │  • 64-Byte Binary Packets    │
│  • Map/Zone ID, Facing Dir   │  • Sub-5ms Packet Roundtrip  │
│  • Memory Permission: READ   │  • Room Code Multiplexing    │
└──────────────┬───────────────┴──────────────┬───────────────┘
               │                              │
               ▼                              ▼
┌─────────────────────────────────────────────────────────────┐
│               WORLD-TO-SCREEN ISOMETRIC MATH                │
│    ScreenX = CenterX + (RemoteX - LocalX) * ScaleX          │
│    ScreenY = CenterY - (RemoteY - LocalY) * ScaleY          │
│                      - (RemoteZ - LocalZ) * ScaleZ          │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                  FRAMEBUFFER COMPOSITOR                     │
│  • 60 FPS Sub-Frame LERP (Linear Interpolation)             │
│  • Directional Avatar Billboard Sprites & Dynamic Shadows   │
│  • Floating Chat Bubbles & Emote Overlays                   │
│  • Door Transition & Battle Scene State Guards              │
└─────────────────────────────────────────────────────────────┘
```

#### 1. Passive Zero-RAM-Writes Policy
The plugin queries the local player's floating-point coordinates $(X, Y, Z)$, facing angle, and active map ID directly from RAM using strictly `MEMPERM_READ`. **Zero bytes are written to the game's memory tables.** This mathematical read-only approach guarantees 100% immunity to save corruption and eliminates game crashes.

#### 2. Isometric Projection Mathematics
When position packets arrive from remote players, the engine projects their $(X, Y, Z)$ world coordinates onto the top screen $(400 \times 240)$ framebuffer relative to the local camera viewport:

$$\text{Screen}_X = \text{Center}_X + (X_{\text{remote}} - X_{\text{local}}) \cdot \text{Scale}_X$$

$$\text{Screen}_Y = \text{Center}_Y - (Y_{\text{remote}} - Y_{\text{local}}) \cdot \text{Scale}_Y - (Z_{\text{remote}} - Z_{\text{local}}) \cdot \text{Scale}_Z$$

* **Calibrated Scales:** Scaled at $(2.8\text{x}, 2.1\text{y}, 1.6\text{z})$ to produce a natural, human walking speed that aligns perfectly with ORAS's 3D perspective camera.
* **Ground Shadow Anchoring:** A dynamic shadow ellipse is cast onto the terrain beneath each player's feet, creating authentic spatial depth and grounding.

#### 3. 60 FPS Sub-Frame LERP (Linear Interpolation)
Even when network packets arrive at 20–30 ticks per second, the local rendering hook evaluates hardware display vsyncs at 60 FPS, applying Hermite-smoothed linear interpolation between packet samples:

$$\vec{P}(t) = \vec{P}_{\text{prev}} + (\vec{P}_{\text{target}} - \vec{P}_{\text{prev}}) \cdot \alpha(t)$$

This delivers buttery-smooth movement without micro-stutters, rubber-banding, or sprite flickering.

#### 4. Anti-Softlock State Guards
By continuously sampling the game's internal transition registers (such as door warp state `0x08803C20 == 0x5544` and battle state `0x081FB478 == 0x40001`), the projection layer automatically yields to the native engine during scene transitions. Pokémon Center doors, warp pads, caves, and battle intros execute flawlessly with zero camera lockups.

---

## ⚡ Key Features

* **🏃 Real-Time Overworld Avatars:** See your friends walking, running, and cycling in the wild at 60 FPS.
* **🗺️ Hoenn Continental AreaNav Radar:** Real-time bottom-screen radar tracking friends across all 28+ Hoenn routes, towns, and cities.
* **💬 In-Game Virtual Keyboard & Chat:** Press SELECT to open the quick chat menu, type custom messages using an on-screen keyboard, or send animated floating emotes.
* **⚔️ Native PSS Battles & Trades:** Fully compatible with ORAS's Player Search System. Initiate official battles and trade Pokémon seamlessly.
* **📱 Complete PC & Android Crossplay:** Windows players and Android mobile players interact in the exact same room simultaneously.
* **🔒 Zero-Configuration Mesh Networking:** Integrated with ZeroTier One for secure, high-speed global play without port forwarding.
* **🛡️ 100% Safe:** No RAM writes, no game code tampering, and no save corruption risks.

---

## 🗺️ AreaNav Hoenn Map Radar

Opening the **AreaNav** on the bottom touchscreen activates the global Hoenn continental radar:

* **Independent Absolute Positioning:** In v12.6, remote player pins are mathematically decoupled from your local coordinates. When a friend stands still, their pin remains 100% stationary regardless of where you walk.
* **Comprehensive Landmark Database:** Covers all routes (Routes 101 to 128), towns, cities, Battle Resort, Ever Grande City, and major caves.
* **Local Metric Distance:** Displays proximity in meters when players share the same route or town.
* **Interior Safety Isolation:** Automatically displays a clean `"Centro Pokemon"` badge when a player enters an interior, preventing radar clutter.

---

## 🌐 Cross-Platform Architecture (PC & Android)

The MMO engine is implemented as an ARM11 3GX plugin, enabling binary compatibility across all modern 3DS emulation platforms:

| Platform | Recommended Emulator | Plugin Filename | Graphics API |
| :--- | :--- | :--- | :--- |
| **PC (Windows)** | [Azahar](https://azahar-emu.org/) / Citra | `plugin.3gx` / `OrasMmo.3gx` | OpenGL / Vulkan |
| **Android (Mobile)** | Azahar Mobile / Citra MMJ / Lime3DS | `default.3gx` / `plugin.3gx` | **OpenGL ES** *(Mandatory)* |

---

## 🔒 ZeroTier Mesh Networking

Playing over the Internet no longer requires complex router port-forwarding or outdated proprietary VPNs. Project ORAS MMO integrates **ZeroTier One**:
* Creates an encrypted, zero-configuration peer-to-peer virtual local network (VLAN).
* Available across Windows, Android, macOS, and Linux.
* Sub-millisecond virtual network latency.
* Official Windows installer (`ZeroTier One.msi`) included in the package.

---

## 🚀 High-Performance Relay Server

The server (`server.py`) is an ultra-lightweight asynchronous TCP relay written in Python 3:
* **Zero Dependencies:** Built entirely on Python's native `asyncio` library (no `pip install` required).
* **Minimal Footprint:** Consumes less than 20 MB of RAM and near 0% CPU utilization.
* **Room-Based Isolation:** Supports up to 6 players per room, with unlimited concurrent rooms.
* **Binary Protocol:** Fixed 64-byte little-endian packet structures forwarded in sub-5ms intervals.

---

## 📥 Installation & Setup

### 💻 For PC Players (Windows)
1. Double-click `releases/PC/INSTALAR_PC.bat` to automatically install the plugin into your Azahar emulator.
2. In Azahar, go to **Emulation → Configure → Debug** and enable **"Enable Plugin Loader"**.
3. Launch Pokémon ORAS, press **SELECT**, enter the Host's IP, and perform the 3-second calibration.
4. *Detailed guide:* [`docs/GUIDE_PC.md`](docs/GUIDE_PC.md) *(or [`docs/GUIA_PC_ES.md`](docs/GUIA_PC_ES.md) in Spanish)*.

### 📱 For Android Players
1. Use an app like **ZArchiver** to copy `releases/Android/default.3gx` to `Azahar/sdmc/luma/plugins/default.3gx`.
2. In Azahar settings, enable **"Enable 3GX plugin loader"** and set Graphics API to **OpenGL ES**.
3. Launch the game, tap virtual **SELECT**, configure the Host IP, and calibrate.
4. *Detailed guide:* [`docs/GUIDE_ANDROID.md`](docs/GUIDE_ANDROID.md) *(or [`docs/GUIA_ANDROID_ES.md`](docs/GUIA_ANDROID_ES.md) in Spanish)*.

### 🖥️ For the Host (Server)
1. Run `server/run_server.bat` (keep the console open while playing).
2. Share your ZeroTier Managed IP (or local Wi-Fi IP) with your friends.
3. *Detailed guide:* [`server/README.md`](server/README.md).

---

## 📁 Repository Structure

```
ORAS-MMO/
├── 3ds_plugin/               # ARM11 C++ 3GX Plugin Source Code
│   ├── Sources/              # Core engine, WSEP projection, hooks
│   ├── Includes/             # Headers, network structs, types
│   ├── Makefile              # devkitARM build configuration
│   ├── 3gx.ld                # Linker script
│   └── CTRPluginFramework.plgInfo
├── server/                   # Python 3 TCP Relay Server
│   ├── server.py             # Asynchronous packet relay
│   ├── run_server.bat        # 1-click launcher
│   └── README.md             # Server documentation
├── ZeroTier/                 # ZeroTier Virtual Networking
│   ├── ZeroTier One.msi      # Official Windows installer
│   ├── Instalar_ZeroTier.bat # 1-click installer script
│   └── README.md             # ZeroTier configuration guide
├── docs/                     # Guides & documentation (English & Spanish)
│   ├── GUIDE_PC.md           # PC guide (English)
│   ├── GUIDE_ANDROID.md      # Android guide (English)
│   ├── GUIA_PC_ES.md         # PC guide (Spanish)
│   ├── GUIA_ANDROID_ES.md    # Android guide (Spanish)
│   └── LEER_PRIMERO.md       # Quick-start guide (Spanish)
├── releases/                 # Ready-to-play precompiled 3GX plugins
│   ├── PC/                   # Windows 3GX binaries & installer
│   └── Android/              # Mobile universal 3GX binaries
├── README.md                 # Main English Showcase & Documentation
├── README_ES.md              # Documentación principal en Español
├── LICENSE                   # MIT License
└── .gitignore                # Git exclusions
```

---

## 🛡️ Cybersecurity & Safety Statement

This project has been built following strict open-source security best practices:
* **Zero Telemetry & Tracking:** No personal data, tracking cookies, hardware IDs, or telemetry are collected or transmitted.
* **No Hardcoded Credentials:** No hardcoded IP addresses, personal paths, or tokens exist in the source code or binaries. Default connection strings use standard loopback (`127.0.0.1`).
* **100% Read-Only Memory Safety:** The plugin does not execute arbitrary RAM writes or payload injection into the game's executable code.
* **Secure Networking:** Relies on industry-standard encrypted tunnels (ZeroTier) and clean, stateless TCP binary buffers.

---

## 📜 Credits & Disclaimer

* **Lead Developer & Creator:** [SamuTecYT](https://github.com/SamuTecYT)
* **Framework:** Powered by [CTRPluginFramework](https://github.com/PabloMK7/CTRPluginFramework) by PabloMK7 & Nanquitas.
* **Special Thanks:** The 3DS reverse engineering and Pokémon emulation communities.

### ⚖️ Legal Notice
*Pokémon*, *Pokémon Omega Ruby*, and *Pokémon Alpha Sapphire* are registered trademarks of Nintendo, Creatures Inc., and GAME FREAK Inc. This project is a non-profit, educational, and transformative fan mod. It contains no copyrighted Nintendo ROM assets, game executables, or proprietary game media. Users must provide their own legally acquired copy of the game.
