# 🔒 Security policy / Política de seguridad

## 🚨 Reporting a vulnerability / Avisar de una vulnerabilidad

> [!IMPORTANT]
> Please **do not open a public issue**. Use GitHub's private reporting instead:
> **Security → Report a vulnerability** ([direct link](https://github.com/SamuTecYT/ORAS-MMO/security/advisories/new)).
>
> Por favor, **no abras un issue público**. Usa el aviso privado de GitHub:
> **Security → Report a vulnerability** ([enlace directo](https://github.com/SamuTecYT/ORAS-MMO/security/advisories/new)).

Tell us / Cuéntanos:

- 🔎 what you found / qué encontraste;
- 🧪 how to reproduce it / cómo reproducirlo;
- 🏷️ which version you used, plugin and server / qué versión usaste, del plugin y del servidor.

## ✅ Supported versions / Versiones con soporte

| Version / Versión | Protocol / Protocolo | Fixes / Arreglos |
|---|:---:|:---:|
| 17.x (latest / la última) | 16 | ✅ |
| Older / Anteriores | — | ❌ |

## 🎯 Scope / Alcance

| Part / Parte | What counts / Qué cuenta |
|---|---|
| 🖥️ `server/server.py` | Crashes, memory or disk exhaustion, ways around the room or connection limits / Cuelgues, agotar memoria o disco, saltarse los límites de salas o conexiones |
| 🎮 The plugin / El plugin | Anything a server or a file (`mmo_*.txt`, `mmo_pokes_3d.bin`) could send to crash Azahar or run code / Cualquier cosa que un servidor o un archivo pueda mandar para colgar Azahar o ejecutar código |
| 📦 Leaks / Filtraciones | Anything in the repository or the release ZIP that exposes private data / Cualquier cosa del repositorio o del ZIP que exponga datos privados |

> [!NOTE]
> By design there are **no accounts**: the room code is a shared secret and nicknames are not verified (see
> [Security and privacy](README.md#security-and-privacy)). That is not a vulnerability by itself.
>
> Por diseño **no hay cuentas**: el código de sala es un secreto compartido y los apodos no se verifican (ver
> [Seguridad y privacidad](README_ES.md#seguridad-y-privacidad)). Eso no es una vulnerabilidad en sí.
