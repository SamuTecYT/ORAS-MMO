# 📱 Pokémon ORAS MMO v11.0 (Edición Definitiva) — Guía para Android

¡Bienvenido a la versión **v11.0 Definitiva** de **Pokémon ORAS MMO** para teléfonos y tablets Android!  
Esta versión te permite jugar a **Pokémon Rubí Omega y Zafiro Alfa** en tu celular viendo a tus amigos en tiempo real, con compatibilidad cruzada (*Crossplay*) total tanto con otros usuarios de Android como con jugadores en PC.

---

## ✨ Novedades de la Versión 11.0 en Celulares
1. **Puntos de Amigos en el Mapa de Hoenn (AreaNav) 100% Precisos:**
   - Tus amigos aparecen en sus ubicaciones geográficas exactas en la pantalla táctil inferior.
   - Si tu amigo no se mueve, su punto se queda completamente quieto aunque tú camines.
2. **Cero Pantallas Congeladas (Anti-Softlock):**
   - Entra y sal con total tranquilidad de Centros Pokémon, casas, cuevas, tiendas y combates.
3. **Cero Modificaciones a tu Partida:**
   - 100% seguro: no modifica la RAM de guardado ni arriesga tus Pokémon.
4. **Optimización Móvil a 60 FPS:**
   - Rendimiento suave con interpolación LERP adaptada para pantallas táctiles de 60Hz a 120Hz.

---

## 📥 Paso 1: Copiar el Plugin en tu Teléfono

Para instalar el mod solo necesitas copiar **un solo archivo**:
1. En tu teléfono, descarga o extrae este paquete usando una app de archivos como **ZArchiver** (recomendada y gratis en Google Play) o **Files de Google**.
2. Entra a la carpeta **`Para_Android/`** y copia el archivo **`default.3gx`**.
3. Pégalo en la carpeta de plugins de tu emulador:
   - 👉 **Ruta típica 1:** `Azahar/sdmc/luma/plugins/default.3gx`
   - 👉 **Ruta típica 2 (Android 11, 12, 13 o 14):**  
     `Android/data/org.citra.azahar/files/sdmc/luma/plugins/default.3gx`  
     *(Si usas Citra tradicional o Lime3DS, la carpeta se llamará `citra-emu` en vez de `Azahar`)*.

> [!TIP]
> Si las carpetas `luma` o `plugins` aún no existen en tu teléfono, simplemente créalas tú mismo con el explorador de archivos escribiendo los nombres en minúsculas.  
> Al usar el nombre `default.3gx`, el juego lo cargará automáticamente tanto en Pokémon Rubí Omega como en Zafiro Alfa.

---

## ⚙️ Paso 2: Ajustes OBLIGATORIOS en Azahar Android (Evita Cierres)

Antes de abrir el juego, abre la app de **Azahar** en tu celular y entra a sus **Ajustes (icono de engranaje)**:
1. **En la sección Sistema (o Depuración):**
   - Activa la casilla: **"Habilitar cargador de plugins 3GX"** (o *"Enable 3GX plugin loader"*).
2. **En la sección Gráficos (MUY IMPORTANTE):**
   - En **API de gráficos**, selecciona obligatoriamente **OpenGL ES**.  
   ⚠️ **¡NUNCA selecciones Vulkan!** Vulkan no soporta la inyección de plugins 3GX en la mayoría de procesadores móviles y cerrará el juego al instante.
3. **En la sección Depuración:**
   - Asegúrate de que **"Retrasar el comienzo con módulos LLE"** esté **DESACTIVADO** (apagado).

---

## 🌐 Paso 3: Conexión por Internet con ZeroTier (En tu Celular)

