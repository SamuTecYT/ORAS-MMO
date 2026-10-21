"""Minimal RomFS reader for YOUR copy of the game / Lector mínimo de la RomFS de TU copia del juego.

Accepts / Admite:
  - a DECRYPTED .cxi, or a decrypted .3ds/.cci (NCSD, partition 0)   / un .cxi o .3ds/.cci DESCIFRADO
  - a folder with the extracted RomFS (the one that contains a/0/0/8) / una carpeta con la RomFS extraída

    python romfs.py <rom.cxi | rom.3ds | romfs_folder> list [filter]
"""
import os
import struct
import sys

ORAS_TITLE_IDS = {0x000400000011C400: "Omega Ruby", 0x000400000011C500: "Alpha Sapphire"}


class RomError(Exception):
    pass


class RomFS:
    """files: {"/a/0/0/8": (offset, size)}; open(path) -> (file object, base offset, size)."""

    def __init__(self, path):
        self.path = path
        self.title = None
        self.files = {}
        if os.path.isdir(path):
            self._init_dir(path)
        elif os.path.isfile(path):
            self._init_ncch(path)
        else:
            raise RomError("not found: %s" % path)

    # ---------------------------------------------------------------- extracted folder
    def _init_dir(self, root):
        if os.path.isdir(os.path.join(root, "romfs")) and not os.path.isdir(os.path.join(root, "a")):
            root = os.path.join(root, "romfs")
        self.root = root
        self.f = None
        for dp, dn, fn in os.walk(root):
            for n in fn:
                full = os.path.join(dp, n)
                rel = "/" + os.path.relpath(full, root).replace(os.sep, "/")
                self.files[rel] = (0, os.path.getsize(full))

    # ---------------------------------------------------------------- .cxi / .3ds
    def _init_ncch(self, path):
        self.root = None
        self.f = open(path, "rb"); f = self.f
        base = 0
        if f.read(0x200)[0x100:0x104] == b"NCSD":
            f.seek(0x120); base = struct.unpack("<I", f.read(4))[0] * 0x200
        f.seek(base); h = f.read(0x200)
        if h[0x100:0x104] != b"NCCH":
            raise RomError("%s is not a .cxi/.3ds game file" % path)
        self.title = struct.unpack_from("<Q", h, 0x118)[0]
        if not (h[0x18F] & 0x04):                         # NCCH flags[7] bit 2 = NoCrypto
            raise RomError("%s is encrypted: decrypt it first (see README)" % path)
        romfs_off = struct.unpack("<I", h[0x1B0:0x1B4])[0] * 0x200 + base
        f.seek(romfs_off); ivfc = f.read(0x60)
        if ivfc[:4] != b"IVFC":
            raise RomError("%s: RomFS not found (encrypted or damaged file?)" % path)
        master_size = struct.unpack("<I", ivfc[0x08:0x0C])[0]
        lvl3_blocksize = 1 << struct.unpack("<I", ivfc[0x4C:0x50])[0]
        lvl3_off = romfs_off + ((0x60 + master_size + lvl3_blocksize - 1) // lvl3_blocksize) * lvl3_blocksize
        self.l3 = lvl3_off
        f.seek(lvl3_off); hdr = struct.unpack("<10I", f.read(40))
        self.data_off = hdr[9]
        f.seek(lvl3_off + hdr[3]); self.dm = f.read(hdr[4])
        f.seek(lvl3_off + hdr[7]); self.fm = f.read(hdr[8])
        self._walk(0, "")

    def _walk(self, doff, path):
        parent, sib, child, firstfile, nxt, namelen = struct.unpack("<6I", self.dm[doff:doff + 24])
        name = self.dm[doff + 24:doff + 24 + namelen].decode("utf-16le")
        p = path + ("/" + name if doff else "")
        fo = firstfile
        while fo != 0xFFFFFFFF:
            par, fsib, dataoff, size, nxtf, fnamelen = struct.unpack("<IIQQII", self.fm[fo:fo + 32])
            fname = self.fm[fo + 32:fo + 32 + fnamelen].decode("utf-16le")
            self.files[p + "/" + fname] = (self.l3 + self.data_off + dataoff, size)
            fo = fsib
        c = child
        while c != 0xFFFFFFFF:
            self._walk(c, p)
            c = struct.unpack("<I", self.dm[c + 4:c + 8])[0]

    # ---------------------------------------------------------------- access
    def open(self, path):
        if path not in self.files:
            raise RomError("%s not found in the RomFS: is this Pokemon Omega Ruby / Alpha Sapphire?" % path)
        off, size = self.files[path]
        if self.root is not None:
            return open(os.path.join(self.root, path.lstrip("/")), "rb"), 0, size
        return self.f, off, size

    def read(self, path):
        f, off, size = self.open(path)
        f.seek(off)
        return f.read(size)


if __name__ == "__main__":
    r = RomFS(sys.argv[1])
    if r.title is not None:
        print("title %016X (%s)" % (r.title, ORAS_TITLE_IDS.get(r.title, "not ORAS")))
    flt = sys.argv[3] if len(sys.argv) > 3 else ""
    for p, (o, s) in sorted(r.files.items()):
        if flt in p:
            print("%12d  %s" % (s, p))
    print(len(r.files), "files")
