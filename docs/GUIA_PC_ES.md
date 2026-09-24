# 💻 Pokémon ORAS MMO v11.0 (Edición Definitiva) — Guía para PC (Windows)

Esta guía explica paso a paso cómo instalar, configurar y disfrutar de **Pokémon Rubí Omega y Zafiro Alfa MMO (Versión 11.0 Definitiva)** en tu computadora con Windows utilizando el emulador **Azahar** (o Citra).

---

## ✨ Novedades de la Versión 11.0 Definitiva
1. **Mapa de Hoenn Absoluto e Independiente:**
   - Tus amigos aparecen en la pantalla inferior (AreaNav) en su posición geográfica real.
   - Si un amigo está quieto, su punto se queda 100% inmóvil sin importar hacia dónde camines tú.
2. **Cero Pantallas Congeladas (Anti-Softlock Total):**
   - Transiciones seguras al entrar y salir de Centros Pokémon, tiendas, casas, cuevas y batallas.
3. **Cero Escritura en RAM (Seguridad Máxima):**
   - El mod es 100% seguro para tus archivos de guardado y progreso del juego.
4. **Interpolación Suave a 60 FPS:**
   - Movimiento natural y fluido de los avatares sin parpadeos ni saltos bruscos.
5. **Crossplay PC & Android:**
   - Juega simultáneamente con amigos que estén en sus teléfonos móviles o en computadoras.

---

## 📥 Paso 1: Instalación del Plugin en Azahar / Citra

### Opción Automática (Recomendada en 1 Clic):
1. Entra en la carpeta **`releases/PC/`**.
2. Haz doble clic en el archivo **`INSTALAR_PC.bat`**.
3. El instalador detectará automáticamente la carpeta de Azahar en tu PC y copiará los archivos del mod en las rutas correspondientes de Rubí Omega y Zafiro Alfa.
4. Si lo deseas, podrás configurar la IP del servidor directamente en la pantalla negra del instalador.

### Opción Manual (Si usas una versión portable o prefieres copiarlo tú mismo):
1. Copia el archivo **`plugin.3gx`** (o `OrasMmo.3gx`) de la carpeta `releases/PC/`.
2. Pégalo en tu carpeta de Azahar según el juego que tengas:
   - **Pokémon Rubí Omega:**  
     `%APPDATA%\Azahar\sdmc\luma\plugins\000400000011C400\plugin.3gx`
   - **Pokémon Zafiro Alfa:**  
     `%APPDATA%\Azahar\sdmc\luma\plugins\000400000011C500\plugin.3gx`  
   *(Si juegas en Citra tradicional, la ruta es `%APPDATA%\Citra\sdmc\luma\plugins\...`)*.

---

## ⚙️ Paso 2: Activar el Plugin en el Emulador

Para que el juego cargue el mod al iniciar, debes tener activada esta casilla en tu emulador:
1. Abre **Azahar** (o Citra) en tu PC.
2. En la barra superior haz clic en **Emulación → Configurar...**
3. Ve a la pestaña **General → Depuración** (en algunas versiones está en *Sistema* o *Depuración*).
4. Asegúrate de que esté marcada la opción: **"Enable Plugin Loader"** (o *Habilitar cargador de plugins 3GX*).
5. Haz clic en **Aceptar**.

---

## 🌐 Paso 3: Conexión de Red (ZeroTier o Wi-Fi Local)

### Caso A: Jugar por Internet a distancia con amigos (ZeroTier)
Para que amigos que están en diferentes casas puedan jugar juntos sin abrir puertos en el router:
1. **Instalar ZeroTier:**  
   En la carpeta `ZeroTier/`, haz doble clic en **`Instalar_ZeroTier.bat`** (o en `ZeroTier One.msi` directamente).
