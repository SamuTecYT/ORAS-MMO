# 📱 Android Installation & Setup Guide

Step-by-step guide for running **Pokémon ORAS MMO v11.0 (Definitive Edition)** on Android phones and tablets with crossplay to PC.

---

## 📋 Prerequisites
- **Operating System:** Android 9.0 or higher.
- **Emulator:** Azahar Mobile, Citra MMJ, or Lime3DS.
- **File Manager:** [ZArchiver](https://play.google.com/store/apps/details?id=ru.zdevs.zarchiver) or Google Files.
- **Game:** Pokémon Omega Ruby or Alpha Sapphire ROM (v1.4 update recommended).

---

## 📥 Step 1: Copying the Plugin

1. Extract this package onto your phone using **ZArchiver**.
2. Navigate to `releases/Android/` and copy **`default.3gx`**.
3. Paste it into your emulator's plugin folder:
   - 👉 **Standard Path:** `Azahar/sdmc/luma/plugins/default.3gx`
   - 👉 **Scoped Storage (Android 11+):**  
     `Android/data/org.citra.azahar/files/sdmc/luma/plugins/default.3gx`  
     *(If using Citra or Lime3DS, look for `citra-emu` instead of `Azahar`)*.

> [!NOTE]
> If the `luma` or `plugins` folders do not exist, create them using lowercase letters. The file name `default.3gx` allows Azahar to load the plugin automatically across both Omega Ruby and Alpha Sapphire.

---

## ⚙️ Step 2: Critical Emulator Settings (Crash Prevention)

Open the **Azahar** app and tap the **Settings icon (gear)**:
1. **System (or Debug):**
   - Enable **"Enable 3GX plugin loader"** (or *Habilitar cargador de plugins 3GX*).
2. **Graphics (MANDATORY):**
   - Set **Graphics API** strictly to **OpenGL ES**.  
   ⚠️ **NEVER select Vulkan!** Vulkan crashes immediately on mobile devices when loading 3GX ARM11 plugins.
3. **Debug:**
   - Ensure **"Delay start with LLE modules"** is **DISABLED** (off).

---

## 🌐 Step 3: ZeroTier Connection (Internet Play)
1. Install **ZeroTier One** from the **Google Play Store**.
2. Tap **`+`**, enter the Host's 16-character Network ID, and toggle the switch ON.
3. Accept the Android VPN prompt.
4. Have the Host authorize your device in their ZeroTier dashboard.
5. Obtain the Host's **Managed IP** (e.g. `10.147.19.45`).

---

## 🕹️ Step 4: In-Game Setup & Calibration
1. Launch Pokémon ORAS and load your game in the Overworld.
2. An overlay badge appears in the top-left: `MMO [ROOM] Player`.
3. Tap the on-screen virtual **SELECT** button:
   - **Network & Profile Settings → Server IP:** Enter the Host's IP.
   - **Network & Profile Settings → Room Code:** Enter the shared room code (e.g. `1234`).
   - **Network & Profile Settings → Set Nickname:** Enter your name.
4. **Calibration (Required):**
   - Tap **Calibration & Visual Adjustments**.
   - Stand still and tap: **`1. [STEP 1] Register Initial Position`**.
   - Walk 3 steps and tap: **`2. [STEP 2] Confirm Movement (Walk 3 steps)`**.
5. Tap the virtual **B** button to close the menu. The overlay turns green and other players will appear around you!
