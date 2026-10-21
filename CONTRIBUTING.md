<a id="english"></a>
# 🤝 Contributing to Pokémon ORAS MMO

**English** · [Español](#español)

Thanks for wanting to help! 💙 You don't need to be an expert: reporting a bug or fixing a translation helps as much
as writing code.

## 🌟 Ways to help

| | How | Where to start |
|:---:|---|---|
| 🐛 | **Report a bug** | [Open a bug report](https://github.com/SamuTecYT/ORAS-MMO/issues/new/choose) and attach `mmo_log.txt` |
| 💡 | **Suggest an idea** | [Open a feature request](https://github.com/SamuTecYT/ORAS-MMO/issues/new/choose) |
| 🌐 | **Improve a translation** | Edit `3ds_plugin/tools/i18n/strings.py` (all 7 languages live there) |
| 🧪 | **Test it** | Try new versions on Android, Linux or macOS and tell us what you saw |
| 🛠️ | **Write code** | Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), then follow the steps below |

## 🚀 Your first pull request

1. **Fork** the repository (button at the top right) and clone your fork.
2. **Create a branch** for your change: `git checkout -b fix-chat-colors`.
3. **Make your change.** For big ones (new features, protocol changes), [open an issue](https://github.com/SamuTecYT/ORAS-MMO/issues)
   first so we can agree on the approach.
4. **Check it** with the list below.
5. **Open the pull request** and say what you changed and how you tested it.

> [!TIP]
> No devkitPro on your PC? Just open the pull request: the checks build the plugin and run the server tests
> for you (or, in your fork: **Actions → Build plugin and test server → Run workflow**).

## ✅ Before you send it

- [ ] `python tools/pr_guard.py` (your commits vs `origin/main`) says 🟢 or 🟡.
- [ ] The plugin builds **without new warnings** (`make EXTRA_CXXFLAGS="-Wall -Wextra -Wno-unused-parameter"` in
      `3ds_plugin/`, or the Actions workflow above).
- [ ] The server tests pass: `python -m unittest server/test_server_ranking.py server/test_server_limits.py` and
      `python server/regression_server.py`.
- [ ] You tested it **in Azahar**, and you say how: game, PC or Android, number of players.
- [ ] `python tools/prepublish_check.py` reports nothing private.

## 🤖 What happens when you open a pull request

1. 🛡️ **The guard** (`tools/pr_guard.py`, always the copy from `main`: your pull request can't change it) reads your
   change, **without running it**, and comments a report:
   - 🟢 nothing sensitive;
   - 🟡 sensitive area (game memory, network, protocol, server limits...): the maintainer reviews it and tests it in
     Azahar;
   - 🔴 blocked until you fix it: game files or builds, binaries, files over 1 MB, secrets, real IP addresses or web
     addresses in the plugin or the server, a different default server, new writes into the game's memory, code or save
     outside the approved files, network code outside `NetworkClient`, server code that runs programs or connects out,
     new server dependencies, plugin and server no longer agreeing on the protocol, or **protected files**.
2. ⚙️ **The checks** build the plugin (no new compiler warnings compared with `main`), run the server's unit and
   regression tests, check the protocol and the translations, and attach the built `.3gx` (for testing only).
   If it's your first pull request here, they wait until the maintainer allows them to run.
3. 👀 **The maintainer reviews** it (every change needs their approval) and merges it with *Squash and merge*.
   Merging never installs anything: the server and the releases are updated by hand, later.

🔒 **Protected files** (only the maintainer changes them): `.github/` (workflows, `CODEOWNERS`), `tools/pr_guard.py`,
`tools/privacy.py`, `tools/prepublish_check.py`, `tools/package_public.py`, `packaging/` (what players run),
`LICENSE`, `SECURITY.md` and `.gitattributes`. If you think one needs a change, open an issue.

## 📏 Golden rules

- 📖 **Read the guides:** [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (how the plugin is organized and the rules that
  keep it stable) and [docs/PROTOCOL.md](docs/PROTOCOL.md) (network format).
- 🔌 **Keep the protocol compatible.** If you must change the 64-byte packet, bump `MMO_PROTOCOL_VERSION` and update
  `server/server.py` and its tests in the same pull request.
- 🌐 **Every text in all 7 languages.** Never hardcode a new menu text in one language. Add it to
  `3ds_plugin/tools/i18n/strings.py` (English is used where you can't translate) and run
  `python 3ds_plugin/tools/i18n/gen_lang.py`.
- 🎨 **Code style:** follow the code around it: 4 spaces, `PascalCase` functions, `s_` for file statics, and short
  comments that explain *why*.

> [!WARNING]
> **Never commit** game files or anything made from them (ROMs, RomFS dumps, `mmo_pokes_*.bin`, extracted models,
> fonts or textures), builds (`.3gx`, ZIPs), rankings, `.env`, or your `ServerConfig.local.hpp`.

🔒 **Security problems** go through [SECURITY.md](SECURITY.md), never a public issue.

By contributing, you agree that your code is released under the [MIT License](LICENSE).

---

<a id="español"></a>
# 🤝 Colaborar con Pokémon ORAS MMO

[English](#english) · **Español**

¡Gracias por querer ayudar! 💙 No hace falta ser experto: avisar de un fallo o corregir una traducción ayuda tanto
como programar.

## 🌟 Formas de ayudar

| | Cómo | Por dónde empezar |
|:---:|---|---|
| 🐛 | **Avisar de un fallo** | [Abre un aviso de fallo](https://github.com/SamuTecYT/ORAS-MMO/issues/new/choose) y adjunta `mmo_log.txt` |
| 💡 | **Proponer una idea** | [Abre una propuesta](https://github.com/SamuTecYT/ORAS-MMO/issues/new/choose) |
| 🌐 | **Mejorar una traducción** | Edita `3ds_plugin/tools/i18n/strings.py` (ahí están los 7 idiomas) |
| 🧪 | **Probarlo** | Prueba las versiones nuevas en Android, Linux o macOS y cuéntanos qué viste |
| 🛠️ | **Programar** | Lee [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) y sigue los pasos de abajo |

## 🚀 Tu primer pull request

1. Haz un **fork** del repositorio (botón arriba a la derecha) y clona tu fork.
2. **Crea una rama** para tu cambio: `git checkout -b arreglar-colores-chat`.
3. **Haz tu cambio.** Si es grande (funciones nuevas, cambios del protocolo),
   [abre antes un issue](https://github.com/SamuTecYT/ORAS-MMO/issues) y acordamos cómo hacerlo.
4. **Revísalo** con la lista de abajo.
5. **Abre el pull request** y cuenta qué cambiaste y cómo lo probaste.

> [!TIP]
> ¿No tienes devkitPro en tu PC? Abre el pull request y las comprobaciones compilan el plugin y pasan las
> pruebas por ti (o, en tu fork: **Actions → Build plugin and test server → Run workflow**).

## ✅ Antes de enviarlo

- [ ] `python tools/pr_guard.py` (tus commits frente a `origin/main`) dice 🟢 o 🟡.
- [ ] El plugin compila **sin avisos nuevos** (`make EXTRA_CXXFLAGS="-Wall -Wextra -Wno-unused-parameter"` en
      `3ds_plugin/`, o el workflow de Actions de arriba).
- [ ] Pasan las pruebas del servidor: `python -m unittest server/test_server_ranking.py server/test_server_limits.py`
      y `python server/regression_server.py`.
- [ ] Lo probaste **en Azahar**, y cuentas cómo: juego, PC o Android, cuántos jugadores.
- [ ] `python tools/prepublish_check.py` no encuentra nada privado.

## 🤖 Qué pasa al abrir un pull request

1. 🛡️ **El guardián** (`tools/pr_guard.py`, siempre la copia de `main`: tu pull request no puede cambiarlo) lee tu
   cambio, **sin ejecutarlo**, y deja un informe:
   - 🟢 nada delicado;
   - 🟡 zona delicada (memoria del juego, red, protocolo, límites del servidor...): quien mantiene el proyecto lo
     revisa y lo prueba en Azahar;
   - 🔴 bloqueado hasta que lo corrijas: archivos del juego o compilaciones, binarios, archivos de más de 1 MB,
     secretos, direcciones IP reales o direcciones web en el plugin o el servidor, otro servidor por defecto,
     escrituras nuevas en la memoria, el código o la partida del juego fuera de los archivos aprobados, código de red
     fuera de `NetworkClient`, un servidor que ejecuta programas o se conecta hacia fuera, dependencias nuevas del
     servidor, plugin y servidor que ya no se entienden, o **archivos protegidos**.
2. ⚙️ **Las comprobaciones** compilan el plugin (sin avisos nuevos frente a `main`), pasan las pruebas del servidor
   (normales y de regresión), revisan el protocolo y las traducciones, y adjuntan el `.3gx` compilado (solo para
   probar). Si es tu primer pull request aquí, esperan a que quien mantiene el proyecto les dé permiso.
3. 👀 **Quien mantiene el proyecto lo revisa** (todo cambio necesita su aprobación) y lo acepta con *Squash and
   merge*. Aceptarlo no instala nada: el servidor y las publicaciones se actualizan a mano, después.

🔒 **Archivos protegidos** (solo los cambia quien mantiene el proyecto): `.github/` (workflows, `CODEOWNERS`),
`tools/pr_guard.py`, `tools/privacy.py`, `tools/prepublish_check.py`, `tools/package_public.py`, `packaging/` (lo que
ejecutan los jugadores), `LICENSE`, `SECURITY.md` y `.gitattributes`. Si crees que uno necesita un cambio, abre un issue.

## 📏 Reglas de oro

- 📖 **Lee las guías:** [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (cómo está organizado el plugin y las reglas que
  lo mantienen estable) y [docs/PROTOCOL.md](docs/PROTOCOL.md) (formato de red).
- 🔌 **Mantén el protocolo compatible.** Si cambias el paquete de 64 bytes, sube `MMO_PROTOCOL_VERSION` y actualiza
  `server/server.py` y sus pruebas en el mismo pull request.
- 🌐 **Cada texto, en los 7 idiomas.** Nunca pongas un texto nuevo del menú en un solo idioma. Añádelo en
  `3ds_plugin/tools/i18n/strings.py` (si no sabes traducirlo, se usa el inglés) y ejecuta
  `python 3ds_plugin/tools/i18n/gen_lang.py`.
- 🎨 **Estilo:** imita el código de alrededor: 4 espacios, funciones en `PascalCase`, `s_` para estáticos del archivo
  y comentarios cortos que expliquen el *porqué*.

> [!WARNING]
> **Nunca subas** archivos del juego ni nada sacado de él (ROMs, RomFS, `mmo_pokes_*.bin`, modelos, letras o texturas
> extraídos), compilaciones (`.3gx`, ZIP), rankings, `.env` ni tu `ServerConfig.local.hpp`.

## 🧑‍💼 Para quien mantiene el proyecto

- Si es de alguien nuevo, GitHub te pide **«Approve and run workflows»**: púlsalo solo cuando el guardián diga 🟢 o 🟡
  (así las comprobaciones nunca ejecutan algo que el guardián ya marcó en 🔴).
- 🟢 y todo en verde: *Squash and merge*.
- 🟡: antes de aceptarlo, pide una revisión del código (en Claude: `/requesting-code-review`) y prueba en un Azahar de pruebas el `.3gx` de la comprobación *plugin* (nunca en tu partida de verdad).
- 🔴: no lo aceptes; quien lo envió tiene que corregirlo. Saltarte el bloqueo solo tras una revisión experta.
- Nada se instala solo: después de aceptar, el servidor del VPS y los ZIP se actualizan a mano, como siempre.

🔒 Los **problemas de seguridad** van por [SECURITY.md](SECURITY.md), nunca en un issue público.

Al colaborar, aceptas que tu código se publique con la [licencia MIT](LICENSE).
