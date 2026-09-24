# 🌟 POKÉMON ORAS MMO v11.0 (Edición Definitiva) — Guía de Inicio Rápido

¡Bienvenido a la versión **v11.0 Definitiva** de **Pokémon ORAS MMO**!  
Este mod convierte **Pokémon Rubí Omega** y **Pokémon Zafiro Alfa** de Nintendo 3DS en un juego multijugador masivo en tiempo real, permitiendo que juegues y te encuentres por el mapa de Hoenn con tus amigos tanto desde **PC (Windows)** como desde **Android (Celulares y Tablets)** con conexión cruzada (*Crossplay* total).

---

## 🎯 ¿Qué hace este Mod?
* **Ver a tus amigos en el mundo:** Los verás caminando, corriendo y usando su bicicleta en tiempo real a tu lado a 60 FPS ultra fluidos.
* **Mapa de Hoenn en tiempo real (AreaNav):** Abre el mapa en la pantalla táctil para ver exactamente en qué pueblo, ciudad o ruta se encuentra cada amigo en cualquier rincón de la región.
* **Cero Bugs y Máxima Estabilidad:** Entra y sal libremente de Centros Pokémon, tiendas, cuevas, casas y combates sin pantallas congeladas ni pérdidas de progreso.
* **Chat y Emoticonos:** Escribe cualquier mensaje con el teclado en pantalla o envía caras y saludos flotantes.
* **Combates e Intercambios Oficiales:** Utiliza el PSS nativo de Pokémon para luchar e intercambiar con tu equipo.

---

## 📦 Contenido de este Repositorio

| Carpeta / Archivo | ¿Para qué sirve? |
| :--- | :--- |
| **`releases/PC/`** | Contiene el plugin para PC (`plugin.3gx`, `OrasMmo.3gx`), el instalador automático en 1 clic (`INSTALAR_PC.bat`) y su guía `README_PC.md`. |
| **`releases/Android/`** | Contiene los plugins para celulares (`default.3gx`, etc.) y la guía paso a paso `README_Android.md`. |
| **`server/`** | El programa que une a todos los jugadores (`run_server.bat` y `server.py`). Solo lo abre **UNA** persona (el anfitrión). |
| **`ZeroTier/`** | Instalador oficial de **ZeroTier One** (`ZeroTier One.msi`) para jugar por Internet a distancia de forma 100% gratuita y privada. |

---

## ⚡ Guía Rápida en 3 Pasos (Para Principiantes)

### PASO 1: Elegir quién será el Anfitrión (Host)
Solo una persona del grupo debe ser el anfitrión:
1. El anfitrión abre la carpeta **`server/`** y hace doble clic en **`run_server.bat`**.
2. Se abrirá una ventana negra con letras verdes: **¡Déjala abierta mientras juegan!**
3. **¿Juegan en la misma casa (mismo Wi-Fi)?** La ventana te mostrará tu IP local (ejemplo: `192.168.1.15`). Esa es la IP que le das a tus amigos.
4. **¿Juegan por Internet (cada uno en su casa)?** 
   - Abre **ZeroTier One** (está en la carpeta `ZeroTier/`).
   - Conéctense todos a la misma red virtual.
   - La **"Managed IP"** de ZeroTier del anfitrión (ejemplo: `10.147.19.45`) es la que usarán todos.

---

### PASO 2: Instalar el Mod en tu Dispositivo

#### 💻 Si juegas en PC (Emulador Azahar o Citra):
1. Entra en la carpeta **`releases/PC/`** y haz doble clic en **`INSTALAR_PC.bat`** (o copia `plugin.3gx` a tu carpeta de Azahar manualmente).
2. Abre Azahar, ve a **Emulación → Configurar → Depuración** (o Sistema) y asegúrate de marcar la casilla **"Enable Plugin Loader"** (o *Habilitar cargador de plugins 3GX*).
3. Abre Pokémon Rubí Omega o Zafiro Alfa.

#### 📱 Si juegas en Android (Celular / Tablet):
1. Con una app de archivos (como **ZArchiver** o Files), entra a la carpeta **`releases/Android/`** y copia el archivo **`default.3gx`**.
2. Pégalo en la carpeta de Azahar en tu teléfono:  
   `Azahar/sdmc/luma/plugins/default.3gx`  
   *(o en `Android/data/org.citra.azahar/files/sdmc/luma/plugins/default.3gx`)*
