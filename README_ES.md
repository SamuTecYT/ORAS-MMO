<p align="center">
  <img src="docs/banner.png" alt="Pokémon ORAS MMO" width="750"/>
</p>

<h1 align="center">Pokémon ORAS MMO</h1>

<p align="center">
  <b>Juega Pokémon Rubí Omega / Zafiro Alfa con tus amigos: los ves caminar en tu propio mundo, en tiempo real.</b><br/>
  Un plugin 3GX para el emulador <a href="https://azahar-emu.org">Azahar</a> (PC y Android) + un servidor pequeño y libre que montas tú.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/versi%C3%B3n-17.0-blue" alt="versión 17.0"/>
  <img src="https://img.shields.io/badge/plataforma-Azahar%20PC%20%7C%20Android-green" alt="Azahar PC y Android"/>
  <img src="https://img.shields.io/badge/servidor-Python%203.8%2B-yellow" alt="Python 3.8+"/>
  <img src="https://img.shields.io/badge/licencia-MIT-lightgrey" alt="MIT"/>
</p>

<p align="center"><a href="README.md">🇬🇧 Read in English</a></p>

---

## Índice

1. [Qué hace](#qué-hace)
2. [Cómo funciona](#cómo-funciona)
3. [Empezar a jugar](#empezar-a-jugar)
4. [El menú SELECT](#el-menú-select)
5. [Monta tu propio servidor](#monta-tu-propio-servidor)
6. [Jugar por Internet sin servidor contratado](#jugar-por-internet-sin-servidor-contratado) (ZeroTier, Tailscale, Radmin, router)
7. [Archivos de configuración](#archivos-de-configuración)
8. [Si algo falla](#si-algo-falla)
9. [Compilar el plugin](#compilar-el-plugin)
10. [Hazlo tuyo (programadores)](#hazlo-tuyo-programadores): ver también [CONTRIBUTING](CONTRIBUTING.md) y [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
11. [Seguridad y privacidad](#seguridad-y-privacidad)
12. [Preguntas frecuentes](#preguntas-frecuentes)
13. [Estructura del proyecto](#estructura-del-proyecto)
14. [Aviso legal](#aviso-legal)

---

## Qué hace

- **Tus amigos dentro de tu juego**: hasta 12 por sala. Cada uno aparece en tu mapa con el **modelo 3D real del
  entrenador** (Bruno / Aura), andando, corriendo o en bici (Mach o Acro), con movimiento suave aunque su conexión
  vaya mal (colchón y predicción por jugador).
- **Pokémon acompañantes en 3D**: tu primer Pokémon (y el de tus amigos) sigue a su entrenador, dibujado a partir del
  modelo 3D del propio juego, en 8 direcciones y con su paso real. Solo pisa tus huellas o suelo libre, así que no
  atraviesa casas, setos ni árboles, y los edificios lo tapan bien cuando pasa por detrás.
  *(Necesita el paquete opcional `mmo_pokes_3d.bin`, ver [preguntas](#preguntas-frecuentes).)*
- **Mapas del propio juego**: tus amigos salen como pines en el **AreaNav** y en el **mapa de Vuelo**, en su sitio exacto.
- **Menú SELECT** (ratón, pantalla táctil o botones): jugadores conectados, chat con mensajes rápidos y susurros,
  emotes (`!`, feliz, corazón, risa...), invitaciones a combatir o intercambiar, tu personaje (16 colores de ropa +
  color del gorro o lazo, color de la placa del nombre), pantalla, conexión y ayuda.
- **Ranking de la sala que se guarda**: medallas, Liga, Pokédex y equipo, guardado por el servidor (también de quien
  está desconectado).
- **Combates e intercambios reales**: la invitación sale del menú del MMO y se juega por la sala de red de Azahar (PSS).
- **7 idiomas** para el menú del MMO **y para el juego entero**: español, inglés, francés, italiano, alemán, japonés
  (kana o kanji) y coreano. Por defecto, el de tu partida; se cambia en SELECT → Pantalla → Idioma. Los nombres
  oficiales de Pokémon y lugares salen de tu propio juego; japonés y coreano usan la letra del juego.
- **Juego cruzado**: Azahar en Windows/Linux/macOS y Azahar en Android juegan en las mismas salas.
- **Tu partida nunca se modifica.** El plugin lee la memoria del juego y solo escribe lo visual que necesita (el color
  de tu ropa, los modelos de tus amigos, ocultar el icono de cabeza del mapa, el idioma del juego al arrancar).

## Cómo funciona

```
 Azahar (PC / Android)                                          Tu servidor (PC, VPS, Docker...)
┌──────────────────────────────────────────┐   TCP, paquetes   ┌─────────────────────────────────┐
│ Pokémon Rubí Omega / Zafiro Alfa         │   de 64 bytes     │ server/server.py (Python 3.8+,  │
│  └─ OrasMmo.3gx (CTRPluginFramework)     │ ◄───────────────► │ sin dependencias)               │
│     · lee tu posición, mapa y cámara     │   código de sala  │  · salas de hasta 12 jugadores  │
│     · dibuja amigos, Pokémon y menús     │   + datos         │  · reenvía posiciones, chat...  │
│     · guarda mmo_*.txt a su lado         │                   │  · guarda el ranking de la sala │
└──────────────────────────────────────────┘                   └─────────────────────────────────┘
          │  (opcional) sala de red de Azahar, UDP 24872 — combates e intercambios reales (PSS)
```

El servidor solo **reenvía**: nunca ve tu partida, solo lo que manda el plugin (apodo, modelo, posición, chat,
emotes y datos del ranking). Cualquiera puede montar uno; no hay servidor central.

## Empezar a jugar

Necesitas: **[Azahar](https://azahar-emu.org)** (la última versión), **tu propia copia legal** de Pokémon Rubí Omega
(`000400000011C400`) o Zafiro Alfa (`000400000011C500`) ya funcionando en Azahar (en la web de Azahar se explica cómo
volcar tu propio juego) y la dirección de un servidor (tuyo o de un amigo).

### PC (Windows)
1. Descarga `ORAS_MMO_v17.0_Global.zip` de [Releases](../../releases) y extráelo.
2. Doble clic en **`INSTALL_PC.bat`**. Busca Azahar, instala el plugin para los dos juegos y te pide la dirección del
   servidor (`IP` o `IP:puerto`; vacío = `127.0.0.1`, un servidor en el mismo PC).
3. En Azahar: **Emulación → Configurar → Sistema** (o **Depuración**) → activa el **cargador de plugins 3GX** (una vez).
4. Abre el juego y carga tu partida. Pulsa **SELECT** para abrir el menú del MMO.

### PC (Linux / macOS) e instalación a mano
Abre la carpeta de usuario de Azahar (**Archivo → Abrir carpeta de Azahar**; normalmente `%APPDATA%\Azahar` en Windows,
`~/.local/share/azahar-emu` en Linux y `~/Library/Application Support/Azahar` en macOS).
Copia `OrasMmo.3gx` en `<carpeta de usuario de Azahar>/sdmc/luma/plugins/000400000011C400/` (Rubí Omega) y/o
`.../000400000011C500/` (Zafiro Alfa). Para elegir servidor, crea `mmo_server.txt` en esa misma carpeta con la IP
(por ejemplo `203.0.113.10` o `203.0.113.10:9000`).

### Android
1. Con un explorador de archivos, abre la carpeta de datos de Azahar → `sdmc/luma/plugins/` (crea `luma` y `plugins`
   si no existen).
2. Copia ahí `OrasMmo.3gx` **con el nombre `default.3gx`**, y un `mmo_server.txt` con la IP de tu servidor.
3. Azahar → Ajustes → Sistema: activa el **cargador de plugins 3GX**; Gráficos: **OpenGL ES**.
4. Abre el juego; toca **SELECT** para abrir el menú (la pantalla de abajo es táctil).

### Jugar juntos
- Todos usan el **mismo servidor** y el **mismo código de sala** (SELECT → Conexión; por defecto `1234`).
  Usa un código propio y difícil de adivinar: es lo único que deja fuera de tu sala a desconocidos.
- ¿Sin servidor? Uno de vosotros puede montarlo en casa: mira
  [Jugar por Internet sin servidor contratado](#jugar-por-internet-sin-servidor-contratado).
- **Combates e intercambios**: manda la invitación desde SELECT → Combates; después los dos entráis en la misma sala de
  Azahar (**Multijugador → Conectarse directamente**) y abrís el **PSS** en la pantalla táctil.

## El menú SELECT

Pulsa **SELECT** en el juego (en Android, el botón SELECT de la pantalla). Funciona con ratón, pantalla táctil o botones.

| Sección | Qué haces ahí |
|---|---|
| **Inicio** | Estado de la conexión, tu sala, dónde estás; atajos: ver quién juega, saludar a todos, enviar un mensaje, invitar, cambiar tus colores |
| **Jugadores** | Todos los de tu sala y dónde está cada uno; elige a alguien para susurrarle o invitarle |
| **Ranking** | La tabla de la sala: medallas, Liga, Pokédex, equipo (también de los desconectados) |
| **Chat y emotes** | Mensajes rápidos, texto libre, susurros y emotes (`!`, feliz, corazón, risa...) |
| **Combates** | Invitar a combatir o intercambiar; ver en directo el combate de un amigo |
| **Mi personaje** | Color de la ropa (16), color del gorro o lazo, color de la placa del nombre, vista previa |
| **Pantalla** | Recuadro de estado, nombres, acompañantes (todos / el mío / ninguno), rendimiento (auto / calidad / ahorro), **idioma** |
| **Conexión** | Tu nombre, código de sala, servidor, reconectar |
| **Ayuda** | Guías cortas dentro del juego |

## Monta tu propio servidor

Guía completa: **[server/README.md](server/README.md)** (Windows, VPS con Linux, Docker, systemd, firewall, límites).

```bash
cd server
python3 server.py                 # escucha en TCP 9000, sin dependencias
PORT=9100 python3 server.py       # otro puerto (jugadores: mmo_server.txt = "IP:9100")
docker compose up -d              # o con Docker (antes copia .env.example como .env)
```

Abre el **TCP 9000** (y el **UDP 24872** si también montas la sala de Azahar) en tu router o en el panel del hosting.

## Jugar por Internet sin servidor contratado

No hace falta un VPS. Un jugador (el **anfitrión**) abre el servidor en su propio PC, y sus amigos de cualquier parte
del mundo llegan a ese PC con una **red virtual** gratuita (ZeroTier, Tailscale, Radmin VPN...) o abriendo un puerto del
router. El PC del anfitrión tiene que estar encendido mientras jugáis.

### 1. El anfitrión abre el servidor
1. Instala [Python 3](https://www.python.org/downloads/) (en Windows marca **"Add Python to PATH"**).
2. Doble clic en `server/run_server.bat` (Linux/macOS: `python3 server/server.py`). Deja la ventana abierta.
3. Windows pregunta si deja pasar a Python por el firewall: permítelo en **redes privadas y públicas** (las redes
   virtuales suelen salir como "públicas"). O crea tú la regla, en una terminal abierta como administrador:
   ```bat
   netsh advfirewall firewall add rule name="ORAS MMO" dir=in action=allow protocol=TCP localport=9000
   ```
4. El anfitrión juega en ese mismo PC con el servidor por defecto `127.0.0.1` (no tiene que cambiar nada).

### 2. Conecta a tus amigos (elige una opción)

**ZeroTier** (gratis; Windows, macOS, Linux y **Android**). Es lo que usaba este proyecto antes de tener un VPS:
1. El anfitrión crea una cuenta en [my.zerotier.com](https://my.zerotier.com), pulsa **Create A Network** y copia el
   **Network ID** (16 caracteres). Deja **Access Control** en **Private**.
2. Todos (también el anfitrión) instaláis [ZeroTier One](https://www.zerotier.com/download/) y os **unís** a ese Network
   ID (Windows: icono junto al reloj → *Join New Network*; Android: **+**).
3. En la página de la red, en **Members**, el anfitrión marca **Auth** para cada amigo. Solo gente que conozcas.
4. La **Managed IP** del anfitrión en esa lista (por ejemplo `10.147.17.5`) es la dirección del servidor para los demás.

**Tailscale** (gratis para uso personal; Windows, macOS, Linux y **Android**):
1. Todos instaláis [Tailscale](https://tailscale.com/download) e iniciáis sesión.
2. El anfitrión comparte su PC con cada amigo (consola de administración → su equipo → **Share**) o los invita a su red.
3. La **IP de Tailscale** del anfitrión (`100.x.y.z`, sale en la app) es la dirección del servidor.

**Radmin VPN** (gratis, sin cuenta, **solo Windows**, no sirve para Android): el anfitrión crea una red con nombre y
contraseña, los amigos se unen, y la IP `26.x.x.x` del anfitrión es la dirección del servidor.

**Abrir un puerto del router** (sin programas extra): en el router del anfitrión, redirige el **TCP 9000** a la IP
local de su PC y da a tus amigos tu **IP pública** (busca "cuál es mi IP"). Inconvenientes: tus amigos conocen la IP de
tu casa, puede cambiar, y no funciona si tu compañía usa CGNAT. En ese caso, usa una red virtual.

### 3. Cada amigo pone la dirección del anfitrión
SELECT → **Conexión → Servidor** → escribe la IP del anfitrión (la de la red virtual o la pública), o ponla en
`mmo_server.txt` junto al plugin (en Windows, `INSTALL_PC.bat` te la pide). Todos con el **mismo código de sala**.

### 4. Combates e intercambios (opcional)
El anfitrión abre una sala de Azahar: **Multijugador → Crear sala** (puerto **24872**, con contraseña). Los amigos
entran por **Multijugador → Conectarse directamente** con la misma IP del anfitrión y el puerto 24872. Si usáis un
puerto del router en vez de una red virtual, redirige también el **UDP 24872**. Regla del firewall de Windows:
`netsh advfirewall firewall add rule name="Sala Azahar" dir=in action=allow protocol=UDP localport=24872`

**Si alguien no puede entrar:** la ventana del servidor muestra una línea por cada jugador que se conecta. Revisa:
- la regla del firewall;
- que ZeroTier/Tailscale diga *conectado* en los dos lados (y que el amigo esté autorizado);
- la IP;
- el código de sala.

Las redes virtuales cifran lo que viaja entre vosotros; mete en la tuya solo a gente de confianza.

## Archivos de configuración

Todos van junto al plugin (`sdmc/luma/plugins/<TitleID>/`, o `plugins/` en Android):

| Archivo | Qué es | Ejemplo |
|---|---|---|
| `mmo_server.txt` | Servidor: IPv4 y, si hace falta, `:puerto` (sin nombres de dominio) | `203.0.113.10:9000` |
| `mmo_room.txt` | Código de sala (1–16 caracteres) | `MiGrupo2026` |
| `mmo_name.txt` | Tu apodo (hasta 13 caracteres) | `Ash` |
| `mmo_settings.txt` | Todo lo que eliges en el menú SELECT | lo escribe el plugin |
| `mmo_pokes_3d.bin` | Paquete de Pokémon acompañantes 3D (opcional) | ver [preguntas](#preguntas-frecuentes) |
| `mmo_log.txt` | Registro: adjúntalo cuando informes de un fallo | lo escribe el plugin |

Casi nadie necesita tocarlos: el menú SELECT los cambia por ti.

## Si algo falla

| Problema | Prueba esto |
|---|---|
| Pulso SELECT y no pasa nada | Activa el cargador de plugins 3GX de Azahar; comprueba que el plugin está en `sdmc/luma/plugins/000400000011C400/` (Rubí Omega) o `.../000400000011C500/` (Zafiro Alfa), o `plugins/default.3gx` en Android; reinicia el juego |
| No veo a mis amigos | Todos con el **mismo servidor y el mismo código de sala**; la ventana del servidor tiene que estar abierta; SELECT → Conexión → Reconectar; revisa el firewall del anfitrión |
| El menú dice "Desconectado" | IP o puerto mal escritos, el servidor está apagado o un firewall bloquea el TCP 9000 |
| El juego va lento | SELECT → Pantalla → Rendimiento → **Ahorro**; en Android usa **OpenGL ES** |
| No nos sigue ningún Pokémon | Hace falta `mmo_pokes_3d.bin` (ver preguntas frecuentes) |
| Quiero avisar de un fallo | Adjunta `mmo_log.txt` (junto al plugin); antes borra la IP de tu servidor y tu código de sala |

## Compilar el plugin

Requisitos ([devkitPro](https://devkitpro.org/wiki/Getting_Started)): **devkitARM** (r68), **libctru** (2.7),
**libctrpf** 0.8 (CTRPluginFramework) y **3gxtool** 1.3. libctrpf y 3gxtool vienen del
[repositorio de paquetes de ThePixellizerOSS](https://thepixellizeross.gitlab.io/packages/).

```bash
cd 3ds_plugin
make                      # -> 3ds_plugin.3gx  (renómbralo a OrasMmo.3gx)
```

- **Windows**: el `make` de devkitPro falla con espacios en la ruta; compila desde una carpeta como `C:\dev\ORAS-MMO`.
- Opciones: `-Os -std=gnu++11 -fno-rtti -fno-exceptions`; memoria del plugin `10MiB` (`CTRPluginFramework.plgInfo`).
- Los textos están en **`3ds_plugin/tools/i18n/strings.py`**; después de editarlos, ejecuta
  `python 3ds_plugin/tools/i18n/gen_lang.py` (regenera `Includes/LangTable.inc` y comprueba los `%s`/`%d`).
- Pruebas del servidor: `python -m unittest server/test_server_ranking.py server/test_server_limits.py`.
- ZIP para publicar: `python tools/package_public.py ruta/a/OrasMmo.3gx` → `dist/ORAS_MMO_v17.0_Global.zip`.

## Hazlo tuyo (programadores)

- **Servidor por defecto para tu comunidad**: copia `3ds_plugin/Includes/ServerConfig.example.hpp` como
  `ServerConfig.local.hpp` y pon tu IP, puerto y sala. Ese archivo está en **.gitignore**: tu compilación se conecta
  sola y tu dirección nunca acaba en un repositorio público. Sin él, el plugin usa `127.0.0.1:9000` y la sala `1234`.
- **Servidor privado**: `ORAS_ROOMS=MiSala,OtraSala` hace que el servidor acepte solo esas salas.
- Los comentarios del código citan `KB §x.y`: son las notas de ingeniería inversa del proyecto (direcciones y
  estructuras del juego), que no forman parte de este repositorio. Los valores en sí están en el código.
- **Protocolo de red**: paquetes de 64 bytes, explicados en **[docs/PROTOCOL.md](docs/PROTOCOL.md)**, para escribir
  tu propio servidor o herramientas.
- **Antes de publicar tu versión**: `python tools/prepublish_check.py` revisa lo que git publicaría buscando tus datos
  privados (IP, nombres, contraseñas que pones en un `.private_patterns.txt` ignorado por git) y archivos que nunca se
  comparten (`mmo_pokes_*.bin`, ZIP, rankings, `.env`).

## Seguridad y privacidad

- **Sin cuentas, sin contraseñas y sin telemetría.** El plugin solo habla con el servidor que configures.
- Lo que recibe el servidor: apodo, modelo, posición y mapa, chat y emotes, invitaciones y datos del ranking (medallas,
  Pokédex, tiempo de juego, niveles del equipo). También ve tu dirección IP, como cualquier servidor de juego.
- El servidor comprueba el código de sala, sustituye el número de jugador del que envía (nadie ocupa el sitio de otro),
  limita la cola de cada cliente y pone límites de conexiones (en total, por IP, de salas y de archivos de ranking).
  Los archivos del ranking se nombran con el código de sala en hexadecimal: una sala nunca puede escribir fuera de la
  carpeta del servidor.
- Cada jugador puede mandar como mucho 120 paquetes por segundo (el plugin manda unos 30); si alguien inunda la sala,
  lo que sobra se descarta y no llega a nadie.
- El plugin comprueba todo lo que recibe (números de jugador; los textos siempre se cortan y se terminan) y todo
  archivo que lee (`mmo_*.txt`, el paquete de acompañantes).
- **No hay autenticación**: quien conozca la dirección del servidor y el código de sala puede entrar en esa sala con
  cualquier apodo. Usa un código de sala propio (o `ORAS_ROOMS`) y compártelo solo con gente de confianza.
- ¿Has encontrado una vulnerabilidad? Abre un
  [aviso de seguridad privado](../../security/advisories/new) en lugar de un issue público.

## Preguntas frecuentes

**No salen los Pokémon acompañantes.** Necesitan `mmo_pokes_3d.bin`, un paquete dibujado a partir de los modelos 3D
del propio juego. No se reparte aquí (son gráficos del juego). Sin él funciona todo lo demás; solo no se dibujan.
Puedes crearlo tú desde tu propia copia del juego con [`tools/pokepack`](tools/pokepack/README_ES.md).

**¿Japonés o coreano?** Elígelo en SELECT → Pantalla → Idioma. El menú del MMO cambia al momento; los textos del juego,
al reiniciar el juego. Tu partida no se toca.

**¿"Servidor: Automático" en el menú?** Es el servidor que trae el plugin compilado (`127.0.0.1` en la versión
pública). Pon el tuyo en SELECT → Conexión o con `mmo_server.txt`.

**¿En una 3DS real?** Está hecho y probado en Azahar. En una consola con el cargador de plugins de Luma3DS podría
funcionar, pero no está probado.

**¿Otra versión del juego?** El plugin comprueba la versión del juego al arrancar. Si no coincide, desactiva lo que toca
la memoria del juego en lugar de colgarse.

## Estructura del proyecto

```
3ds_plugin/            El plugin 3GX (C++ / CTRPluginFramework)
  Sources/, Includes/  Lectura del juego, dibujo, menú, red, idiomas
  tools/i18n/          Traducciones (strings.py) y generador de la tabla
server/                Servidor (server.py), pruebas, Docker y guía para montarlo
packaging/             Lo que va dentro del ZIP (instalador, guía rápida)
extras/shaders/        Shader opcional de post-procesado para Azahar (OpenGL)
tools/                 package_public.py (ZIP para publicar), prepublish_check.py, fake_players.py (bots de prueba)
  pokepack/            Crea el paquete de Pokémon acompañantes desde tu propio juego
docs/                  ARCHITECTURE.md (guía para programadores), PROTOCOL.md, banner
CONTRIBUTING.md        Cómo contribuir · SECURITY.md: cómo avisar de vulnerabilidades
```

## Aviso legal

Pokémon ORAS MMO es un proyecto de fans sin ánimo de lucro, sin relación con Nintendo, Creatures Inc. ni GAME FREAK inc.
Pokémon y los nombres relacionados son marcas de sus respectivos dueños. Este repositorio **no contiene ROMs, ni código
ni gráficos del juego**: los nombres, letras, modelos y texturas se leen al jugar desde **tu propia copia** del juego.
Úsalo solo con juegos que tengas legalmente.

Código con [licencia MIT](LICENSE). Creado por [SamuTecYT](https://github.com/SamuTecYT) y colaboradores.
