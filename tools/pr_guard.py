"""PR guard / guardián de pull requests — Pokémon ORAS MMO.

Checks what a pull request changes BEFORE anyone builds, runs or merges it:

    python tools/pr_guard.py                       # your commits vs origin/main (run it before opening a PR)
    python tools/pr_guard.py --base main --head my-branch --report report.md
    python tools/pr_guard.py --contract-only       # only: do the plugin and the server still speak the same protocol?

It only READS the change with git (diff and file contents). It never imports, builds or runs anything from the pull
request, so GitHub runs it from the main branch against untrusted pull requests (.github/workflows/pr-guard.yml): a
pull request can't change the guard that checks it.

Verdict: 🟢 nothing sensitive · 🟡 sensitive area, review and test it carefully · 🔴 blocked (exit code 1).
"""
import argparse
import ast
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request
from typing import Dict, List, Optional, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from privacy import ROOT, FORBIDDEN_NAMES, GENERIC_SECRETS, load_private_patterns, scan_bytes  # noqa: E402

MARKER = "<!-- oras-pr-guard -->"
MAX_FILES = 300                 # more than this is not a reviewable pull request
MAX_FILE_BYTES = 1000000        # no file over 1 MB (images included)
BIG_CHANGE = 2000               # added + removed lines
MAX_LINE = 2000                 # longer lines are cut before matching (no slow regexes on huge lines)
BLOCK, WARN = "block", "warn"

# ---------------------------------------------------------------------------------------------------------------------
# Rules
# ---------------------------------------------------------------------------------------------------------------------

# Only the maintainer changes these: the CI, the publishing tools and what players run on their PC trust them.
PROTECTED = [
    (re.compile(r"^\.github/"), "configuración de GitHub y comprobaciones automáticas", "GitHub settings and workflows"),
    (re.compile(r"^tools/(pr_guard|test_pr_guard|privacy|prepublish_check|package_public)\.py$"),
     "las herramientas que revisan y publican el proyecto", "the tools that check and publish the project"),
    (re.compile(r"^packaging/"), "lo que los jugadores ejecutan en su PC (instalador del ZIP)",
     "what players run on their PC (the ZIP installer)"),
    (re.compile(r"^(LICENSE|SECURITY\.md|\.gitattributes)$"), "licencia y política de seguridad",
     "license and security policy"),
]

EXTRA_FORBIDDEN = [
    (re.compile(r"\.(elf|exe|dll|so|dylib|o|a|lib|obj|bin|nds|gba|bcfnt|bch|bcres|bcmdl|garc|cgfx|ctpk|darc|"
                r"7z|rar|tar|gz|xz|bz2|jar|apk|msi|dmg|iso|bak)$", re.I),
     "archivo binario o del juego", "binary or game file"),
    (re.compile(r"(^|/)(romfs|exefs)/", re.I), "volcado del juego (RomFS/ExeFS)", "game dump (RomFS/ExeFS)"),
    (re.compile(r"(^|/)mmo_(key|dev_[^/]*)$|(^|/)mmo_key\.txt$"), "archivo de una instalación real (llave, pruebas)",
     "file from a real install (key, tests)"),
]
IMAGE_EXT = (".png", ".jpg", ".jpeg", ".gif", ".webp")
SCRIPT_EXT = (".sh", ".bat", ".cmd", ".ps1", ".js", ".vbs", ".py")

PLUGIN_CODE = re.compile(r"^3ds_plugin/(Sources|Includes)/.*\.(c|cc|cpp|h|hpp|inc|s)$")
SERVER_MAIN = "server/server.py"
PROTOCOL_HPP = "3ds_plugin/Includes/Protocol.hpp"
PLGINFO = "3ds_plugin/CTRPluginFramework.plgInfo"

# Files allowed to write into the game (docs/ARCHITECTURE.md, rule 3). Touching them is fine, but must be tested.
GAME_WRITE_FILES = re.compile(r"^3ds_plugin/(Sources|Includes)/(GameHook|SkinTex|GameMaps|GameLang|DepthProbe|TownMap|"
                              r"MapLowerPanes|SafePatch|csvc)\.(cpp|hpp|h)$")
GAME_READ_FILES = re.compile(r"^3ds_plugin/(Sources|Includes)/(GameState|GameStats|RomFS|HeroAssets|Lzma|Follower3D)\."
                             r"(cpp|hpp|h)$")
NETWORK_FILES = re.compile(r"^(3ds_plugin/(Includes/Protocol\.hpp|Sources/NetworkClient\.cpp|Includes/NetworkClient\.hpp|"
                           r"Sources/Players\.cpp|Sources/PlayerKey\.cpp)|server/server\.py)$")
HOST_FILES = re.compile(r"^server/(Dockerfile|docker-compose\.yml|run_server\.bat|start_azahar_room\.sh|\.env\.example)$")

