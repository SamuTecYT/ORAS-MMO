# Generador del paquete de acompañantes 3D (`mmo_pokes_3d.bin`)

[English](README.md)

Los Pokémon acompañantes en 3D del ORAS MMO se dibujan a partir de `mmo_pokes_3d.bin`. Ese archivo sale de los
modelos 3D del propio juego, así que **no** se reparte aquí. Esta herramienta lo crea en tu PC a partir de **tu propia
copia** de Pokémon Rubí Omega o Zafiro Alfa: los 721 Pokémon, 5 direcciones, un cuadro quieto y 8 de paso (o de
reposo) cada uno.

## Qué necesitas

- **Tu propia copia legal** de Pokémon Rubí Omega o Zafiro Alfa, de una de estas formas:
  - un archivo `.3ds` / `.cci` o `.cxi` **descifrado** (el que usa Azahar), o
  - una carpeta con la **RomFS extraída** del juego (la carpeta que contiene `a/0/0/8`).

  Si el archivo está cifrado, el programa avisa y se para. Si solo tienes un `.cia`, extrae antes su RomFS con una
  herramienta como GodMode9 (en tu consola), ctrtool o 3dstool.
- **Python 3.10 o más nuevo** ([python.org](https://www.python.org/downloads/)).
- Una tarjeta gráfica con **OpenGL 3.3** (casi cualquier PC de los últimos 10 años, con el controlador al día).

## Pasos

Abre una terminal en esta carpeta (`tools/pokepack`) y ejecuta:

```
python -m pip install -r requirements.txt
python make_3d_pack.py --rom "C:\ruta\a\tu_juego.3ds"
```

Tarda unos 10 minutos con 4 procesos (más en un PC lento) y deja `mmo_pokes_3d.bin` (unos 31 MB) en la carpeta
donde estés.

Primero puedes hacer una prueba rápida (Bulbasaur, Pikachu y Salamence, unos segundos) con una imagen de muestra:

```
python make_3d_pack.py --rom "C:\ruta\a\tu_juego.3ds" --only 1,25,373 -o mmo_pokes_prueba.bin --sheet muestra.png
```

Después copia `mmo_pokes_3d.bin` junto al plugin, en la misma carpeta que `OrasMmo.3gx`:
`<carpeta de usuario de Azahar>/sdmc/luma/plugins/000400000011C400/` (Rubí Omega) y/o `.../000400000011C500/`
(Zafiro Alfa). Reinicia el juego: tu primer Pokémon ya te sigue.

## Opciones

| Opción | Qué hace |
|---|---|
| `--rom RUTA` | `.3ds` / `.cci` / `.cxi` descifrado, o la carpeta con la RomFS extraída (obligatoria) |
| `-o ARCHIVO` | Archivo de salida (por defecto `mmo_pokes_3d.bin`) |
| `--workers N` | Procesos a la vez (por defecto 4; pon 1 si tu PC se queda sin memoria) |
| `--only 1,25,373` | Solo estos números de la Pokédex Nacional (para pruebas) |
| `--sheet ARCHIVO.png` | Guarda también una imagen con los cuadros del paso, para revisar el resultado |

## Conviene saber

- El `.bin` contiene gráficos del juego: **es solo para ti**, no lo subas ni lo compartas. El `.gitignore` del
  repositorio y `tools/prepublish_check.py` ya bloquean `mmo_pokes_*.bin`.
- Con otra tarjeta gráfica u otra versión de las librerías pueden cambiar algunos píxeles. No pasa nada: el plugin lee
  el archivo igual.
- Archivos: `make_3d_pack.py` (programa principal y formato del archivo), `bake.py` (cámara, tamaño, pose), `walk.py`
  (paso), `pkrender.py` (dibujo con OpenGL), `pkmodel.py` / `pkanim.py` (modelos y animaciones de los Pokémon),
  `bch.py` / `pica.py` (formato de modelos de 3DS), `gfmotion.py` (movimientos de Game Freak), `romfs.py` / `lgarc.py`
  (lectura de los archivos del juego).
