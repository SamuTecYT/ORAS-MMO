# 🖥️ Pokémon ORAS MMO v11.0 — Guía del Servidor de Sincronización

Este servidor de relé TCP ultrarrápido y liviano sincroniza en tiempo real los movimientos, posiciones, mapas, chats e interacciones de todos los jugadores de PC y Android.

---

## ⚡ Cómo Iniciar el Servidor en 1 Clic

1. Haz doble clic en el archivo **`run_server.bat`**.
2. Se abrirá una ventana de consola con letras verdes:
   - Verificará que tengas instalado Python 3.
   - Te mostrará tu dirección **IP Local** (para amigos que jueguen en tu misma casa / Wi-Fi).
   - Te mostrará instrucciones para jugar por Internet con **ZeroTier**.
   - Mostrará: `Servidor ORAS MMO ACTIVO en el puerto 9000 (TCP)`.
3. **Mantén esta ventana abierta mientras jueguen.** Al terminar la sesión, puedes cerrarla normalmente.

---

## 🌐 Jugar por Internet con Amigos (Guía de ZeroTier)

**ZeroTier** crea una red virtual privada y segura que permite que amigos jueguen juntos desde cualquier parte del mundo sin tener que abrir puertos en el router del hogar.

### 1. Instalar ZeroTier
* En PC: Ejecuta el instalador **`ZeroTier One.msi`** (incluido en la carpeta `ZeroTier/` o `Para_PC/`).
* En Android: Tus amigos con celular lo instalan gratis desde **Google Play Store** buscando "ZeroTier One".

### 2. Crear la Red (Solo toma 1 minuto)
1. Entra en [my.zerotier.com](https://my.zerotier.com) y crea una cuenta gratuita.
2. Haz clic en **"Create a Network"**.
3. Verás un código de 16 caracteres llamado **Network ID** (ejemplo: `8056c2e21c000001`). Copia ese código y compártelo con tus amigos.

### 3. Unirse a la Red
* **En PC:** Haz clic derecho en el icono de ZeroTier al lado del reloj de Windows → **Join New Network...** → Pega el código de 16 dígitos y pulsa **Join**.
* **En Android:** Abre la app ZeroTier One, toca el botón **`+`**, pega el código de 16 dígitos y activa el interruptor.

### 4. Autorizar a los Miembros (Paso de Seguridad)
1. En tu panel de [my.zerotier.com](https://my.zerotier.com), entra a la red creada y baja a la sección **"Members"**.
2. Verás aparecer a tus amigos conectados. Marca la casilla **"Auth"** de cada uno para autorizarlos.

### 5. Obtener la IP para el Juego
* En la misma tabla de miembros de ZeroTier, copia la **"Managed IP"** de tu computadora (suele empezar con `10.147.x.x` o `192.168.19x.x`).
* Esa es la dirección IP que todos los jugadores (incluido tú si juegas desde otra máquina) deben ingresar en el menú del juego (**SELECT → Configuración de Red y Perfil → Cambiar IP del Servidor**).

---

## 🏠 Jugar en la Misma Casa (Wi-Fi Local)

Si todos los jugadores están conectados al mismo módem o router Wi-Fi de la casa:
1. No necesitas ZeroTier ni Internet exterior.
2. Abre `run_server.bat` y mira la **IP Local** que aparece en pantalla (ejemplo: `192.168.1.15`).
3. Tus amigos simplemente escriben esa IP local en el menú SELECT de su juego.

---

## ⚙️ Especificaciones Técnicas del Servidor

* **Protocolo:** TCP binario ultraligero y asíncrono con `asyncio`.
* **Puerto:** 9000 TCP (bidireccional).
* **Consumo:** Menos de 20 MB de memoria RAM y 0% de uso de CPU.
* **Capacidad:** Hasta 6 jugadores por sala (soporta múltiples salas independientes al mismo tiempo cambiando el código de sala).
* **Sin librerías externas:** Funciona con Python 3 puro sin requerir `pip` adicional.

---

## 🛠️ Solución de Problemas Comunes

### 1. "Python no se reconoce como un comando interno o externo"
* **Causa:** No tienes instalado Python 3 en tu PC o no marcaste la casilla del sistema al instalarlo.
* **Solución:** Descarga e instala Python 3 (gratis desde [python.org](https://www.python.org/downloads/)). Durante la instalación, **marca obligatoriamente la casilla "Add python.exe to PATH"** en la primera ventana.

### 2. Error "[Errno 10048] address already in use"
* **Causa:** Ya tienes otra ventana de `server.py` ejecutándose en segundo plano ocupando el puerto 9000.
* **Solución:** Abre el Administrador de Tareas de Windows (Ctrl + Shift + Esc), busca los procesos llamados "Python" y haz clic en "Finalizar tarea". Luego vuelve a abrir `run_server.bat`.

### 3. Mis amigos dicen que el juego queda en "Conectando a..." (en rojo)
* **Causa 1:** El Firewall de Windows está bloqueando la entrada de conexiones a Python.  
  *Solución:* Ve a la configuración del Firewall de Windows y asegúrate de permitir conexiones entrantes para Python tanto en redes públicas como privadas, o permite el puerto TCP 9000.
* **Causa 2:** En ZeroTier olvidaste marcar la casilla **"Auth"** para autorizar al amigo en el panel web.
* **Causa 3:** Escribieron mal la IP en el menú del juego. Verifica los números.

### 4. Aparece el mensaje "ERROR:ROOM_FULL"
* **Causa:** La sala alcanzó el límite de 6 jugadores concurrentes.
* **Solución:** Cambia el código de sala en el juego (SELECT → Cambiar Código de Sala) a otro valor (por ejemplo `5678`) para abrir una sala nueva e independiente.
