# Security policy / Política de seguridad

## Reporting a vulnerability / Avisar de una vulnerabilidad

Please **do not open a public issue**. Use GitHub's private reporting instead:
**Security → Report a vulnerability** ([direct link](../../security/advisories/new)).
Include what you found, how to reproduce it and which version (plugin and server) you used.

Por favor, **no abras un issue público**. Usa el aviso privado de GitHub:
**Security → Report a vulnerability** ([enlace directo](../../security/advisories/new)).
Explica qué encontraste, cómo reproducirlo y qué versión (plugin y servidor) usaste.

## Supported versions / Versiones con soporte

Only the latest release (plugin 17.x, protocol 16) receives fixes.
Solo la última versión (plugin 17.x, protocolo 16) recibe arreglos.

## Scope / Alcance

- `server/server.py` (relay): crashes, memory/disk exhaustion, ways to bypass the room or connection limits.
- The plugin: anything a server or a file (`mmo_*.txt`, `mmo_pokes_3d.bin`) could send to crash Azahar or run code.
- Leaks: anything in the repository or the release ZIP that exposes private data.

By design there are **no accounts**: the room code is a shared secret and nicknames are not authenticated
(see "Security and privacy" in the [README](README.md#security-and-privacy)). That is not a vulnerability by itself.

Por diseño **no hay cuentas**: el código de sala es un secreto compartido y los apodos no se verifican (ver "Seguridad y
privacidad" en el [README](README_ES.md#seguridad-y-privacidad)). Eso no es una vulnerabilidad en sí.
