# Pokémon ORAS MMO 17.0 — Global

**English** · [Español](#español)

## What's inside
- `OrasMmo.3gx` — the plugin (default server `127.0.0.1`; set yours below)
- `INSTALL_PC.bat` — Windows installer for Azahar (asks for your server address)
- `server/` — the relay server to host your own community (`server/README.md`)
- `shaders/ORAS_MMO_HD.glsl` — optional post-processing shader (Azahar, OpenGL)

## Play
1. **Windows**: run `INSTALL_PC.bat` and type the server address (`IP` or `IP:port`).
   **Linux/macOS**: copy `OrasMmo.3gx` to `<Azahar user folder>/sdmc/luma/plugins/000400000011C400/` (Omega Ruby)
   or `.../000400000011C500/` (Alpha Sapphire) and create `mmo_server.txt` there with the server IP.
   **Android**: copy `OrasMmo.3gx` as `default.3gx` into Azahar's `sdmc/luma/plugins/`, plus `mmo_server.txt`.
2. Enable Azahar's **3GX plugin loader** (System settings). Android: graphics API **OpenGL ES**.
3. Load your save and press **SELECT**. Everyone must use the same server and the same **room code**
   (SELECT → Connection).
4. The plugin creates `mmo_key.txt` next to it: your secret ranking key, so nobody else can use your nickname to
   change your stats. Keep it, don't share it, and copy it to play with the same nickname on another device.

## Host a server
`python3 server/server.py` (TCP 9000, Python 3.8+, no dependencies), or double-click `server/run_server.bat`.
- **Friends far away, no VPS?** The host runs the server on their PC and everyone joins the same free virtual network:
  **ZeroTier** (my.zerotier.com: create a private network, everyone joins its Network ID, the host authorizes each
  friend) or **Tailscale**. Friends use the host's ZeroTier/Tailscale IP as the server. Both work on Android too.
- Or open TCP 9000 on the host's router, or use a VPS.
- Allow Python / TCP 9000 in the Windows firewall (also on public networks).
Full guide: `server/README.md` and the project README ("Play with friends far away").

You need your own legal copy of Pokémon Omega Ruby or Alpha Sapphire. Followers need the optional
`mmo_pokes_3d.bin` pack (not included: it contains game graphics; build it from your own game with `tools/pokepack` in
the repository). Project: https://github.com/SamuTecYT/ORAS-MMO

---

## Español

## Qué trae
- `OrasMmo.3gx` — el plugin (servidor por defecto `127.0.0.1`; pon el tuyo como se explica abajo)
- `INSTALL_PC.bat` — instalador para Azahar en Windows (te pide la dirección del servidor)
- `server/` — el servidor para montar tu propia comunidad (`server/README.md`)
- `shaders/ORAS_MMO_HD.glsl` — shader opcional de post-procesado (Azahar, OpenGL)

## Jugar
1. **Windows**: abre `INSTALL_PC.bat` y escribe la dirección del servidor (`IP` o `IP:puerto`).
   **Linux/macOS**: copia `OrasMmo.3gx` en `<carpeta de usuario de Azahar>/sdmc/luma/plugins/000400000011C400/`
   (Rubí Omega) o `.../000400000011C500/` (Zafiro Alfa) y crea ahí `mmo_server.txt` con la IP del servidor.
   **Android**: copia `OrasMmo.3gx` como `default.3gx` en `sdmc/luma/plugins/` de Azahar, y un `mmo_server.txt`.
2. Activa el **cargador de plugins 3GX** de Azahar (ajustes de Sistema). Android: gráficos **OpenGL ES**.
3. Carga tu partida y pulsa **SELECT**. Todos usan el mismo servidor y el mismo **código de sala**
   (SELECT → Conexión).
4. El plugin crea `mmo_key.txt` a su lado: tu llave secreta del ranking, para que nadie use tu apodo para cambiar tus
   puntos. Guárdala, no la compartas y cópiala para jugar con el mismo apodo en otro aparato.

## Montar un servidor
`python3 server/server.py` (TCP 9000, Python 3.8+, sin dependencias), o doble clic en `server/run_server.bat`.
- **¿Amigos lejos y sin VPS?** El anfitrión abre el servidor en su PC y todos os unís a la misma red virtual gratuita:
  **ZeroTier** (my.zerotier.com: crea una red privada, todos os unís a su Network ID y el anfitrión autoriza a cada
  amigo) o **Tailscale**. Los amigos ponen como servidor la IP de ZeroTier/Tailscale del anfitrión. También en Android.
- O abre el TCP 9000 en el router del anfitrión, o usa un VPS.
- Permite Python / el TCP 9000 en el firewall de Windows (también en redes públicas).
Guía completa: `server/README.md` y el README del proyecto ("Jugar con amigos lejos").

Necesitas tu propia copia legal de Pokémon Rubí Omega o Zafiro Alfa. Los Pokémon acompañantes necesitan el paquete
opcional `mmo_pokes_3d.bin` (no incluido: son gráficos del juego; créalo desde tu propio juego con `tools/pokepack` del
repositorio). Proyecto: https://github.com/SamuTecYT/ORAS-MMO
