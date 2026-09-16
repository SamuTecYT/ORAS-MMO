# Guía de Instalación - ORAS MMO Plugin (Azahar)

Esta guía te ayudará a instalar y jugar al mod multijugador de Pokémon Rubí Omega y Zafiro Alfa (ORAS MMO) utilizando el emulador Azahar.

## 1. Instalación en PC (Windows)

1. Descarga el archivo compilado `OrasMmo.3gx`.
2. Abre la carpeta donde tienes instalado el emulador **Azahar**.
3. Navega a la siguiente ruta exacta:
   `[Carpeta_de_Azahar]\sdmc\luma\plugins\[Title_ID_del_juego]\`
   *Nota: El Title ID para Pokémon Rubí Omega es `000400000011C400` y para Zafiro Alfa es `000400000011C500`.*
   *Si las carpetas `luma` o `plugins` no existen, créalas.*
4. Pega el archivo `OrasMmo.3gx` dentro de esa carpeta.

## 2. Instalación en Android

1. Copia el archivo `OrasMmo.3gx` a tu dispositivo Android.
2. Abre tu explorador de archivos y navega a la carpeta de datos de **Azahar Android** (generalmente `Azahar/sdmc/`).
3. Ve a la ruta exacta:
   `Azahar\sdmc\luma\plugins\[Title_ID_del_juego]\`
   *Nota: El Title ID para Pokémon Rubí Omega es `000400000011C400` y para Zafiro Alfa es `000400000011C500`.*
   *Si las carpetas no existen, créalas.*
4. Pega allí el archivo `OrasMmo.3gx`.

## 3. Activar los Plugins en Azahar

Para que el mod funcione, debes habilitar la carga de plugins en los ajustes del emulador:
1. Abre Azahar.
2. Ve a **Configuración** (Settings) -> **Sistema** (System) o **Luma3DS**.
3. Busca la opción **Enable Plugin Loader** (Activar Carga de Plugins) y actívala.
4. Inicia el juego Pokémon ORAS.

## 4. Configurar la IP del Servidor

Por defecto, el plugin intenta conectarse a `127.0.0.1` (localhost). Para jugar con amigos:
1. Crea un archivo de texto llamado `mmo_ip.txt` en la raíz de la tarjeta SD (la carpeta `sdmc` de Azahar).
2. Escribe la dirección IP del servidor en la primera línea del archivo (por ejemplo, `192.168.1.50` o la IP de Hamachi/ZeroTier).
3. Guarda el archivo y reinicia el emulador.

## 5. Jugar

- Al iniciar el juego, verás en la pantalla superior (TOP screen) un texto superpuesto que dice:
  `MMO: 0 jugadores conectados`
  `Sala: 1234`
- El plugin intentará conectarse automáticamente al servidor. Cuando te conectes, el texto se actualizará.
- Verás a los demás jugadores renderizados en el juego 3D con los modelos de Bruno y Aura (Brendan y May).

## Problemas Frecuentes (Troubleshooting)

- **El juego se congela:** Asegúrate de estar usando la versión correcta del juego. Los ROMs muy modificados (Randomizers) pueden causar fallos, aunque el mod intenta buscar las direcciones de memoria automáticamente.
- **No veo a mis amigos:** Verifica que tengas internet y que todos estén en la misma sala ("1234").
- **No aparece el texto MMO:** Revisa el paso 3 para asegurar que el "Plugin Loader" está habilitado.
- **No conecta al servidor (Android/PC):** Asegúrate de haber creado el archivo `mmo_ip.txt` con la IP correcta y de que tu firewall permite la conexión por el puerto 9000.