Si juegas por Internet con amigos que están en otras casas:
1. Instala la aplicación **ZeroTier One** desde **Google Play Store** (es gratis y no requiere registro en el teléfono).
2. Abre la app, toca el botón **`+`** (abajo a la derecha) e ingresa el **Network ID** de 16 dígitos que te dé el anfitrión de la partida.
3. Activa el interruptor de la red para conectarte (acepta la solicitud de conexión VPN de Android).
4. El anfitrión te autorizará en su panel de control de ZeroTier.
5. Pídele al anfitrión su **Managed IP** de ZeroTier (ejemplo: `10.147.19.45`). Esa será la IP que usarás en el juego.

*(Si juegan en la misma casa conectados al mismo Wi-Fi, no necesitan ZeroTier; simplemente usa la IP local que muestra la PC del anfitrión).*

---

## 🕹️ Paso 4: Jugar y Sincronizar Dentro de Pokémon

1. Abre Pokémon Rubí Omega o Zafiro Alfa en tu celular y carga tu partida en el Overworld.
2. En la esquina superior izquierda de la pantalla verás el recuadro:  
   `MMO [SALA] Jugador`  
   *(En rojo si aún no pusiste la IP, o verde cuando conecte).*
3. Toca el botón táctil **SELECT** en la pantalla de tu celular para abrir el menú:
   - **Configuración de Red y Perfil → Cambiar IP del Servidor:** Escribe la IP del anfitrión.
   - **Configuración de Red y Perfil → Cambiar Codigo de Sala:** Escriban la misma sala todos (ej: `1234` o `AZAH`).
   - **Configuración de Red y Perfil → Configurar mi Nombre:** Escribe tu apodo.
4. **¡CALIBRACIÓN INDISPENSABLE (Solo toma 3 segundos)!**:
   - Entra en **Calibración y Ajustes Visuales**.
   - Deja a tu personaje quieto y pulsa: **`1. [PASO 1] Registrar Posicion Inicial`**.
   - Camina 3 pasos hacia cualquier lado y pulsa: **`2. [PASO 2] Confirmar Movimiento (Caminar 3 pasos)`**.
5. Toca el botón táctil **B** para cerrar el menú y volver al juego.  
**¡El recuadro se pondrá verde y verás a tus amigos en tiempo real!**

---

## 🗺️ Paso 5: Ver el Mapa de Hoenn en Pantalla Táctil
* Toca la pantalla inferior para activar el **AreaNav**.
* Verás rombos luminosos con los nombres de tus amigos en sus respectivas ciudades o rutas, indicando la distancia a la que están si comparten la misma zona.
* El radar de interiores se apaga inteligentemente cuando entras al Centro Pokémon para garantizar que tu cámara y tu juego no se queden trabados.

---

## 💬 Chat y Teclado Táctil
* Toca **SELECT** → **Chat y Emotes** → **Enviar Chat Rapido**.
* Selecciona **`>> Escribir Mensaje Personalizado <<`**.
* Se abrirá el teclado virtual de tu celular para que redactes cualquier mensaje libremente.
* También puedes enviar emotes y caritas animadas sobre tu personaje.

---

## ⚔️ Combates e Intercambios Multijugador (PSS)
1. Conéctate a la sala de Azahar en el menú desplegable del emulador (**Multijugador → Unirse a sala**).
2. En la pantalla táctil de tu juego, ve al **PSS** y toca el icono azul de Wi-Fi arriba a la derecha.
3. Verás a tus amigos en la lista para empezar un combate oficial o intercambiar Pokémon.

---

## 🛠️ Preguntas Frecuentes en Android

* **El juego se cierra al abrir o al iniciar la partida:**  
  Casi siempre se debe a tener seleccionado **Vulkan** en la configuración de gráficos. Cámbialo a **OpenGL ES** en *Ajustes → Gráficos*.
* **El menú SELECT no responde al tocarlo:**  
  Asegúrate de tocar el botón virtual SELECT de los controles en pantalla de Azahar. Si usas un mando Bluetooth (como un mando de Xbox o PS4/PS5), presiona el botón físico asignado a SELECT.
* **Mis amigos se ven parados o fuera de lugar:**  
  Asegúrate de haber hecho el **Paso 1** (quieto) y el **Paso 2** (dar 3 pasos) en el menú SELECT.
