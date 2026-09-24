# 🌟 Pokémon ORAS MMO — v12.6 (Edición Definitiva)

<p align="center">
  <b>Multijugador en Tiempo Real para Pokémon Rubí Omega y Zafiro Alfa — PC y Android</b><br>
  <i>Juega con amigos en la región de Hoenn a 60 FPS. Crossplay total entre PC (Windows) y Android.</i>
</p>

---

## 🎯 ¿Qué es este Proyecto?

**Pokémon ORAS MMO** convierte el clásico juego de Nintendo 3DS **Pokémon Rubí Omega y Zafiro Alfa** en un juego multijugador masivo en tiempo real. Usando un plugin personalizado para emuladores (`.3gx`) y un servidor de relé TCP, puedes ver a tus amigos caminando por el mapa de Hoenn a tu lado, con crossplay completo entre PC y Android.

> **Idioma:** Lee esta guía en inglés en [`README.md`](README.md).

---

## ✨ Características Principales

* 🏃 **Avatares en tiempo real:** Verás a tus amigos caminando, corriendo y usando la bici en el mundo a 60 FPS.
* 🗺️ **Radar AreaNav de Hoenn:** Abre el mapa en la pantalla táctil inferior y ve exactamente en qué pueblo, ciudad o ruta está cada amigo.
* 💬 **Chat y Emoticonos:** Escribe mensajes personalizados con el teclado o envía emotes flotantes.
* ⚔️ **Combates e Intercambios Oficiales:** Compatible con el PSS nativo de Pokémon (Combate, Intercambio).
* 📱 **Crossplay PC ↔ Android:** Jugadores en Windows y Android comparten la misma sala sin problema.
* 🔒 **100% Seguro:** Cero escrituras en la RAM del juego. No modifica tu partida guardada.
* 🌐 **ZeroTier integrado:** Juega con amigos de cualquier parte del mundo sin abrir puertos.

---

## 📦 Estructura del Repositorio

```
ORAS-MMO/
├── 3ds_plugin/          # Código fuente C++ del plugin ARM11 3GX
├── server/              # Servidor TCP de relé en Python 3
├── ZeroTier/            # Instalador oficial de ZeroTier One
├── docs/                # Guías detalladas en Español e Inglés
│   ├── GUIA_PC_ES.md        ← Guía completa para PC
│   ├── GUIA_ANDROID_ES.md   ← Guía completa para Android
│   ├── GUIA_SERVIDOR_ES.md  ← Guía para el servidor
│   ├── GUIDE_PC.md          ← PC guide (English)
│   └── GUIDE_ANDROID.md     ← Android guide (English)
├── releases/            # Archivos 3GX precompilados listos para usar
│   ├── PC/              # Plugin para PC (plugin.3gx, OrasMmo.3gx, INSTALAR_PC.bat)
│   └── Android/         # Plugin para Android (default.3gx)
├── README.md            # Documentación principal en Inglés
└── README_ES.md         # Este archivo — Documentación en Español
```

---

## ⚡ Inicio Rápido

### Paso 0 — El Anfitrión Inicia el Servidor
1. Descarga o clona este repositorio.
2. Abre la carpeta **`server/`** y haz doble clic en **`run_server.bat`**.
3. Deja esa ventana abierta mientras juegan.
4. Comparte tu IP (local o ZeroTier Managed IP) con todos los jugadores.