3. En los ajustes de Azahar en tu teléfono:
   - **Sistema:** Activa *"Habilitar cargador de plugins 3GX"*.
   - **Gráficos:** Selecciona **OpenGL ES** (¡nunca Vulkan!).
4. Abre el juego en tu celular.

---

### PASO 3: Conectarse y Calibrar Dentro del Juego
1. Entra a tu partida guardada y sal al exterior (a cualquier pueblo o ruta).
2. En la esquina superior izquierda verás un recuadro que dice `MMO [SALA] Jugador`.
3. Presiona el botón **SELECT** (en tu teclado/mando de PC, o tocando el botón táctil SELECT en la pantalla de tu celular):
   - **Configuración de Red y Perfil → Cambiar IP del Servidor:** Escribe la IP del anfitrión.
   - **Configuración de Red y Perfil → Cambiar Codigo de Sala:** Asegúrate de que todos tengan el mismo código (ejemplo: `1234` o `AZAH`).
   - **Configuración de Red y Perfil → Configurar mi Nombre:** Escribe el nombre con el que quieres que te vean.
4. **¡CALIBRACIÓN RÁPIDA (Muy Importante para sincronizar posiciones)!**:
   - En el menú SELECT entra en **Calibración y Ajustes Visuales**.
   - Quédate completamente quieto y pulsa: **`1. [PASO 1] Registrar Posicion Inicial`**.
   - Da 3 pasos con tu personaje hacia cualquier dirección y pulsa: **`2. [PASO 2] Confirmar Movimiento (Caminar 3 pasos)`**.
5. Presiona el botón **B** para cerrar el menú.  
**¡Listo! El recuadro superior se pondrá verde y empezarás a ver a tus amigos en tiempo real en la pantalla y en el mapa.**

---

## 🗺️ Cómo Ver a tus Amigos en el Mapa de Hoenn (AreaNav)
* En la **pantalla táctil inferior**, toca el **AreaNav** (el mapa de la región).
* Verás pines luminosos con los nombres de tus amigos ubicados exactamente sobre la ciudad, pueblo o ruta en la que están.
* **100% Independiente:** En esta versión 11.0, el punto de cada amigo permanece totalmente quieto si él está quieto, y solo se mueve cuando él realmente camina.

---

## 💬 Chat y Emoticonos
* Presiona **SELECT** y entra a **Chat y Emotes → Enviar Chat Rapido**.
* Puedes seleccionar frases rápidas o pulsar **`>> Escribir Mensaje Personalizado <<`** para escribir lo que quieras con el teclado.
* También puedes seleccionar **Enviar Emote / Saludo** para mostrar caritas felices, corazones y animaciones sobre tu personaje.

---

## ⚔️ Cómo Luchar e Intercambiar Oficialmente (PSS)
1. Ambos jugadores deben unirse a la misma sala multijugador dentro del menú del emulador Azahar (**Multijugador → Crear / Unirse a Sala** en PC, o en el menú lateral de Android).
2. En la pantalla táctil inferior de Pokémon, abre el **PSS**, toca el icono circular azul de Wi-Fi arriba a la derecha y pulsa **"Sí"**.
3. Tu amigo aparecerá en la lista de Transeúntes o Amigos. Tócalo y elige **Combate** o **Intercambio**.

---

## ❓ Preguntas Frecuentes y Ayuda
* **¿Se puede bugear el juego al entrar al Centro Pokémon?**  
  No. La versión 11.0 incluye protección de transiciones que desactiva temporalmente el radar al cruzar puertas y entrar a edificios para que no sufras ningún bloqueo de cámara o pantalla congelada.
* **El recuadro dice "Conectando a..." y se queda en rojo:**  
  Revisa que el anfitrión tenga abierto `run_server.bat`, que ambos estén en la misma red de ZeroTier (o mismo Wi-Fi), y que la IP introducida sea la correcta.
* **¿Dudas específicas de PC o Android?**  
  Revisa las guías detalladas en `docs/GUIA_PC_ES.md` y `docs/GUIA_ANDROID_ES.md`.
