"""Builds the public release ZIP (dist/ORAS_MMO_v<version>_Global.zip) from a plugin built WITHOUT a private
ServerConfig.local.hpp.

    python tools/package_public.py path/to/OrasMmo.3gx [--version 17.0]

Refuses to package if the plugin or any packaged file contains one of your private patterns (.private_patterns.txt)
or if the plugin does not look like a 3GX file. Game-derived files (mmo_pokes_3d.bin) are never included.
"""
import argparse
import os
import re
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from privacy import ROOT, GENERIC_SECRETS, load_private_patterns, scan_bytes  # noqa: E402

# (path inside the ZIP, path in the repository)
CONTENT = [
    ("INSTALL_PC.bat", "packaging/INSTALL_PC.bat"),
    ("README.md", "packaging/README_ZIP.md"),
    ("LICENSE", "LICENSE"),
    ("shaders/ORAS_MMO_HD.glsl", "extras/shaders/ORAS_MMO_HD.glsl"),
    ("docs/PROTOCOL.md", "docs/PROTOCOL.md"),
    ("server/server.py", "server/server.py"),
    ("server/README.md", "server/README.md"),
    ("server/run_server.bat", "server/run_server.bat"),
    ("server/requirements.txt", "server/requirements.txt"),
    ("server/Dockerfile", "server/Dockerfile"),
    ("server/docker-compose.yml", "server/docker-compose.yml"),
    ("server/.env.example", "server/.env.example"),
    ("server/start_azahar_room.sh", "server/start_azahar_room.sh"),
]


def plugin_version():
    with open(os.path.join(ROOT, "3ds_plugin", "Includes", "Protocol.hpp"), encoding="utf-8") as f:
        m = re.search(r'#define\s+MMO_VERSION_STR\s+"([^"]+)"', f.read())
    return m.group(1) if m else "dev"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("plugin", help="OrasMmo.3gx built without ServerConfig.local.hpp")
    ap.add_argument("--version", default=plugin_version())
    ap.add_argument("--out", default=os.path.join(ROOT, "dist"))
    a = ap.parse_args()

    with open(a.plugin, "rb") as f:
        plugin = f.read()
    if plugin[:4] != b"3GX$":
        raise SystemExit("[X] %s is not a 3GX plugin" % a.plugin)
    private = load_private_patterns()
    if not private:
        print("[!] No .private_patterns.txt: the private-data check is limited to generic secrets.")
    bad = scan_bytes(plugin, private)
    if bad:
        raise SystemExit("[X] The plugin contains private data (%s). Build it without ServerConfig.local.hpp." % ", ".join(bad))

    files = []
    for arc, rel in CONTENT:
        with open(os.path.join(ROOT, rel), "rb") as f:
            data = f.read()
        bad = scan_bytes(data, private + GENERIC_SECRETS)
        if bad:
            raise SystemExit("[X] %s contains %s" % (rel, ", ".join(bad)))
        files.append((arc, data))

    os.makedirs(a.out, exist_ok=True)
    out = os.path.join(a.out, "ORAS_MMO_v%s_Global.zip" % a.version)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        z.writestr("OrasMmo.3gx", plugin)
        for arc, data in files:
            z.writestr(arc, data)
    with zipfile.ZipFile(out) as z:
        assert z.testzip() is None
        assert z.read("OrasMmo.3gx") == plugin
        names = z.namelist()
    print("[OK] %s (%d KB): %s" % (out, os.path.getsize(out) // 1024, ", ".join(names)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
