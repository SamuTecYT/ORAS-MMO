# Contributing / Contribuir

**English** · [Español](#español)

Thanks for wanting to improve Pokémon ORAS MMO!

1. **Read** [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (how the plugin is organized and the rules that keep it stable)
   and [docs/PROTOCOL.md](docs/PROTOCOL.md) (network format).
2. **Open an issue first** for big changes (new features, protocol changes) so we can agree on the approach.
3. **Keep the protocol compatible.** If you must change the 64-byte packet, bump `MMO_PROTOCOL_VERSION` and update
   `server/server.py` and its tests in the same pull request.
4. **Texts**: never hardcode a new UI text in one language only. Add it to `3ds_plugin/tools/i18n/strings.py` (all 7
   languages; English is used if you can't translate) and run `python 3ds_plugin/tools/i18n/gen_lang.py`.
5. **Before sending a pull request**:
   - the plugin builds without new warnings (`make` in `3ds_plugin/`);
   - server tests pass: `python -m unittest server/test_server_ranking.py server/test_server_limits.py`;
   - you tested it in Azahar (say what you tested: game, PC/Android, number of players);
   - `python tools/prepublish_check.py` reports nothing private.
6. **Never commit** game files or anything derived from them (ROMs, RomFS dumps, `mmo_pokes_*.bin`, extracted
   models/fonts/textures), builds (`.3gx`, ZIPs), rankings, `.env`, or your `ServerConfig.local.hpp`.
7. Security problems: follow [SECURITY.md](SECURITY.md), not a public issue.

Code style: follow the surrounding code (4 spaces, `PascalCase` functions, `s_` for file statics, short comments that
explain *why*). By contributing you agree your code is released under the [MIT License](LICENSE).

---

## Español

¡Gracias por querer mejorar Pokémon ORAS MMO!

1. **Lee** [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (cómo está organizado el plugin y las reglas que lo mantienen
   estable) y [docs/PROTOCOL.md](docs/PROTOCOL.md) (formato de red).
2. **Abre primero un issue** para cambios grandes (funciones nuevas, cambios del protocolo) y acordamos cómo hacerlo.
3. **Mantén el protocolo compatible.** Si cambias el paquete de 64 bytes, sube `MMO_PROTOCOL_VERSION` y actualiza
   `server/server.py` y sus pruebas en el mismo pull request.
4. **Textos**: nunca pongas un texto nuevo solo en un idioma. Añádelo en `3ds_plugin/tools/i18n/strings.py` (los 7
   idiomas; si no sabes traducirlo, se usa el inglés) y ejecuta `python 3ds_plugin/tools/i18n/gen_lang.py`.
5. **Antes de mandar un pull request**:
   - el plugin compila sin avisos nuevos (`make` en `3ds_plugin/`);
   - pasan las pruebas del servidor (`python -m unittest server/test_server_ranking.py server/test_server_limits.py`);
   - lo probaste en Azahar (cuenta qué probaste: juego, PC/Android, cuántos jugadores);
   - `python tools/prepublish_check.py` no encuentra nada privado.
6. **Nunca subas** archivos del juego ni nada sacado de él (ROMs, RomFS, `mmo_pokes_*.bin`, modelos, letras o texturas
   extraídos), compilaciones (`.3gx`, ZIP), rankings, `.env` ni tu `ServerConfig.local.hpp`.
7. Problemas de seguridad: sigue [SECURITY.md](SECURITY.md), no abras un issue público.

Estilo: imita el código de alrededor (4 espacios, funciones en `PascalCase`, `s_` para estáticos del archivo, comentarios
cortos que expliquen el *porqué*). Al contribuir aceptas que tu código se publique con la [licencia MIT](LICENSE).
