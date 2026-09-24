# 🌐 ZeroTier One Guide — Project ORAS MMO

ZeroTier provides secure, end-to-end encrypted peer-to-peer virtual networking. It allows players on PC and Android to connect across different homes and networks worldwide without port forwarding.

---

## 💻 Windows Setup (Host & PC Players)

1. **Install ZeroTier One:**  
   Double-click `ZeroTier One.msi` (or run `Instalar_ZeroTier.bat`).
2. **Create a Network (Host Only):**  
   - Visit [my.zerotier.com](https://my.zerotier.com) and create a free account.
   - Click **Create a Network** and copy your 16-character **Network ID** (e.g., `8056c2e21c000001`).
   - Share this Network ID with your friends.
3. **Join the Network:**  
   - Right-click the ZeroTier tray icon near the Windows taskbar clock.
   - Select **Join New Network...**, paste the 16-character Network ID, and click **Join**.
4. **Authorize Members (Host Only):**  
   - In the web console at [my.zerotier.com](https://my.zerotier.com), navigate to your network and scroll down to the **Members** section.
   - Check the **Auth** checkbox for each connected friend.
5. **Get Your Server IP:**  
   - Look at the Host's **Managed IP** column in the member table (e.g., `10.147.19.45`).
   - Every player enters this IP in the game's SELECT menu.

---

## 📱 Android Setup (Mobile Players)

1. Install the official **ZeroTier One** app from the **Google Play Store** (free, no account required on the phone).
2. Open the app, tap the **`+`** icon in the lower right, enter the 16-character Network ID, and toggle the connection switch ON.
3. Accept the Android VPN profile prompt.
4. Have the Host authorize your device in their ZeroTier web console.
5. Enter the Host's Managed IP into the game.