2. **Crear o Unirse a la Red:**
   - Uno del grupo crea una red gratuita en [my.zerotier.com](https://my.zerotier.com) y copia el **Network ID** (código de 16 caracteres, ej: `8056c2e21c000001`).
   - Los demás abren ZeroTier en su PC (icono en la barra de tareas al lado del reloj), hacen clic derecho, seleccionan **Join New Network...**, pegan el Network ID y pulsan **Join**.
   - El creador de la red entra a my.zerotier.com y marca la casilla **"Auth"** para autorizar a cada miembro.
3. **Obtener la IP del Anfitrión (Host):**
   - El anfitrión mira en su ZeroTier su **Managed IP** (ejemplo: `10.147.19.45`).
   - Esa es la IP que todos los jugadores pondrán en el juego.

### Caso B: Jugar en la misma casa (Wi-Fi Local)
- No necesitan ZeroTier. Simplemente conéctense al mismo Wi-Fi del hogar.
- El anfitrión abre `server/run_server.bat` y mira su IP local (ejemplo: `192.168.1.15`). Esa será la IP a usar.

---

## 🚀 Paso 4: Iniciar el Servidor (Solo el Anfitrión)
Si tú eres el anfitrión del grupo:
1. Entra a la carpeta **`server/`** y haz doble clic en **`run_server.bat`**.
2. Verás una consola que dice `Servidor ORAS MMO ACTIVO en el puerto 9000 (TCP)`.
3. **Mantén esta ventana abierta mientras jueguen.** Al terminar la sesión, puedes cerrarla con normalidad.

---

## 🕹️ Paso 5: Configurar y Calibrar Dentro del Juego

1. Abre Pokémon Rubí Omega o Zafiro Alfa en Azahar y carga tu partida en el Overworld.
2. Verás en la esquina superior izquierda un recuadro compacto que dice:  
   `MMO [SALA] Jugador`  
   *(En color rojo si aún no estás conectado al servidor, o verde si ya conectó).*
3. Presiona la tecla asignada a **SELECT** en tu teclado o mando para abrir el menú del mod:
   - **Configuración de Red y Perfil → Cambiar IP del Servidor:** Escribe la IP del anfitrión (la de ZeroTier o Wi-Fi).
   - **Configuración de Red y Perfil → Cambiar Codigo de Sala:** Escriban el mismo código todos (por ejemplo: `1234` o `AZAH`).
   - **Configuración de Red y Perfil → Configurar mi Nombre:** Escribe tu apodo.
4. **¡CALIBRACIÓN INDISPENSABLE (Solo toma 3 segundos)!**:
   - Entra en **Calibración y Ajustes Visuales**.
   - Deja a tu personaje quieto y pulsa: **`1. [PASO 1] Registrar Posicion Inicial`**.
   - Camina 3 pasos en cualquier dirección y pulsa: **`2. [PASO 2] Confirmar Movimiento (Caminar 3 pasos)`**.
   *(Esto sincroniza la cuadrícula de coordenadas del juego con el servidor para que todos se vean en el lugar exacto).*
5. Presiona el botón **B** (o la tecla asociada) para cerrar el menú.
6. ¡El recuadro se pondrá **verde** y verás a tus amigos en pantalla!

---

## 🗺️ Paso 6: El Mapa de Hoenn (AreaNav)
* Abre el **AreaNav** en la pantalla táctil inferior de tu juego.
* En el mapa general verás rombos luminosos con el nombre de cada jugador indicando la ciudad o ruta donde se encuentra.
* Si están en la misma ciudad o ruta, el mapa te mostrará la distancia exacta en metros entre ustedes.

---

## 💬 Chat y Emoticonos
* Presiona **SELECT** → **Chat y Emotes** → **Enviar Chat Rapido**.
* Selecciona **`>> Escribir Mensaje Personalizado <<`** para redactar cualquier mensaje usando tu teclado.
* Tu mensaje aparecerá flotando sobre la pantalla para que todos lo lean.
* También puedes seleccionar **Enviar Emote / Saludo** para mostrar iconos animados alegres, enfadados, corazones, etc.

---

## ⚔️ Combates e Intercambios con Amigos (PSS)
El mod es 100% compatible con el sistema oficial de Pokémon (PSS):
1. En Azahar, ve al menú **Multijugador → Crear Sala** (o Unirse a Sala) con tus amigos.
2. En la pantalla táctil inferior de tu juego, toca el PSS y presiona el icono circular de Wi-Fi.
3. Tus amigos aparecerán en la lista de jugadores listos para desafiarlos a un combate o realizar intercambios.

---

## 🛠️ Solución de Problemas Frecuentes

* **El recuadro del MMO no aparece al iniciar el juego:**  
  Verifica que hayas activado *"Enable Plugin Loader"* en la configuración de Azahar y que el archivo `plugin.3gx` esté en la carpeta correcta (`000400000011C400` para Rubí Omega o `000400000011C500` para Zafiro Alfa).
* **El recuadro se queda en rojo ("Conectando a..."):**  
  1. Verifica que el anfitrión tenga abierta la ventana de `run_server.bat`.  
  2. Si usan ZeroTier, comprueba que ambos estén autorizados (Auth marcado) y conectados a la red.  
  3. Comprueba que el Firewall de Windows del anfitrión no esté bloqueando a Python.
* **No veo a mis amigos moverse aunque están conectados:**  
  Asegúrate de que ambos hayan realizado la calibración (**Paso 1** quieto y **Paso 2** caminando 3 pasos).
