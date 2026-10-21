<p align="center">
  <img src="../../docs/img/followers.png" alt="Un entrenador con un Salamence 3D siguiéndole" width="560"/>
</p>

# 🐉 Generador del paquete de Pokémon 3D (`mmo_pokes_3d.bin`)

[English](README.md) · **Español**

Los Pokémon acompañantes en 3D del ORAS MMO se dibujan a partir de `mmo_pokes_3d.bin`. Ese archivo sale de los
modelos 3D del propio juego, así que **no** se reparte aquí. Esta herramienta lo crea en tu PC a partir de **tu propia
copia** de Pokémon Rubí Omega o Zafiro Alfa: los 721 Pokémon, 5 direcciones, un cuadro quieto y 8 de paso (o de
reposo) cada uno.

## ✅ Qué necesitas

- [ ] **Tu propia copia** de Pokémon Rubí Omega o Zafiro Alfa, de una de estas formas:
  - un archivo `.3ds` / `.cci` o `.cxi` **descifrado** (el que usa Azahar), o
  - una carpeta con la **RomFS extraída** del juego (la carpeta que contiene `a/0/0/8`).
- [ ] **Python 3.10 o más nuevo** ([python.org](https://www.python.org/downloads/); en Windows marca ✅ *Add Python to
  PATH*).
- [ ] Una tarjeta gráfica con **OpenGL 3.3**: casi cualquier PC de los últimos 10 años, con el controlador al día.

> [!NOTE]
> Si el archivo está cifrado, el programa avisa y se para. Si solo tienes un `.cia`, extrae antes su RomFS con una
> herramienta como GodMode9 (en tu consola), ctrtool o 3dstool.

## 🚀 Pasos

1. Abre una terminal en esta carpeta (`tools/pokepack`).
2. Instala lo que necesita la herramienta (solo la primera vez):
   ```bash
   python -m pip install -r requirements.txt
   ```
3. **Prueba rápida, opcional** (Bulbasaur, Pikachu y Salamence, unos segundos), con una imagen de muestra:
   ```bash
   python make_3d_pack.py --rom "C:\ruta\a\tu_juego.3ds" --only 1,25,373 -o mmo_pokes_prueba.bin --sheet muestra.png
   ```
4. **Crea el paquete completo.** Tarda unos **10 minutos** con 4 procesos (más en un PC lento):
   ```bash
   python make_3d_pack.py --rom "C:\ruta\a\tu_juego.3ds"
   ```
   Obtienes `mmo_pokes_3d.bin` (unos 31 MB) en la carpeta donde estés.
5. **Cópialo junto al plugin** y reinicia el juego. ¡Tu primer Pokémon ya te sigue! 🎉

| Dónde juegas | Copia `mmo_pokes_3d.bin` en |
|---|---|
| 🪟 PC, Rubí Omega | `<carpeta de usuario de Azahar>/sdmc/luma/plugins/000400000011C400/` |
| 🪟 PC, Zafiro Alfa | `<carpeta de usuario de Azahar>/sdmc/luma/plugins/000400000011C500/` |
| 🤖 Android | `sdmc/luma/plugins/` de Azahar (junto a `default.3gx`) |

> [!TIP]
> En Windows también puedes ponerlo junto a `INSTALL_PC.bat` y volver a abrir el instalador: lo copia para los dos
> juegos.

## ⚙️ Opciones

| Opción | Qué hace |
|---|---|
| `--rom RUTA` | `.3ds` / `.cci` / `.cxi` descifrado, o la carpeta con la RomFS extraída (obligatoria) |
| `-o ARCHIVO` | Archivo de salida (por defecto `mmo_pokes_3d.bin`) |
| `--workers N` | Procesos a la vez (por defecto 4; pon 1 si tu PC se queda sin memoria) |
| `--only 1,25,373` | Solo estos números de la Pokédex Nacional (para pruebas) |
| `--sheet ARCHIVO.png` | Guarda también una imagen con los cuadros del paso, para revisar el resultado |

## 💡 Conviene saber

> [!WARNING]
> El `.bin` contiene gráficos del juego: **es solo para ti**, no lo subas ni lo compartas. El `.gitignore` del
> repositorio y `tools/prepublish_check.py` ya bloquean `mmo_pokes_*.bin`.

- Con otra tarjeta gráfica u otra versión de las librerías pueden cambiar algunos píxeles. No pasa nada: el plugin lee
  el archivo igual.
- Archivos: `make_3d_pack.py` (programa principal y formato del archivo), `bake.py` (cámara, tamaño, pose), `walk.py`
  (paso), `pkrender.py` (dibujo con OpenGL), `pkmodel.py` / `pkanim.py` (modelos y animaciones de los Pokémon),
  `bch.py` / `pica.py` (formato de modelos de 3DS), `gfmotion.py` (movimientos de Game Freak), `romfs.py` / `lgarc.py`
  (lectura de los archivos del juego).