### Para Jugar por Internet — ZeroTier (Gratis)
Si no están en la misma casa:
1. Abre la carpeta **`ZeroTier/`** y ejecuta **`Instalar_ZeroTier.bat`** (o doble clic en `ZeroTier One.msi`).
2. El anfitrión crea una red en [my.zerotier.com](https://my.zerotier.com) y comparte el **Network ID** (16 dígitos).
3. Todos los demás abren ZeroTier → clic derecho en el ícono del reloj → **Join New Network...** → pegan el código.
4. El anfitrión autoriza a cada miembro marcando la casilla **"Auth"** en el panel web.
5. La **"Managed IP"** del anfitrión (ej. `10.147.19.45`) es la IP que todos escribirán en el juego.

---

### 💻 Instalación en PC (Windows)

**Opción A — Automática (1 clic):**
1. Ve a `releases/PC/` y haz doble clic en **`INSTALAR_PC.bat`**.
2. El script detecta Azahar automáticamente e instala el plugin.

**Opción B — Manual:**
1. Copia `releases/PC/plugin.3gx`.
2. Pégalo en tu carpeta de Azahar:
   - Rubí Omega: `%APPDATA%\Azahar\sdmc\luma\plugins\000400000011C400\plugin.3gx`
   - Zafiro Alfa: `%APPDATA%\Azahar\sdmc\luma\plugins\000400000011C500\plugin.3gx`
3. Abre Azahar → **Emulación → Configurar → Depuración** → activa **"Enable Plugin Loader"**.

👉 **Guía completa:** [`docs/GUIA_PC_ES.md`](docs/GUIA_PC_ES.md)

---

### 📱 Instalación en Android

1. Copia `releases/Android/default.3gx`.
2. Pégalo en tu emulador:
   - Ruta típica: `Azahar/sdmc/luma/plugins/default.3gx`
   - Android 11+: `Android/data/org.citra.azahar/files/sdmc/luma/plugins/default.3gx`
3. En los ajustes de Azahar:
   - **Sistema:** Activa *"Habilitar cargador de plugins 3GX"*.
   - **Gráficos:** Selecciona **OpenGL ES** (¡nunca Vulkan!).

👉 **Guía completa:** [`docs/GUIA_ANDROID_ES.md`](docs/GUIA_ANDROID_ES.md)

---

### 🕹️ Configurar y Calibrar Dentro del Juego (PC y Android)

1. Carga tu partida en el exterior (cualquier pueblo o ruta).
2. El recuadro `MMO [SALA] Jugador` aparece en la esquina superior izquierda (rojo = sin conexión, verde = conectado).
3. Presiona **SELECT** y configura:
   - **Configuración de Red y Perfil → Cambiar IP del Servidor:** Pon la IP del anfitrión.
   - **Configuración de Red y Perfil → Cambiar Codigo de Sala:** Todos deben tener el mismo código (ej. `1234`).
   - **Configuración de Red y Perfil → Configurar mi Nombre:** Tu apodo en el juego.
4. **Calibración (3 segundos, obligatoria):**
   - Entra en **Calibración y Ajustes Visuales**.
   - Quédate quieto y pulsa **`1. [PASO 1] Registrar Posicion Inicial`**.
   - Camina 3 pasos y pulsa **`2. [PASO 2] Confirmar Movimiento`**.
5. Presiona **B** para salir. ¡El recuadro se pondrá verde y verás a tus amigos!

---

## 🛠️ Solución de Problemas

| Problema | Solución |
|:---|:---|
| El recuadro queda en **rojo** | Verifica que `run_server.bat` esté abierto y que la IP sea correcta. |
| **No veo a mis amigos** | Asegúrate de que ambos hayan completado el Paso 1 y Paso 2 de calibración. |
| **Android: el juego se cierra** | Cambia el API de gráficos a **OpenGL ES** (nunca Vulkan). |
| **Python no reconocido** | Reinstala Python 3 marcando "Add to PATH" durante la instalación. |
| **ERROR:ROOM_FULL** | Cambia el código de sala a otro valor (SELECT → Cambiar Código de Sala). |

---

## 📜 Créditos y Aviso Legal

* **Desarrollador Principal:** [SamuTecYT](https://github.com/SamuTecYT)
* **Framework:** [CTRPluginFramework](https://github.com/PabloMK7/CTRPluginFramework) por PabloMK7 y Nanquitas.
* **Agradecimientos:** A la comunidad de ingeniería inversa del 3DS y emulación de Pokémon.

*Pokémon*, *Pokémon Rubí Omega* y *Pokémon Zafiro Alfa* son marcas registradas de Nintendo, Creatures Inc. y GAME FREAK Inc. Este proyecto es un mod de fan sin fines de lucro. No incluye ROMs ni activos propietarios. Los usuarios deben aportar su propia copia legal del juego.