# Writing into the game's memory or code, or into the save, outside the approved files: blocked
GAME_WRITE_RX = [
    re.compile(r"Process::(Write\w*|Patch\w*|CopyMemory|ProtectMemory\w*)"),
    re.compile(r"\b(Hook|HookContext|HookResult|MenuHook)\b"),
    re.compile(r"\bsvc(ControlProcessMemory|ControlMemory\w*|MapProcessMemory\w*|UnmapProcessMemory\w*|"
               r"FlushProcessDataCache|InvalidateProcessDataCache|CustomBackdoor|Backdoor|KernelSetState|"
               r"ControlProcess)\b"),
    re.compile(r"\bSafePatch::"),
    re.compile(r"\*\s*\(\s*(volatile\s+)?[\w:]+\s*\*\s*\)\s*\(?[^;=()]*\)?\s*(=|\+=|-=|\|=|&=|\^=)(?!=)"),
    re.compile(r"\bmem(cpy|set|move)\s*\(\s*\(\s*(void|u8|u16|u32|char|uint8_t|uint32_t)\s*\*\s*\)\s*\(?\s*(0x|[A-Z_]{3,})"),
    re.compile(r"\b(ARCHIVE_SAVEDATA|ARCHIVE_EXTDATA|ARCHIVE_SYSTEM_SAVEDATA|FSUSER_(Delete|Rename|Create|Format|Control)"
               r"\w*|FSFILE_(Write|SetSize)\w*)\b"),
]
GAME_READ_RX = re.compile(r"\(\s*volatile\s+[\w:]+\s*\*\s*\)|Process::(Read\w*|CheckAddress)")
SD_FILE_RX = re.compile(r"\bFile::(Remove|Rename)|\bDirectory::(Remove|Rename|Create)")
PLUGIN_NET_RX = re.compile(r"(?<![\w:.])(socket|connect|sendto|recvfrom|bind|listen|accept|gethostbyname|getaddrinfo|"
                           r"inet_addr|inet_pton)\s*\(")

SERVER_BLOCK_RX = [
    (re.compile(r"(?<![\w.])(eval|exec|compile|__import__)\s*\("), "ejecuta código dinámico", "runs dynamic code"),
    (re.compile(r"\b(pickle|marshal|shelve|ctypes|subprocess|multiprocessing)\b"), "usa un módulo peligroso",
     "uses a dangerous module"),
    (re.compile(r"\bos\.(system|popen|exec\w*|spawn\w*|fork)\b"), "ejecuta programas", "runs programs"),
    (re.compile(r"^\s*(import|from)\s+(urllib|http|requests|httpx|aiohttp|ftplib|smtplib|telnetlib|webbrowser)\b|"
                r"\b(urllib|requests|httpx|aiohttp)\.\w+"), "se conecta a internet", "connects to the internet"),
    (re.compile(r"\b(create_connection|open_connection)\s*\(|\.connect\s*\("), "abre conexiones hacia fuera",
     "opens outgoing connections"),
]
SERVER_IMPORTS_OK = {"asyncio", "collections", "hashlib", "json", "logging", "os", "socket", "struct", "sys", "time",
                     "typing", "re", "math", "random", "secrets", "ipaddress", "functools", "itertools", "enum",
                     "dataclasses", "contextlib", "signal"}
SERVER_IMPORT_RX = re.compile(r"^\s*(?:import|from)\s+([A-Za-z_][\w]*)")
SERVER_LIMIT_RX = re.compile(r"\b(MAX_\w+|ROOM_TRIES\w*|RANK_\w+|BURST|IDLE_TIMEOUT|QUEUE_MAX|HANDSHAKE_TIMEOUT|"
                             r"EXEMPT_LOOPBACK|RELAY_TYPES|ALLOWED_ROOMS|KEY_LEN)\s*=(?!=)")
SERVER_FILE_RX = re.compile(r"\bos\.(remove|unlink|rmdir|removedirs|chmod|chown)\b|\bshutil\.")

IPV4 = re.compile(r"(?<![\d.])(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})(?![\d.])")
URL_RX = re.compile(r"\b(?:https?|ftp|wss?)://", re.I)
USES_RX = re.compile(r"^\s*(-\s+)?uses:\s+[\w.-]+/[\w./-]+@[0-9a-f]{40}(\s+#\s*[\w.+-]+)?\s*$")


def ip_allowed(m) -> bool:
    """127.x (this PC), 0.0.0.0 / 255.x (masks) and the documentation ranges are fine in code; any other is a real host."""
    o = [int(x) for x in m.groups()]
    if any(x > 255 for x in o):
        return True                                   # not an address (e.g. a version number)
    return (o[0] == 127 or o == [0, 0, 0, 0] or o[0] == 255 or o[:3] in ([192, 0, 2], [198, 51, 100], [203, 0, 113]))


# ---------------------------------------------------------------------------------------------------------------------
# Findings and git helpers
# ---------------------------------------------------------------------------------------------------------------------

