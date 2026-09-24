# 💻 PC Installation & Setup Guide (Windows)

Complete guide to setting up **Pokémon ORAS MMO v11.0 (Definitive Edition)** on PC using **Azahar** or Citra.

---

## 📋 Prerequisites
- **Operating System:** Windows 10 / 11 (64-bit).
- **Emulator:** [Azahar](https://azahar-emu.org/) (Recommended) or Citra.
- **ROM:** A legally obtained copy of *Pokémon Omega Ruby* or *Pokémon Alpha Sapphire* (Update v1.4 recommended).

---

## 🚀 Installation

### Option 1: Automatic 1-Click Installer (Recommended)
1. Open the `releases/PC/` folder.
2. Double-click `INSTALAR_PC.bat`.
3. The script automatically detects your Azahar directory and installs the plugin for both Omega Ruby and Alpha Sapphire.
4. When prompted, you can enter the Host's IP address.

### Option 2: Manual Installation
1. Copy `releases/PC/plugin.3gx` (or `OrasMmo.3gx`).
2. Paste it into your emulator's plugin folder:
   - **Omega Ruby:** `%APPDATA%\Azahar\sdmc\luma\plugins\000400000011C400\plugin.3gx`
   - **Alpha Sapphire:** `%APPDATA%\Azahar\sdmc\luma\plugins\000400000011C500\plugin.3gx`  
   *(If using standard Citra, replace `Azahar` with `Citra` in the path)*.

---

## ⚙️ Enabling the Plugin in Azahar
1. Launch **Azahar**.
2. Click **Emulation → Configure...** in the top menu bar.
3. Navigate to **General → Debug** (or *System* depending on the build).
4. Check the box labeled **"Enable Plugin Loader"** (or *Habilitar cargador de plugins 3GX*).
5. Click **OK**.

---

## 🕹️ In-Game Configuration & Calibration
1. Launch Pokémon ORAS and load your save file into the Overworld.
2. An overlay badge in the upper left displays `MMO [ROOM] Player`.
3. Press **SELECT** on your keyboard/gamepad:
   - **Network & Profile Settings → Server IP:** Enter the Host IP (Local Wi-Fi or ZeroTier Managed IP).
   - **Network & Profile Settings → Room Code:** Match your friends (e.g. `1234` or `AZAH`).
   - **Network & Profile Settings → Set Nickname:** Choose your display name.
4. **Mandatory 3-Second Calibration:**
   - Go to **Calibration & Visual Adjustments**.
   - Stand completely still and select: **`1. [STEP 1] Register Initial Position`**.
   - Walk 3 steps in any direction and select: **`2. [STEP 2] Confirm Movement (Walk 3 steps)`**.
5. Press **B** to exit the menu. The badge turns green and you will see your friends moving in real time!