class Finding:
    def __init__(self, level: str, path: str, es: str, en: str):
        self.level, self.path, self.es, self.en = level, path, es, en

    def __repr__(self):
        return "%s %s: %s" % (self.level, self.path, self.en)


class Repo:
    def __init__(self, path: str):
        self.path = path

    def git(self, *args: str, check: bool = True) -> bytes:
        cmd = ["git", "-c", "core.quotepath=off", "-c", "core.autocrlf=false", "--literal-pathspecs"] + list(args)
        out = subprocess.run(cmd, cwd=self.path, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if check and out.returncode != 0:
            raise RuntimeError("git %s: %s" % (args[0], out.stderr.decode("utf-8", "replace").strip()[:300]))
        return out.stdout

    def rev(self, ref: str) -> str:
        return self.git("rev-parse", "--verify", "--quiet", ref + "^{commit}").decode().strip()

    def blob(self, sha: str) -> bytes:
        return self.git("cat-file", "blob", sha)

    def blob_size(self, sha: str) -> int:
        return int(self.git("cat-file", "-s", sha).decode().strip())

    def file_at(self, commit: str, path: str) -> Optional[bytes]:
        out = subprocess.run(["git", "cat-file", "blob", "%s:%s" % (commit, path)], cwd=self.path,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return out.stdout if out.returncode == 0 else None

    def changes(self, base: str, head: str) -> List[dict]:
        """git diff --raw: one entry per changed file (status, old/new mode and blob, old/new path)."""
        raw = self.git("diff", "--raw", "-z", "-M", "--abbrev=40", "--no-ext-diff", "--no-textconv", base, head)
        parts = raw.split(b"\0")
        out, i = [], 0
        while i < len(parts) and parts[i]:
            meta = parts[i].decode("utf-8", "replace").lstrip(":").split()
            status = meta[4]
            old_path = parts[i + 1].decode("utf-8", "replace")
            if status[0] in "RC":
                new_path = parts[i + 2].decode("utf-8", "replace")
                i += 3
            else:
                new_path = old_path
                i += 2
            out.append({"status": status[0], "old_mode": meta[0], "new_mode": meta[1], "old_sha": meta[2],
                        "new_sha": meta[3], "old_path": old_path, "path": new_path})
        return out

    def diff_lines(self, base: str, head: str, path: str, old_path: str) -> Tuple[List[str], List[str]]:
        """Added and removed lines of one file (text files only)."""
        paths = [old_path, path] if old_path != path else [path]
        out = self.git("diff", "-U0", "-M", "--no-color", "--no-ext-diff", "--no-textconv", base, head, "--", *paths)
        added, removed = [], []
        for line in out.decode("utf-8", "replace").split("\n"):
            if line.startswith("+++") or line.startswith("---"):
                continue
            if line.startswith("+"):
                added.append(line[1:][:MAX_LINE])
            elif line.startswith("-"):
                removed.append(line[1:][:MAX_LINE])
        return added, removed


# ---------------------------------------------------------------------------------------------------------------------
# Protocol contract: plugin (Protocol.hpp) and server (server.py) must agree
# ---------------------------------------------------------------------------------------------------------------------

C_TYPES = {"uint8_t": 1, "int8_t": 1, "u8": 1, "s8": 1, "char": 1, "uint16_t": 2, "int16_t": 2, "u16": 2, "s16": 2,
           "uint32_t": 4, "int32_t": 4, "u32": 4, "s32": 4, "float": 4}
CLIENT_TYPES = ("POSITION", "EMOTE", "CHAT", "EVENT_REQ", "EVENT_RESP", "EVENT_END", "PING", "STATS", "BATTLE")
SERVER_ONLY = ("LEAVE", "RANK_SNAPSHOT", "KEY", "NAME_TAKEN")


def parse_protocol_hpp(text: str):
    code = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    code = re.sub(r"//[^\n]*", "", code)
    defines = {}
    for m in re.finditer(r"^\s*#define\s+([A-Z][A-Z0-9_]*)\s+(0x[0-9A-Fa-f]+|\d+|\"[^\"]*\")", code, re.M):
        v = m.group(2)
        defines[m.group(1)] = v[1:-1] if v.startswith('"') else int(v, 0)
    fields, size = {}, 0
    m = re.search(r"struct\s+PlayerPacket\s*\{(.*?)\};", code, re.S)
    if m:
        for f in re.finditer(r"(\w+)\s+(\w+)\s*(?:\[\s*(\d+)\s*\])?\s*;", m.group(1)):
            t, name, n = f.group(1), f.group(2), int(f.group(3) or 1)
            if t not in C_TYPES:
                return defines, None, 0
            fields[name] = (size, C_TYPES[t] * n)
            size += C_TYPES[t] * n
    return defines, (fields if m else None), size


def _eval(node, env):
    """Tiny evaluator for module-level constants of server.py (parsed, never executed)."""
    if isinstance(node, ast.Constant):
        return node.value
    if isinstance(node, ast.Name):
        return env.get(node.id)
    if isinstance(node, (ast.Tuple, ast.List, ast.Set)):
        return tuple(_eval(e, env) for e in node.elts)
    if isinstance(node, ast.Call) and isinstance(node.func, ast.Name):
        if node.func.id in ("int", "float") and node.args:
            inner = node.args[0]
            if (isinstance(inner, ast.Call) and isinstance(inner.func, ast.Attribute) and inner.func.attr == "get"
                    and len(inner.args) == 2):
                return _eval(inner.args[1], env)
            return _eval(inner, env)
        if node.func.id in ("frozenset", "set", "tuple") and node.args:
            v = _eval(node.args[0], env)
            return tuple(v) if isinstance(v, tuple) else None
    return None


def parse_server_py(text: str):
    tree = ast.parse(text)
    env = {}
    for _ in range(2):                  # twice: a constant may use a name assigned further down
        for st in tree.body:
            if not isinstance(st, ast.Assign) or len(st.targets) != 1:
                continue
            t = st.targets[0]
            if isinstance(t, ast.Name):
                env[t.id] = _eval(st.value, env)
            elif isinstance(t, ast.Tuple) and isinstance(st.value, ast.Tuple) and len(t.elts) == len(st.value.elts):
                for n, v in zip(t.elts, st.value.elts):
                    if isinstance(n, ast.Name):
                        env[n.id] = _eval(v, env)
    return env


def check_contract(hpp: Optional[bytes], srv: Optional[bytes]) -> List[Finding]:
    f = []

    def bad(es, en, path=SERVER_MAIN):
        f.append(Finding(BLOCK, path, "El plugin y el servidor ya no se entienden: " + es,
                         "The plugin and the server no longer match: " + en))

    if hpp is None or srv is None:
        bad("falta Protocol.hpp o server.py", "Protocol.hpp or server.py is missing")
        return f
    srv_text = srv.decode("utf-8", "replace")
    try:
        ast.parse(srv_text, feature_version=(3, 8))
    except SyntaxError as e:
        f.append(Finding(BLOCK, SERVER_MAIN, "server.py debe funcionar con Python 3.8 o más (línea %s)" % e.lineno,
                         "server.py must run on Python 3.8+ (line %s)" % e.lineno))
        return f
    try:
        env = parse_server_py(srv_text)
    except SyntaxError as e:
        bad("server.py no se puede leer (línea %s)" % e.lineno, "server.py can't be parsed (line %s)" % e.lineno)
        return f
    d, fields, size = parse_protocol_hpp(hpp.decode("utf-8", "replace"))
    if fields is None:
        bad("no encuentro el paquete PlayerPacket en Protocol.hpp", "PlayerPacket not found in Protocol.hpp", PROTOCOL_HPP)
        return f
    if d.get("MMO_DEFAULT_SERVER") != "127.0.0.1":
        f.append(Finding(BLOCK, PROTOCOL_HPP, "El servidor por defecto del plugin público tiene que ser 127.0.0.1 "
                         "(si no, todos los jugadores se conectarían a otro servidor)",
                         "The public plugin's default server must stay 127.0.0.1 (otherwise every player would "
                         "connect to someone else's server)"))
    psize = d.get("MMO_PACKET_SIZE")
    if not (size == psize == env.get("PKT_SIZE") == 64):
        bad("el paquete mide %s bytes en el plugin y %s en el servidor (tienen que ser 64)" % (size, env.get("PKT_SIZE")),
            "the packet is %s bytes in the plugin and %s in the server (must be 64)" % (size, env.get("PKT_SIZE")))
    want = {"OFF_MODEL": ("model_id", 0), "OFF_FLAGS": ("flags", 0), "OFF_VERSION": ("lvls", 2),
            "OFF_NAME": ("nickname", 0), "OFF_MSG": ("msg", 0)}
    for const, (field, extra) in want.items():
        if field not in fields or env.get(const) != fields[field][0] + extra:
            bad("%s del servidor no coincide con el campo %s del plugin" % (const, field),
                "server %s doesn't match plugin field %s" % (const, field))
    plugin_types = {k[len("MMO_PKT_"):]: v for k, v in d.items() if k.startswith("MMO_PKT_")}
    for k, v in env.items():
        if k.startswith("PKT_") and k != "PKT_SIZE" and isinstance(v, int) and k[4:] in plugin_types:
            if plugin_types[k[4:]] != v:
                bad("el tipo %s vale 0x%02X en el servidor y 0x%02X en el plugin" % (k[4:], v, plugin_types[k[4:]]),
                    "type %s is 0x%02X in the server and 0x%02X in the plugin" % (k[4:], v, plugin_types[k[4:]]))
    relay = env.get("RELAY_TYPES")
    if not isinstance(relay, tuple) or not all(isinstance(x, int) for x in relay):
        bad("no puedo leer RELAY_TYPES (qué paquetes reenvía el servidor)",
            "can't read RELAY_TYPES (which packets the server relays)")
    else:
        for name in CLIENT_TYPES:
            if name in plugin_types and plugin_types[name] not in relay:
                bad("el servidor ya no reenvía %s" % name, "the server no longer relays %s" % name)
        for name in SERVER_ONLY:
            if name in plugin_types and plugin_types[name] in relay:
                f.append(Finding(BLOCK, SERVER_MAIN, "el servidor reenviaría %s, que solo puede mandar el servidor "
                                 "(salidas o rankings falsos, o llaves filtradas)" % name,
                                 "the server would relay %s, which only the server may send (fake leaves or "
                                 "rankings, or leaked keys)" % name))
    mp, mmp = env.get("MAX_PLAYERS"), d.get("MAX_MMO_PLAYERS")
    if not (isinstance(mp, int) and isinstance(mmp, int) and mp <= mmp):
        bad("el servidor admite %s jugadores por sala y el plugin %s" % (mp, mmp),
            "the server allows %s players per room and the plugin %s" % (mp, mmp))
    pv, rmv = d.get("MMO_PROTOCOL_VERSION"), env.get("RANK_MIN_VERSION")
    if not (isinstance(pv, int) and isinstance(rmv, int) and pv >= rmv):
        bad("la versión de protocolo del plugin (%s) es menor que la que pide el servidor (%s)" % (pv, rmv),
            "the plugin protocol version (%s) is lower than the server needs (%s)" % (pv, rmv))
    msg = fields.get("msg", (0, 0))[1]
    if isinstance(env.get("STATS_LEN"), int) and env["STATS_LEN"] + 2 > msg:
        bad("las estadísticas del ranking no caben en msg", "ranking stats don't fit in msg")
    if isinstance(env.get("KEY_LEN"), int) and env["KEY_LEN"] > msg:
        bad("la llave del ranking no cabe en msg", "the ranking key doesn't fit in msg")
    return f


# ---------------------------------------------------------------------------------------------------------------------
# The guard
# ---------------------------------------------------------------------------------------------------------------------

def analyze(repo: Repo, base: str, head: str, author: str = "", use_private: bool = True):
    """Returns (findings, stats). Reads the change with git only."""
    findings: List[Finding] = []
    head_sha = repo.rev(head)
    base_sha = repo.rev(base)
    if not head_sha or not base_sha:
        raise SystemExit("pr_guard: unknown base or head (%s / %s)" % (base, head))
    mb = repo.git("merge-base", base_sha, head_sha, check=False).decode().strip()
    if not mb:
        findings.append(Finding(BLOCK, "-", "La rama no comparte historia con main", "The branch shares no history "
                                "with main"))
        return findings, {"files": 0, "added": 0, "removed": 0}
    changes = repo.changes(mb, head_sha)
    stats = {"files": len(changes), "added": 0, "removed": 0}
    if len(changes) > MAX_FILES:
        findings.append(Finding(BLOCK, "-", "Cambia %d archivos: demasiados para revisarlo; divídelo en partes"
                                % len(changes), "It changes %d files: too many to review; split it" % len(changes)))
        return findings, stats
    private = load_private_patterns() if use_private else []
    dependabot = author == "dependabot[bot]"
    touched_network = False

    def add(level, path, es, en):
        findings.append(Finding(level, path, es, en))

    for c in changes:
        path, old_path, status = c["path"], c["old_path"], c["status"]
        lower = path.lower()
        # -- protected files (also when renamed away or deleted)
        for rx, es, en in PROTECTED:
            if rx.search(path) or rx.search(old_path):
                if dependabot and path.startswith(".github/workflows/") and status == "M":
                    a, r = repo.diff_lines(mb, head_sha, path, old_path)
                    if all(USES_RX.match(x) for x in a + r if x.strip()):
                        add(WARN, path, "Dependabot actualiza acciones de GitHub (fijadas por SHA): revisa la nota de "
                            "versión", "Dependabot updates GitHub actions (pinned by SHA): check the release notes")
                        break
                add(BLOCK, path, "Archivo protegido (%s): solo lo cambia el dueño del proyecto" % es,
                    "Protected file (%s): only the project owner changes it" % en)
                break
        if status == "D":
            if re.search(r"(^|/)test_[^/]*\.py$", path):
                add(WARN, path, "Borra un archivo de pruebas", "Deletes a test file")
            continue
        # -- symlinks and submodules
        if c["new_mode"] in ("120000", "160000"):
            add(BLOCK, path, "Enlace simbólico o submódulo: no se aceptan", "Symlink or submodule: not accepted")
            continue
        # -- names that must never be published
        for rx, why in FORBIDDEN_NAMES:
            if rx.search(path):
                add(BLOCK, path, "Archivo prohibido (%s)" % why, "Forbidden file (%s)" % why)
                break
        else:
            for rx, es, en in EXTRA_FORBIDDEN:
                if rx.search(path):
                    add(BLOCK, path, "Archivo prohibido: %s" % es, "Forbidden file: %s" % en)
                    break
        size = repo.blob_size(c["new_sha"])
        if size > MAX_FILE_BYTES:
            add(BLOCK, path, "Pesa %d KB (máximo 1 MB por archivo)" % (size // 1024),
                "It is %d KB (1 MB per file at most)" % (size // 1024))
            continue
        data = repo.blob(c["new_sha"])
        binary = b"\0" in data[:8000]
        if binary and not lower.endswith(IMAGE_EXT):
            add(BLOCK, path, "Archivo binario: solo se aceptan imágenes (y como mucho 1 MB)",
                "Binary file: only images are accepted (1 MB at most)")
            continue
        for why in scan_bytes(data, private):
            add(BLOCK, path, "Contiene un dato privado del proyecto (%s)" % why, "Contains private project data")
        if binary:
            continue
        added, removed = repo.diff_lines(mb, head_sha, path, old_path)
        stats["added"] += len(added)
        stats["removed"] += len(removed)
        added_text = "\n".join(added)
        for rx, why in GENERIC_SECRETS:
            if rx.search(added_text):
                add(BLOCK, path, "Parece una clave o contraseña (%s)" % why, "Looks like a secret (%s)" % why)
        is_plugin = bool(PLUGIN_CODE.search(path))
        is_doc = lower.endswith((".md", ".txt"))
        # -- addresses and URLs in code
        if not is_doc:
            for line in added:
                for m in IPV4.finditer(line):
                    if not ip_allowed(m):
                        lvl = BLOCK if (is_plugin or path == SERVER_MAIN) else WARN
                        add(lvl, path, "Pone una dirección IP real en el código (%s)" % m.group(0),
                            "Puts a real IP address in the code (%s)" % m.group(0))
                if (is_plugin or path == SERVER_MAIN) and URL_RX.search(line):
                    add(BLOCK, path, "Pone una dirección web en el código: el plugin y el servidor no se conectan a "
                        "nada más", "Puts a web address in the code: the plugin and the server never connect to "
                        "anything else")
        # -- the plugin and the game
        if is_plugin:
            approved = bool(GAME_WRITE_FILES.search(path))
            if approved:
                add(WARN, path, "Toca la memoria del juego (zona aprobada): pruébalo en Azahar antes de aceptarlo",
                    "Touches the game's memory (approved area): test it in Azahar before merging")
            elif GAME_READ_FILES.search(path):
                add(WARN, path, "Lee la memoria o los archivos del juego: un puntero malo cuelga el emulador",
                    "Reads the game's memory or files: a bad pointer freezes the emulator")
            writes = [x for x in added if not approved and any(rx.search(x) for rx in GAME_WRITE_RX)]
            if writes:
                add(BLOCK, path, "Escribe en la memoria, el código o la partida del juego fuera de las zonas "
                    "aprobadas", "Writes into the game's memory, code or save outside the approved files")
            if not approved and not GAME_READ_FILES.search(path) and any(GAME_READ_RX.search(x) for x in added
                                                                        if x not in writes):
                add(WARN, path, "Lee memoria directa: revisa que el puntero se compruebe antes",
                    "Reads raw memory: check that the pointer is validated first")
            if any(SD_FILE_RX.search(x) for x in added):
                add(WARN, path, "Borra o renombra archivos de la tarjeta SD", "Deletes or renames files on the SD card")
            if not path.endswith(("NetworkClient.cpp", "NetworkClient.hpp")) and any(PLUGIN_NET_RX.search(x)
                                                                                 for x in added):
                add(BLOCK, path, "Código de red nuevo fuera de NetworkClient: el plugin solo habla con el servidor "
                    "elegido", "New network code outside NetworkClient: the plugin only talks to the chosen server")
        if path == PROTOCOL_HPP and any("MMO_PROTOCOL_VERSION" in x for x in added):
            add(WARN, path, "Cambia la versión del protocolo: plugin y servidor se publican a la vez",
                "Changes the protocol version: plugin and server must be released together")
        if path == PLGINFO and any("MemorySize" in x for x in added + removed):
            add(WARN, path, "Cambia la memoria del plugin (MemorySize)", "Changes the plugin memory (MemorySize)")
        # -- the server
        if path == SERVER_MAIN:
            for rx, es, en in SERVER_BLOCK_RX:
                if any(rx.search(x) for x in added):
                    add(BLOCK, path, "El servidor %s: es un relé sin dependencias y no puede hacerlo" % es,
                        "The server %s: it is a dependency-free relay and must not" % en)
            for x in added:
                m = SERVER_IMPORT_RX.match(x)
                if m and m.group(1) not in SERVER_IMPORTS_OK:
                    add(WARN, path, "El servidor importa «%s»" % m.group(1), "The server imports '%s'" % m.group(1))
            if any(SERVER_LIMIT_RX.search(x) for x in added + removed):
                add(WARN, path, "Cambia un límite de seguridad del servidor (conexiones, salas, ranking...)",
                    "Changes a server security limit (connections, rooms, ranking...)")
            if any(SERVER_FILE_RX.search(x) for x in added):
                add(WARN, path, "El servidor borra o cambia permisos de archivos", "The server deletes files or "
                    "changes permissions")
        if path == "server/requirements.txt" and any(x.strip() and not x.strip().startswith("#") for x in added):
            add(BLOCK, path, "Añade dependencias al servidor: está hecho para no tener ninguna",
                "Adds server dependencies: it is built to have none")
        if NETWORK_FILES.search(path):
            touched_network = True
        if HOST_FILES.search(path):
            add(WARN, path, "Lo ejecuta en su equipo quien monta un servidor: revísalo línea por línea",
                "Whoever hosts a server runs this on their machine: review it line by line")
        if path == ".gitignore":
            add(WARN, path, "Cambia qué archivos ignora git (podría dejar subir archivos privados)",
                "Changes what git ignores (it could let private files in)")
        if status == "A" and lower.endswith(SCRIPT_EXT) and not lower.startswith(("server/test_", "tools/test_")):
            add(WARN, path, "Script nuevo: revisa qué hace antes de ejecutarlo", "New script: check what it does "
                "before running it")
        if re.search(r"(^|/)test_[^/]*\.py$", path) and sum(1 for x in removed if "def test_" in x) > sum(
                1 for x in added if "def test_" in x):
            add(WARN, path, "Quita pruebas", "Removes tests")

    if touched_network:
        add(WARN, "-", "Toca la red o el protocolo: pruébalo con el servidor local y amigos de prueba",
            "Touches the network or the protocol: test it with a local server and fake players")
    if stats["added"] + stats["removed"] > BIG_CHANGE:
        add(WARN, "-", "Cambio muy grande (%d líneas): pide que lo divida" % (stats["added"] + stats["removed"]),
            "Very big change (%d lines): ask to split it" % (stats["added"] + stats["removed"]))
    findings += check_contract(repo.file_at(head_sha, PROTOCOL_HPP), repo.file_at(head_sha, SERVER_MAIN))
    return dedupe(findings), stats


def dedupe(findings: List[Finding]) -> List[Finding]:
    seen, out = set(), []
    for f in findings:
        k = (f.level, f.path, f.en)
        if k not in seen:
            seen.add(k)
            out.append(f)
    out.sort(key=lambda f: (f.level != BLOCK, f.path))
    return out


def verdict(findings: List[Finding]) -> str:
    if any(f.level == BLOCK for f in findings):
        return BLOCK
    return WARN if findings else "ok"


# ---------------------------------------------------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------------------------------------------------

def md_path(p: str) -> str:
    """File names come from the pull request: shown as plain code, never as markdown or mentions."""
    p = re.sub(r"[\x00-\x1f\x7f`]", "?", p)[:120]
    return "`%s`" % p if p != "-" else "—"


def report(findings: List[Finding], stats: dict, contract_only: bool = False) -> str:
    v = verdict(findings)
    head_es = {"ok": "🟢 Sin riesgo a la vista", WARN: "🟡 Revisar con cuidado", BLOCK: "🔴 Bloqueado"}[v]
    head_en = {"ok": "🟢 Nothing risky found", WARN: "🟡 Review carefully", BLOCK: "🔴 Blocked"}[v]
    icon = {BLOCK: "🔴", WARN: "🟡"}
    lines = [MARKER, "## 🛡️ Guardián del PR — %s" % head_es, ""]
    if not contract_only:
        lines.append("%d %s · +%d / −%d líneas · el guardián solo lee el cambio, nunca lo ejecuta."
                     % (stats["files"], "archivo" if stats["files"] == 1 else "archivos", stats["added"],
                        stats["removed"]))
        lines.append("")
    shown = findings[:40]
    if shown:
        lines += ["| | Archivo | Qué pasa |", "|:-:|---|---|"]
        lines += ["| %s | %s | %s |" % (icon[f.level], md_path(f.path), f.es.replace("|", "/")) for f in shown]
        if len(findings) > len(shown):
            lines.append("")
            lines.append("…y %d avisos más." % (len(findings) - len(shown)))
        lines.append("")
    lines += ["**Para el dueño:** " + {
        "ok": "si las demás comprobaciones salen en verde, puedes aceptarlo con *Squash and merge*.",
        WARN: "no lo aceptes solo por esto: pide una revisión del código (en Claude: `/requesting-code-review`) y "
              "prueba en un Azahar de pruebas el `.3gx` que deja la comprobación *plugin*.",
        BLOCK: "no lo aceptes. Quien lo envió tiene que corregir lo que sale en 🔴 (o, si de verdad hace falta, "
               "lo decides tú tras una revisión experta).",
    }[v], "", "Nada se instala solo: aceptar un PR no toca el servidor ni los ZIP de tus amigos.", ""]
    lines += ["<details><summary>English</summary>", "", "### %s" % head_en, ""]
    if shown:
        lines += ["| | File | What |", "|:-:|---|---|"]
        lines += ["| %s | %s | %s |" % (icon[f.level], md_path(f.path), f.en.replace("|", "/")) for f in shown]
        lines.append("")
    lines += [{"ok": "Nothing sensitive: if the other checks are green, the maintainer can merge it.",
               WARN: "Sensitive areas: the maintainer will review the code and test the build in Azahar.",
               BLOCK: "Blocked: please fix the 🔴 items. See CONTRIBUTING.md (protected files and golden rules)."}[v],
              "", "</details>", ""]
    return "\n".join(lines)


def gh_escape(s: str, prop: bool = False) -> str:
    s = s.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    return s.replace(":", "%3A").replace(",", "%2C") if prop else s


def post_comment(body: str, pr: str) -> None:
    """Creates or updates the guard's comment on the pull request (needs GITHUB_TOKEN with pull-requests: write)."""
    token, repo = os.environ.get("GITHUB_TOKEN"), os.environ.get("GITHUB_REPOSITORY")
    if not token or not repo or not pr.isdigit():
        print("  (no comment: GITHUB_TOKEN / GITHUB_REPOSITORY / PR number missing)")
        return
    api = "https://api.github.com/repos/%s/issues" % repo

    def call(method, url, payload=None):
        req = urllib.request.Request(url, method=method, data=None if payload is None else json.dumps(payload).encode(),
                                     headers={"Authorization": "Bearer " + token, "Accept": "application/vnd.github+json",
                                              "X-GitHub-Api-Version": "2022-11-28", "User-Agent": "oras-pr-guard"})
        with urllib.request.urlopen(req, timeout=20) as r:
            return json.loads(r.read().decode() or "null")

    body = body[:60000]
    try:
        mine = None
        for page in range(1, 6):
            batch = call("GET", "%s/%s/comments?per_page=100&page=%d" % (api, pr, page))
            for c in batch:
                if MARKER in (c.get("body") or "") and (c.get("user") or {}).get("type") == "Bot":
                    mine = c["id"]
            if len(batch) < 100:
                break
        if mine:
            call("PATCH", "%s/comments/%d" % (api, mine), {"body": body})
        else:
            call("POST", "%s/%s/comments" % (api, pr), {"body": body})
        print("  comment updated on the pull request")
    except (urllib.error.URLError, OSError, ValueError) as e:
        print("::warning::%s" % gh_escape("Could not post the guard comment: %r" % e))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Checks a pull request without running it.")
    ap.add_argument("--repo", default=ROOT, help="git repository (default: this one)")
    ap.add_argument("--base", default="origin/main")
    ap.add_argument("--head", default="HEAD")
    ap.add_argument("--author", default="", help="pull request author (dependabot[bot] may update pinned actions)")
    ap.add_argument("--report", help="also write the markdown report to this file")
    ap.add_argument("--comment", metavar="PR", help="post the report as a comment on this pull request number")
    ap.add_argument("--contract-only", action="store_true", help="only check plugin/server protocol (working tree)")
    a = ap.parse_args(argv)

    if a.contract_only:
        def read(p):
            fp = os.path.join(a.repo, p)
            if not os.path.exists(fp):
                return None
            with open(fp, "rb") as fh:
                return fh.read()
        findings, stats = check_contract(read(PROTOCOL_HPP), read(SERVER_MAIN)), {"files": 0, "added": 0, "removed": 0}
    else:
        findings, stats = analyze(Repo(a.repo), a.base, a.head, a.author)
    text = report(findings, stats, a.contract_only)
    v = verdict(findings)
    for f in findings:
        kind = "error" if f.level == BLOCK else "warning"
        prop = (" file=%s" % gh_escape(f.path, True)) if f.path != "-" else ""
        if os.environ.get("GITHUB_ACTIONS") == "true":
            print("::%s%s::%s" % (kind, prop, gh_escape(f.en)))
        else:
            print("  %s %s: %s" % ("[X]" if f.level == BLOCK else "[!]", re.sub(r"[\x00-\x1f]", "?", f.path), f.en))
    print("  verdict: %s (%d findings)" % ({"ok": "OK", WARN: "REVIEW", BLOCK: "BLOCKED"}[v], len(findings)))
    if a.report:
        open(a.report, "w", encoding="utf-8").write(text)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as s:
            s.write(text + "\n")
    if a.comment:
        post_comment(text, a.comment)
    return 1 if v == BLOCK else 0


if __name__ == "__main__":
    sys.exit(main())
